#include "iv/vk/renderer.hpp"

#include "iv/assert.hpp"
#include "iv/vk/colormap_lut.hpp"
#include "iv/vk/commands.hpp"
#include "iv/vk/memory.hpp"
#include "iv/vk/result.hpp"
#include "iv/vk/shaders.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace iv::vk {

namespace vkh = ::vk;

namespace {

constexpr std::uint32_t kLocalSize = 8;       // matches shaders/ray_march.comp
constexpr vkh::Format kOutputFormat = vkh::Format::eR8G8B8A8Unorm;
constexpr vkh::Format kVolumeFormat = vkh::Format::eR32G32Sfloat;

// Minimal host vector math for the camera basis (ADR-0012); no GLM dependency.
struct V3 {
    float x, y, z;
};
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
V3 normalize(V3 a) { return a * (1.0f / std::sqrt(dot(a, a))); }

// std140 uniform block; mirrors `Params` in shaders/ray_march.comp exactly.
struct Ubo {
    float eye[4];
    float topLeft[4];
    float horizontal[4];
    float vertical[4];
    float background[4];
    float range[4];           // minPositive, max, densityScale, alphaTermination
    std::uint32_t ints[4];    // width, height, stepCount, opacityMode
    std::uint32_t modes[4];   // colormapMode, pad, pad, pad
};
static_assert(sizeof(Ubo) == 128, "Ubo must match the std140 layout in ray_march.comp");

void writeV3(float (&dst)[4], V3 v) {
    dst[0] = v.x;
    dst[1] = v.y;
    dst[2] = v.z;
    dst[3] = 0.0f;
}

// Fill the std140 UBO from the camera / transfer-function / colormap params and the
// volume's magnitude range, for a width x height image (ADR-0012/0013/0014). Shared
// by the offscreen render() and the present-path recordFrame().
Ubo fillUbo(const RenderParams& params, std::uint32_t width, std::uint32_t height,
            MagnitudeRange range) {
    const V3 eye{params.eye[0], params.eye[1], params.eye[2]};
    const V3 target{params.target[0], params.target[1], params.target[2]};
    const V3 up{params.up[0], params.up[1], params.up[2]};
    const V3 w = normalize(eye - target);
    const V3 u = normalize(cross(up, w));
    const V3 v = cross(w, u);
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const float halfH = std::tan(params.vfovRadians * 0.5f);
    const float halfW = aspect * halfH;

    Ubo data{};
    writeV3(data.eye, eye);
    writeV3(data.topLeft, u * (-halfW) + v * halfH - w);
    writeV3(data.horizontal, u * (2.0f * halfW));
    writeV3(data.vertical, v * (2.0f * halfH));
    data.background[0] = params.background[0];
    data.background[1] = params.background[1];
    data.background[2] = params.background[2];
    data.background[3] = params.background[3];
    data.range[0] = range.minPositive;
    data.range[1] = range.max;
    data.range[2] = params.densityScale;
    data.range[3] = params.alphaTermination;
    data.ints[0] = width;
    data.ints[1] = height;
    data.ints[2] = params.stepCount;
    data.ints[3] = params.opacityMode;
    data.modes[0] = params.colormapMode;
    return data;
}

} // namespace

void Renderer::checkAffinity() const noexcept {
    IV_DEBUG_ASSERT(std::this_thread::get_id() == ownerThread_,
                    "Renderer used from a thread other than its owner (ADR-0007)");
}

Result<Renderer> Renderer::create(const Context& ctx) {
    const vkh::Device device = ctx.device();
    const vkh::PhysicalDevice phys = ctx.physicalDevice();

    Renderer r;
    r.device_ = device;
    r.physicalDevice_ = phys;
    r.queue_ = ctx.queue();
    r.commandPool_ = ctx.commandPool();
    r.ownerThread_ = std::this_thread::get_id();

    // R32G32_SFLOAT linear filtering is not core-mandatory (ADR-0009 note); fall
    // back to nearest if unsupported.
    const auto volFeatures = phys.getFormatProperties(kVolumeFormat).optimalTilingFeatures;
    r.volumeLinearFilter_ =
        static_cast<bool>(volFeatures & vkh::FormatFeatureFlagBits::eSampledImageFilterLinear);
    const vkh::Filter volFilter =
        r.volumeLinearFilter_ ? vkh::Filter::eLinear : vkh::Filter::eNearest;

    // --- Descriptor set layout (ADR-0011 binding model) ---
    const std::array<vkh::DescriptorSetLayoutBinding, 4> bindings{{
        {0u, vkh::DescriptorType::eCombinedImageSampler, 1u, vkh::ShaderStageFlagBits::eCompute},
        {1u, vkh::DescriptorType::eStorageImage, 1u, vkh::ShaderStageFlagBits::eCompute},
        {2u, vkh::DescriptorType::eUniformBuffer, 1u, vkh::ShaderStageFlagBits::eCompute},
        {3u, vkh::DescriptorType::eCombinedImageSampler, 1u, vkh::ShaderStageFlagBits::eCompute},
    }};
    {
        auto m = take(device.createDescriptorSetLayout(
                          vkh::DescriptorSetLayoutCreateInfo{}.setBindings(bindings)),
                      "createDescriptorSetLayout");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        r.setLayout_ = Unique<vkh::DescriptorSetLayout>(
            *m, [device](vkh::DescriptorSetLayout h) { device.destroyDescriptorSetLayout(h); });
    }
    {
        const vkh::DescriptorSetLayout sl = r.setLayout_.get();
        auto m = take(
            device.createPipelineLayout(vkh::PipelineLayoutCreateInfo{}.setSetLayouts(sl)),
            "createPipelineLayout");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        r.pipelineLayout_ = Unique<vkh::PipelineLayout>(
            *m, [device](vkh::PipelineLayout h) { device.destroyPipelineLayout(h); });
    }

    // --- Compute pipeline from embedded SPIR-V (ADR-0011) ---
    auto module = makeShaderModule(device, shaders::ray_march_comp_data,
                                   shaders::ray_march_comp_size);
    if (!module) {
        return std::unexpected(std::move(module).error());
    }
    {
        const auto stage = vkh::PipelineShaderStageCreateInfo{}
                               .setStage(vkh::ShaderStageFlagBits::eCompute)
                               .setModule(module->get())
                               .setPName("main");
        auto m = take(device.createComputePipeline(
                          {}, vkh::ComputePipelineCreateInfo{}.setStage(stage).setLayout(
                                  r.pipelineLayout_.get())),
                      "createComputePipeline");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        r.pipeline_ =
            Unique<vkh::Pipeline>(*m, [device](vkh::Pipeline h) { device.destroyPipeline(h); });
    }

    // --- Samplers (ADR-0011: volume linear/clamp; ADR-0014: colormap linear/repeat) ---
    {
        auto m = take(device.createSampler(vkh::SamplerCreateInfo{}
                                               .setMagFilter(volFilter)
                                               .setMinFilter(volFilter)
                                               .setAddressModeU(vkh::SamplerAddressMode::eClampToEdge)
                                               .setAddressModeV(vkh::SamplerAddressMode::eClampToEdge)
                                               .setAddressModeW(vkh::SamplerAddressMode::eClampToEdge)),
                      "createSampler(volume)");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        r.volumeSampler_ =
            Unique<vkh::Sampler>(*m, [device](vkh::Sampler h) { device.destroySampler(h); });
    }
    {
        auto m = take(device.createSampler(vkh::SamplerCreateInfo{}
                                               .setMagFilter(vkh::Filter::eLinear)
                                               .setMinFilter(vkh::Filter::eLinear)
                                               .setAddressModeU(vkh::SamplerAddressMode::eRepeat)
                                               .setAddressModeV(vkh::SamplerAddressMode::eRepeat)
                                               .setAddressModeW(vkh::SamplerAddressMode::eRepeat)),
                      "createSampler(colormap)");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        r.colormapSampler_ =
            Unique<vkh::Sampler>(*m, [device](vkh::Sampler h) { device.destroySampler(h); });
    }

    // --- Colormap LUT: 1D RGBA8, uploaded once (ADR-0014) ---
    {
        auto m = take(device.createImage(vkh::ImageCreateInfo{}
                                             .setImageType(vkh::ImageType::e1D)
                                             .setFormat(kOutputFormat)
                                             .setExtent(vkh::Extent3D{kColormapLutSize, 1u, 1u})
                                             .setMipLevels(1u)
                                             .setArrayLayers(1u)
                                             .setSamples(vkh::SampleCountFlagBits::e1)
                                             .setTiling(vkh::ImageTiling::eOptimal)
                                             .setUsage(vkh::ImageUsageFlagBits::eSampled
                                                       | vkh::ImageUsageFlagBits::eTransferDst)
                                             .setInitialLayout(vkh::ImageLayout::eUndefined)),
                      "createImage(colormap)");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        r.lutImage_ = Unique<vkh::Image>(*m, [device](vkh::Image h) { device.destroyImage(h); });
    }
    {
        auto mem = allocateAndBindImage(device, phys, r.lutImage_.get(),
                                        vkh::MemoryPropertyFlagBits::eDeviceLocal, "colormap LUT");
        if (!mem) {
            return std::unexpected(std::move(mem).error());
        }
        r.lutMemory_ = *std::move(mem);
    }
    {
        // Upload the LUT via a host-visible staging buffer.
        const vkh::DeviceSize lutBytes = sizeof(kTwilightLut);
        Unique<vkh::Buffer> staging;
        Unique<vkh::DeviceMemory> stagingMem;
        {
            auto m = take(device.createBuffer(vkh::BufferCreateInfo{}
                                                  .setSize(lutBytes)
                                                  .setUsage(vkh::BufferUsageFlagBits::eTransferSrc)
                                                  .setSharingMode(vkh::SharingMode::eExclusive)),
                          "createBuffer(colormap staging)");
            if (!m) {
                return std::unexpected(std::move(m).error());
            }
            staging = Unique<vkh::Buffer>(*m, [device](vkh::Buffer h) { device.destroyBuffer(h); });
        }
        {
            auto mem = allocateAndBindBuffer(device, phys, staging.get(),
                                             vkh::MemoryPropertyFlagBits::eHostVisible
                                                 | vkh::MemoryPropertyFlagBits::eHostCoherent,
                                             "colormap staging");
            if (!mem) {
                return std::unexpected(std::move(mem).error());
            }
            stagingMem = *std::move(mem);
        }
        {
            auto mapped = take(device.mapMemory(stagingMem.get(), 0, lutBytes), "mapMemory(lut)");
            if (!mapped) {
                return std::unexpected(std::move(mapped).error());
            }
            std::memcpy(*mapped, kTwilightLut, static_cast<std::size_t>(lutBytes));
            device.unmapMemory(stagingMem.get());
        }
        const vkh::Image lut = r.lutImage_.get();
        const vkh::Buffer buf = staging.get();
        const auto region = vkh::BufferImageCopy{}
                                .setBufferOffset(0)
                                .setBufferRowLength(0u)
                                .setBufferImageHeight(0u)
                                .setImageSubresource(vkh::ImageSubresourceLayers{
                                    vkh::ImageAspectFlagBits::eColor, 0u, 0u, 1u})
                                .setImageOffset(vkh::Offset3D{0, 0, 0})
                                .setImageExtent(vkh::Extent3D{kColormapLutSize, 1u, 1u});
        if (auto s = submitOneShot(
                device, r.queue_, r.commandPool_,
                [&](vkh::CommandBuffer cmd) {
                    cmd.pipelineBarrier(
                        vkh::PipelineStageFlagBits::eTopOfPipe,
                        vkh::PipelineStageFlagBits::eTransfer, vkh::DependencyFlags{}, nullptr,
                        nullptr,
                        imageBarrier(lut, vkh::ImageLayout::eUndefined,
                                     vkh::ImageLayout::eTransferDstOptimal,
                                     vkh::AccessFlagBits::eNone,
                                     vkh::AccessFlagBits::eTransferWrite));
                    cmd.copyBufferToImage(buf, lut, vkh::ImageLayout::eTransferDstOptimal, region);
                    cmd.pipelineBarrier(
                        vkh::PipelineStageFlagBits::eTransfer,
                        vkh::PipelineStageFlagBits::eFragmentShader, vkh::DependencyFlags{}, nullptr,
                        nullptr,
                        imageBarrier(lut, vkh::ImageLayout::eTransferDstOptimal,
                                     vkh::ImageLayout::eShaderReadOnlyOptimal,
                                     vkh::AccessFlagBits::eTransferWrite,
                                     vkh::AccessFlagBits::eShaderRead));
                });
            !s) {
            return std::unexpected(std::move(s).error());
        }
    }
    {
        auto m = take(device.createImageView(
                          vkh::ImageViewCreateInfo{}
                              .setImage(r.lutImage_.get())
                              .setViewType(vkh::ImageViewType::e1D)
                              .setFormat(kOutputFormat)
                              .setSubresourceRange(vkh::ImageSubresourceRange{
                                  vkh::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u})),
                      "createImageView(colormap)");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        r.lutView_ =
            Unique<vkh::ImageView>(*m, [device](vkh::ImageView h) { device.destroyImageView(h); });
    }

    // --- Descriptor pool (one set, reset each render) ---
    {
        const std::array<vkh::DescriptorPoolSize, 3> sizes{{
            {vkh::DescriptorType::eCombinedImageSampler, 2u},
            {vkh::DescriptorType::eStorageImage, 1u},
            {vkh::DescriptorType::eUniformBuffer, 1u},
        }};
        auto m = take(device.createDescriptorPool(
                          vkh::DescriptorPoolCreateInfo{}.setMaxSets(1u).setPoolSizes(sizes)),
                      "createDescriptorPool");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        r.descriptorPool_ = Unique<vkh::DescriptorPool>(
            *m, [device](vkh::DescriptorPool h) { device.destroyDescriptorPool(h); });
    }

    return r;
}

Result<ImageReadback> Renderer::render(const Volume& volume, std::uint32_t width,
                                       std::uint32_t height, const RenderParams& params) {
    checkAffinity();
    IV_ASSERT(width > 0u && height > 0u, "Renderer::render: extent must be non-zero");
    const vkh::Device device = device_;
    const vkh::DeviceSize outBytes = static_cast<vkh::DeviceSize>(width) * height * 4u;

    Unique<vkh::DeviceMemory> imageMem;
    Unique<vkh::Image> image;
    Unique<vkh::ImageView> view;
    Unique<vkh::DeviceMemory> uboMem;
    Unique<vkh::Buffer> ubo;
    Unique<vkh::DeviceMemory> stagingMem;
    Unique<vkh::Buffer> staging;

    // --- Output storage image (R8G8B8A8_UNORM) ---
    {
        auto m = take(device.createImage(vkh::ImageCreateInfo{}
                                             .setImageType(vkh::ImageType::e2D)
                                             .setFormat(kOutputFormat)
                                             .setExtent(vkh::Extent3D{width, height, 1u})
                                             .setMipLevels(1u)
                                             .setArrayLayers(1u)
                                             .setSamples(vkh::SampleCountFlagBits::e1)
                                             .setTiling(vkh::ImageTiling::eOptimal)
                                             .setUsage(vkh::ImageUsageFlagBits::eStorage
                                                       | vkh::ImageUsageFlagBits::eTransferSrc)
                                             .setInitialLayout(vkh::ImageLayout::eUndefined)),
                      "createImage(output)");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        image = Unique<vkh::Image>(*m, [device](vkh::Image h) { device.destroyImage(h); });
    }
    {
        auto mem = allocateAndBindImage(device, physicalDevice_, image.get(),
                                        vkh::MemoryPropertyFlagBits::eDeviceLocal, "render output");
        if (!mem) {
            return std::unexpected(std::move(mem).error());
        }
        imageMem = *std::move(mem);
    }
    {
        auto m = take(device.createImageView(
                          vkh::ImageViewCreateInfo{}
                              .setImage(image.get())
                              .setViewType(vkh::ImageViewType::e2D)
                              .setFormat(kOutputFormat)
                              .setSubresourceRange(vkh::ImageSubresourceRange{
                                  vkh::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u})),
                      "createImageView(output)");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        view = Unique<vkh::ImageView>(*m, [device](vkh::ImageView h) { device.destroyImageView(h); });
    }

    // --- Uniform buffer (host-visible), filled from params + volume range ---
    {
        auto m = take(device.createBuffer(vkh::BufferCreateInfo{}
                                              .setSize(sizeof(Ubo))
                                              .setUsage(vkh::BufferUsageFlagBits::eUniformBuffer)
                                              .setSharingMode(vkh::SharingMode::eExclusive)),
                      "createBuffer(ubo)");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        ubo = Unique<vkh::Buffer>(*m, [device](vkh::Buffer h) { device.destroyBuffer(h); });
    }
    {
        auto mem = allocateAndBindBuffer(device, physicalDevice_, ubo.get(),
                                         vkh::MemoryPropertyFlagBits::eHostVisible
                                             | vkh::MemoryPropertyFlagBits::eHostCoherent,
                                         "uniform buffer");
        if (!mem) {
            return std::unexpected(std::move(mem).error());
        }
        uboMem = *std::move(mem);
    }
    {
        const Ubo data = fillUbo(params, width, height, volume.magnitudeRange());
        auto mapped = take(device.mapMemory(uboMem.get(), 0, sizeof(Ubo)), "mapMemory(ubo)");
        if (!mapped) {
            return std::unexpected(std::move(mapped).error());
        }
        std::memcpy(*mapped, &data, sizeof(Ubo));
        device.unmapMemory(uboMem.get());
    }

    // --- Readback staging buffer ---
    {
        auto m = take(device.createBuffer(vkh::BufferCreateInfo{}
                                              .setSize(outBytes)
                                              .setUsage(vkh::BufferUsageFlagBits::eTransferDst)
                                              .setSharingMode(vkh::SharingMode::eExclusive)),
                      "createBuffer(render readback)");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        staging = Unique<vkh::Buffer>(*m, [device](vkh::Buffer h) { device.destroyBuffer(h); });
    }
    {
        auto mem = allocateAndBindBuffer(device, physicalDevice_, staging.get(),
                                         vkh::MemoryPropertyFlagBits::eHostVisible
                                             | vkh::MemoryPropertyFlagBits::eHostCoherent,
                                         "render readback");
        if (!mem) {
            return std::unexpected(std::move(mem).error());
        }
        stagingMem = *std::move(mem);
    }

    // --- Descriptor set (pool reset each render; resetDescriptorPool returns void) ---
    device.resetDescriptorPool(descriptorPool_.get());
    vkh::DescriptorSet set;
    {
        const vkh::DescriptorSetLayout sl = setLayout_.get();
        auto m = take(device.allocateDescriptorSets(vkh::DescriptorSetAllocateInfo{}
                                                        .setDescriptorPool(descriptorPool_.get())
                                                        .setSetLayouts(sl)),
                      "allocateDescriptorSets");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        set = (*m).front();
    }
    writeComputeDescriptors(set, volume.view(), view.get(), ubo.get());

    // --- Record: storage -> general, dispatch, -> transferSrc, copy to staging ---
    const vkh::Image img = image.get();
    const vkh::Buffer buf = staging.get();
    const std::uint32_t gx = (width + kLocalSize - 1u) / kLocalSize;
    const std::uint32_t gy = (height + kLocalSize - 1u) / kLocalSize;
    const auto region = vkh::BufferImageCopy{}
                            .setBufferOffset(0)
                            .setBufferRowLength(0u)
                            .setBufferImageHeight(0u)
                            .setImageSubresource(
                                vkh::ImageSubresourceLayers{vkh::ImageAspectFlagBits::eColor, 0u,
                                                            0u, 1u})
                            .setImageOffset(vkh::Offset3D{0, 0, 0})
                            .setImageExtent(vkh::Extent3D{width, height, 1u});
    if (auto s = submitOneShot(
            device, queue_, commandPool_,
            [&](vkh::CommandBuffer cmd) {
                cmd.pipelineBarrier(
                    vkh::PipelineStageFlagBits::eTopOfPipe,
                    vkh::PipelineStageFlagBits::eComputeShader, vkh::DependencyFlags{}, nullptr,
                    nullptr,
                    imageBarrier(img, vkh::ImageLayout::eUndefined, vkh::ImageLayout::eGeneral,
                                 vkh::AccessFlagBits::eNone, vkh::AccessFlagBits::eShaderWrite));
                cmd.bindPipeline(vkh::PipelineBindPoint::eCompute, pipeline_.get());
                cmd.bindDescriptorSets(vkh::PipelineBindPoint::eCompute, pipelineLayout_.get(), 0u,
                                       set, nullptr);
                cmd.dispatch(gx, gy, 1u);
                cmd.pipelineBarrier(
                    vkh::PipelineStageFlagBits::eComputeShader,
                    vkh::PipelineStageFlagBits::eTransfer, vkh::DependencyFlags{}, nullptr, nullptr,
                    imageBarrier(img, vkh::ImageLayout::eGeneral,
                                 vkh::ImageLayout::eTransferSrcOptimal,
                                 vkh::AccessFlagBits::eShaderWrite,
                                 vkh::AccessFlagBits::eTransferRead));
                cmd.copyImageToBuffer(img, vkh::ImageLayout::eTransferSrcOptimal, buf, region);
            });
        !s) {
        return std::unexpected(std::move(s).error());
    }

    // --- Map and copy out (host-coherent: no invalidate needed) ---
    auto mapped = take(device.mapMemory(stagingMem.get(), 0, outBytes), "mapMemory(render)");
    if (!mapped) {
        return std::unexpected(std::move(mapped).error());
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(outBytes));
    std::memcpy(bytes.data(), *mapped, static_cast<std::size_t>(outBytes));
    device.unmapMemory(stagingMem.get());

    return ImageReadback(width, height, std::move(bytes));
}

void Renderer::writeComputeDescriptors(vkh::DescriptorSet set, vkh::ImageView volumeView,
                                       vkh::ImageView storageView, vkh::Buffer ubo) {
    const auto volInfo = vkh::DescriptorImageInfo{}
                             .setSampler(volumeSampler_.get())
                             .setImageView(volumeView)
                             .setImageLayout(vkh::ImageLayout::eShaderReadOnlyOptimal);
    const auto outInfo =
        vkh::DescriptorImageInfo{}.setImageView(storageView).setImageLayout(vkh::ImageLayout::eGeneral);
    const auto uboInfo =
        vkh::DescriptorBufferInfo{}.setBuffer(ubo).setOffset(0).setRange(sizeof(Ubo));
    const auto lutInfo = vkh::DescriptorImageInfo{}
                             .setSampler(colormapSampler_.get())
                             .setImageView(lutView_.get())
                             .setImageLayout(vkh::ImageLayout::eShaderReadOnlyOptimal);
    const std::array<vkh::WriteDescriptorSet, 4> writes{{
        vkh::WriteDescriptorSet{}
            .setDstSet(set)
            .setDstBinding(0u)
            .setDescriptorType(vkh::DescriptorType::eCombinedImageSampler)
            .setImageInfo(volInfo),
        vkh::WriteDescriptorSet{}
            .setDstSet(set)
            .setDstBinding(1u)
            .setDescriptorType(vkh::DescriptorType::eStorageImage)
            .setImageInfo(outInfo),
        vkh::WriteDescriptorSet{}
            .setDstSet(set)
            .setDstBinding(2u)
            .setDescriptorType(vkh::DescriptorType::eUniformBuffer)
            .setBufferInfo(uboInfo),
        vkh::WriteDescriptorSet{}
            .setDstSet(set)
            .setDstBinding(3u)
            .setDescriptorType(vkh::DescriptorType::eCombinedImageSampler)
            .setImageInfo(lutInfo),
    }};
    device_.updateDescriptorSets(writes, nullptr);
}

Status Renderer::ensureFrameResources(const Volume& volume, vkh::Extent2D extent) {
    // Nothing relevant changed since the last frame: reuse everything (ADR-0017).
    if (frameImage_ && frameExtent_ == extent && frameVolumeView_ == volume.view()) {
        return {};
    }
    const vkh::Device device = device_;

    // (Re)create the storage image + view at the new extent. Reset the old view
    // before the old image is destroyed (move-assign), and free the old memory only
    // after its image is gone (member order makes the latter automatic).
    {
        auto m = take(device.createImage(
                          vkh::ImageCreateInfo{}
                              .setImageType(vkh::ImageType::e2D)
                              .setFormat(kOutputFormat)
                              .setExtent(vkh::Extent3D{extent.width, extent.height, 1u})
                              .setMipLevels(1u)
                              .setArrayLayers(1u)
                              .setSamples(vkh::SampleCountFlagBits::e1)
                              .setTiling(vkh::ImageTiling::eOptimal)
                              .setUsage(vkh::ImageUsageFlagBits::eStorage
                                        | vkh::ImageUsageFlagBits::eTransferSrc)
                              .setInitialLayout(vkh::ImageLayout::eUndefined)),
                      "createImage(frame)");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        frameView_.reset();
        frameImage_ = Unique<vkh::Image>(*m, [device](vkh::Image h) { device.destroyImage(h); });
    }
    {
        auto mem = allocateAndBindImage(device, physicalDevice_, frameImage_.get(),
                                        vkh::MemoryPropertyFlagBits::eDeviceLocal, "frame storage");
        if (!mem) {
            return std::unexpected(std::move(mem).error());
        }
        frameImageMem_ = *std::move(mem);
    }
    {
        auto m = take(device.createImageView(
                          vkh::ImageViewCreateInfo{}
                              .setImage(frameImage_.get())
                              .setViewType(vkh::ImageViewType::e2D)
                              .setFormat(kOutputFormat)
                              .setSubresourceRange(vkh::ImageSubresourceRange{
                                  vkh::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u})),
                      "createImageView(frame)");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        frameView_ =
            Unique<vkh::ImageView>(*m, [device](vkh::ImageView h) { device.destroyImageView(h); });
    }

    // The UBO is allocated once and persistently mapped (host-coherent). recordFrame
    // memcpys into frameUboMapped_ each frame; no per-frame map/unmap.
    if (!frameUbo_) {
        {
            auto m = take(device.createBuffer(vkh::BufferCreateInfo{}
                                                  .setSize(sizeof(Ubo))
                                                  .setUsage(vkh::BufferUsageFlagBits::eUniformBuffer)
                                                  .setSharingMode(vkh::SharingMode::eExclusive)),
                          "createBuffer(frame ubo)");
            if (!m) {
                return std::unexpected(std::move(m).error());
            }
            frameUbo_ =
                Unique<vkh::Buffer>(*m, [device](vkh::Buffer h) { device.destroyBuffer(h); });
        }
        {
            auto mem = allocateAndBindBuffer(device, physicalDevice_, frameUbo_.get(),
                                             vkh::MemoryPropertyFlagBits::eHostVisible
                                                 | vkh::MemoryPropertyFlagBits::eHostCoherent,
                                             "frame uniform buffer");
            if (!mem) {
                return std::unexpected(std::move(mem).error());
            }
            frameUboMem_ = *std::move(mem);
        }
        {
            auto mapped =
                take(device.mapMemory(frameUboMem_.get(), 0, sizeof(Ubo)), "mapMemory(frame ubo)");
            if (!mapped) {
                return std::unexpected(std::move(mapped).error());
            }
            frameUboMapped_ = *mapped; // persistently mapped; freed (implicitly unmapped) at teardown
        }
    }

    // One descriptor set, allocated once; its bindings are rewritten below whenever
    // the storage view (or volume) changes.
    if (!frameDescriptorPool_) {
        const std::array<vkh::DescriptorPoolSize, 3> sizes{{
            {vkh::DescriptorType::eCombinedImageSampler, 2u},
            {vkh::DescriptorType::eStorageImage, 1u},
            {vkh::DescriptorType::eUniformBuffer, 1u},
        }};
        {
            auto m = take(device.createDescriptorPool(
                              vkh::DescriptorPoolCreateInfo{}.setMaxSets(1u).setPoolSizes(sizes)),
                          "createDescriptorPool(frame)");
            if (!m) {
                return std::unexpected(std::move(m).error());
            }
            frameDescriptorPool_ = Unique<vkh::DescriptorPool>(
                *m, [device](vkh::DescriptorPool h) { device.destroyDescriptorPool(h); });
        }
        const vkh::DescriptorSetLayout sl = setLayout_.get();
        auto m = take(device.allocateDescriptorSets(vkh::DescriptorSetAllocateInfo{}
                                                        .setDescriptorPool(frameDescriptorPool_.get())
                                                        .setSetLayouts(sl)),
                      "allocateDescriptorSets(frame)");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        frameSet_ = (*m).front();
    }

    writeComputeDescriptors(frameSet_, volume.view(), frameView_.get(), frameUbo_.get());
    frameExtent_ = extent;
    frameVolumeView_ = volume.view();
    return {};
}

Status Renderer::recordFrame(vkh::CommandBuffer cmd, const Volume& volume,
                             const RenderParams& params, vkh::Image dstImage,
                             vkh::Extent2D dstExtent) {
    checkAffinity();
    IV_ASSERT(dstExtent.width > 0u && dstExtent.height > 0u,
              "Renderer::recordFrame: extent must be non-zero");
    if (auto s = ensureFrameResources(volume, dstExtent); !s) {
        return std::unexpected(std::move(s).error());
    }

    // Update the UBO in place (persistently mapped, host-coherent — no flush needed).
    const Ubo data = fillUbo(params, dstExtent.width, dstExtent.height, volume.magnitudeRange());
    std::memcpy(frameUboMapped_, &data, sizeof(Ubo));

    const vkh::Image storage = frameImage_.get();
    const std::uint32_t gx = (dstExtent.width + kLocalSize - 1u) / kLocalSize;
    const std::uint32_t gy = (dstExtent.height + kLocalSize - 1u) / kLocalSize;

    // storage: undefined -> general (compute writes the whole image, so prior
    // contents are discarded).
    cmd.pipelineBarrier(
        vkh::PipelineStageFlagBits::eTopOfPipe, vkh::PipelineStageFlagBits::eComputeShader,
        vkh::DependencyFlags{}, nullptr, nullptr,
        imageBarrier(storage, vkh::ImageLayout::eUndefined, vkh::ImageLayout::eGeneral,
                     vkh::AccessFlagBits::eNone, vkh::AccessFlagBits::eShaderWrite));
    cmd.bindPipeline(vkh::PipelineBindPoint::eCompute, pipeline_.get());
    cmd.bindDescriptorSets(vkh::PipelineBindPoint::eCompute, pipelineLayout_.get(), 0u, frameSet_,
                           nullptr);
    cmd.dispatch(gx, gy, 1u);
    // storage: general -> transferSrc for the blit.
    cmd.pipelineBarrier(
        vkh::PipelineStageFlagBits::eComputeShader, vkh::PipelineStageFlagBits::eTransfer,
        vkh::DependencyFlags{}, nullptr, nullptr,
        imageBarrier(storage, vkh::ImageLayout::eGeneral, vkh::ImageLayout::eTransferSrcOptimal,
                     vkh::AccessFlagBits::eShaderWrite, vkh::AccessFlagBits::eTransferRead));
    // dst: undefined -> transferDst (its prior contents are discarded by contract).
    cmd.pipelineBarrier(
        vkh::PipelineStageFlagBits::eTopOfPipe, vkh::PipelineStageFlagBits::eTransfer,
        vkh::DependencyFlags{}, nullptr, nullptr,
        imageBarrier(dstImage, vkh::ImageLayout::eUndefined, vkh::ImageLayout::eTransferDstOptimal,
                     vkh::AccessFlagBits::eNone, vkh::AccessFlagBits::eTransferWrite));

    // Blit storage -> dst at 1:1. Blit (not copy) is component-aware, so it converts
    // R8G8B8A8 -> a BGRA swapchain format with correct colors; nearest since the
    // extents are identical.
    const std::array<vkh::Offset3D, 2> bounds{
        vkh::Offset3D{0, 0, 0},
        vkh::Offset3D{static_cast<std::int32_t>(dstExtent.width),
                      static_cast<std::int32_t>(dstExtent.height), 1}};
    const auto blit = vkh::ImageBlit{}
                          .setSrcSubresource(vkh::ImageSubresourceLayers{
                              vkh::ImageAspectFlagBits::eColor, 0u, 0u, 1u})
                          .setSrcOffsets(bounds)
                          .setDstSubresource(vkh::ImageSubresourceLayers{
                              vkh::ImageAspectFlagBits::eColor, 0u, 0u, 1u})
                          .setDstOffsets(bounds);
    cmd.blitImage(storage, vkh::ImageLayout::eTransferSrcOptimal, dstImage,
                  vkh::ImageLayout::eTransferDstOptimal, blit, vkh::Filter::eNearest);
    // Leave dst in eTransferDstOptimal; the caller transitions it to ePresentSrcKHR.
    return {};
}

} // namespace iv::vk
