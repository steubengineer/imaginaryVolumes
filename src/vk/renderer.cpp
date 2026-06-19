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

static_assert(sizeof(OverlayVertex) == 7 * sizeof(float),
              "OverlayVertex must be tightly packed (vec3 pos + vec4 color = 28 bytes)");

// Host-visible vertex buffer holding the overlay's line vertices followed by its
// triangle vertices (ADR-0021): lines at offset 0, triangles at
// lines.size()*sizeof(OverlayVertex). Precondition: !ov.empty().
struct OverlayBuffer {
    Unique<vkh::Buffer> buffer;
    Unique<vkh::DeviceMemory> memory;
};
Result<OverlayBuffer> makeOverlayBuffer(vkh::Device device, vkh::PhysicalDevice phys,
                                        const Overlay& ov) {
    const vkh::DeviceSize bytes =
        static_cast<vkh::DeviceSize>(ov.lines.size() + ov.triangles.size()) * sizeof(OverlayVertex);
    OverlayBuffer out;
    {
        auto m = take(device.createBuffer(vkh::BufferCreateInfo{}
                                              .setSize(bytes)
                                              .setUsage(vkh::BufferUsageFlagBits::eVertexBuffer)
                                              .setSharingMode(vkh::SharingMode::eExclusive)),
                      "createBuffer(overlay)");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        out.buffer = Unique<vkh::Buffer>(*m, [device](vkh::Buffer h) { device.destroyBuffer(h); });
    }
    {
        auto mem = allocateAndBindBuffer(device, phys, out.buffer.get(),
                                         vkh::MemoryPropertyFlagBits::eHostVisible
                                             | vkh::MemoryPropertyFlagBits::eHostCoherent,
                                         "overlay vertices");
        if (!mem) {
            return std::unexpected(std::move(mem).error());
        }
        out.memory = *std::move(mem);
    }
    {
        auto mapped = take(device.mapMemory(out.memory.get(), 0, bytes), "mapMemory(overlay)");
        if (!mapped) {
            return std::unexpected(std::move(mapped).error());
        }
        auto* dst = static_cast<unsigned char*>(*mapped);
        const std::size_t lineBytes = ov.lines.size() * sizeof(OverlayVertex);
        if (!ov.lines.empty()) {
            std::memcpy(dst, ov.lines.data(), lineBytes);
        }
        if (!ov.triangles.empty()) {
            std::memcpy(dst + lineBytes, ov.triangles.data(),
                        ov.triangles.size() * sizeof(OverlayVertex));
        }
        device.unmapMemory(out.memory.get());
    }
    return out;
}

} // namespace

void Renderer::checkAffinity() const noexcept {
    IV_DEBUG_ASSERT(std::this_thread::get_id() == ownerThread_,
                    "Renderer used from a thread other than its owner (ADR-0007)");
}

Result<Renderer> Renderer::create(const Context& ctx) {
    const vkh::Device device = ctx.device();
    const vkh::PhysicalDevice phys = ctx.physicalDevice();
    const bool smoothLines = ctx.smoothLinesAvailable(); // anti-aliased overlay lines (ADR-0026)

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

    // --- Overlay graphics pipeline (ADR-0021): render pass + line/triangle pipelines ---
    {
        const auto color = vkh::AttachmentDescription{}
                               .setFormat(kOutputFormat)
                               .setSamples(vkh::SampleCountFlagBits::e1)
                               .setLoadOp(vkh::AttachmentLoadOp::eLoad) // preserve the volume render
                               .setStoreOp(vkh::AttachmentStoreOp::eStore)
                               .setStencilLoadOp(vkh::AttachmentLoadOp::eDontCare)
                               .setStencilStoreOp(vkh::AttachmentStoreOp::eDontCare)
                               .setInitialLayout(vkh::ImageLayout::eColorAttachmentOptimal)
                               .setFinalLayout(vkh::ImageLayout::eColorAttachmentOptimal);
        const auto ref = vkh::AttachmentReference{}.setAttachment(0u).setLayout(
            vkh::ImageLayout::eColorAttachmentOptimal);
        const auto sub = vkh::SubpassDescription{}
                             .setPipelineBindPoint(vkh::PipelineBindPoint::eGraphics)
                             .setColorAttachments(ref);
        const auto dep = vkh::SubpassDependency{}
                             .setSrcSubpass(VK_SUBPASS_EXTERNAL)
                             .setDstSubpass(0u)
                             .setSrcStageMask(vkh::PipelineStageFlagBits::eColorAttachmentOutput)
                             .setDstStageMask(vkh::PipelineStageFlagBits::eColorAttachmentOutput)
                             .setSrcAccessMask(vkh::AccessFlagBits::eColorAttachmentWrite)
                             .setDstAccessMask(vkh::AccessFlagBits::eColorAttachmentWrite
                                               | vkh::AccessFlagBits::eColorAttachmentRead);
        auto m = take(device.createRenderPass(vkh::RenderPassCreateInfo{}
                                                  .setAttachments(color)
                                                  .setSubpasses(sub)
                                                  .setDependencies(dep)),
                      "createRenderPass");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        r.renderPass_ = Unique<vkh::RenderPass>(
            *m, [device](vkh::RenderPass h) { device.destroyRenderPass(h); });
    }
    {
        const auto pcRange = vkh::PushConstantRange{}
                                 .setStageFlags(vkh::ShaderStageFlagBits::eVertex)
                                 .setOffset(0u)
                                 .setSize(static_cast<std::uint32_t>(sizeof(float) * 16u));
        auto m = take(device.createPipelineLayout(
                          vkh::PipelineLayoutCreateInfo{}.setPushConstantRanges(pcRange)),
                      "createPipelineLayout(overlay)");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        r.overlayPipelineLayout_ = Unique<vkh::PipelineLayout>(
            *m, [device](vkh::PipelineLayout h) { device.destroyPipelineLayout(h); });
    }
    {
        auto vert = makeShaderModule(device, shaders::overlay_vert_data, shaders::overlay_vert_size);
        if (!vert) {
            return std::unexpected(std::move(vert).error());
        }
        auto frag = makeShaderModule(device, shaders::overlay_frag_data, shaders::overlay_frag_size);
        if (!frag) {
            return std::unexpected(std::move(frag).error());
        }
        const std::array<vkh::PipelineShaderStageCreateInfo, 2> stages{{
            vkh::PipelineShaderStageCreateInfo{}
                .setStage(vkh::ShaderStageFlagBits::eVertex)
                .setModule(vert->get())
                .setPName("main"),
            vkh::PipelineShaderStageCreateInfo{}
                .setStage(vkh::ShaderStageFlagBits::eFragment)
                .setModule(frag->get())
                .setPName("main"),
        }};
        const auto binding = vkh::VertexInputBindingDescription{}
                                 .setBinding(0u)
                                 .setStride(static_cast<std::uint32_t>(sizeof(OverlayVertex)))
                                 .setInputRate(vkh::VertexInputRate::eVertex);
        const std::array<vkh::VertexInputAttributeDescription, 2> attrs{{
            vkh::VertexInputAttributeDescription{}.setLocation(0u).setBinding(0u).setFormat(
                vkh::Format::eR32G32B32Sfloat).setOffset(0u),
            vkh::VertexInputAttributeDescription{}.setLocation(1u).setBinding(0u).setFormat(
                vkh::Format::eR32G32B32A32Sfloat)
                .setOffset(static_cast<std::uint32_t>(sizeof(float) * 3u)),
        }};
        const auto vinput = vkh::PipelineVertexInputStateCreateInfo{}
                                .setVertexBindingDescriptions(binding)
                                .setVertexAttributeDescriptions(attrs);
        const auto vp =
            vkh::PipelineViewportStateCreateInfo{}.setViewportCount(1u).setScissorCount(1u);
        const auto rs = vkh::PipelineRasterizationStateCreateInfo{}
                            .setPolygonMode(vkh::PolygonMode::eFill)
                            .setCullMode(vkh::CullModeFlagBits::eNone)
                            .setFrontFace(vkh::FrontFace::eCounterClockwise)
                            .setLineWidth(1.0f);
        const auto ms = vkh::PipelineMultisampleStateCreateInfo{}.setRasterizationSamples(
            vkh::SampleCountFlagBits::e1);
        const auto blendAttach = vkh::PipelineColorBlendAttachmentState{}
                                     .setBlendEnable(VK_TRUE)
                                     .setSrcColorBlendFactor(vkh::BlendFactor::eSrcAlpha)
                                     .setDstColorBlendFactor(vkh::BlendFactor::eOneMinusSrcAlpha)
                                     .setColorBlendOp(vkh::BlendOp::eAdd)
                                     .setSrcAlphaBlendFactor(vkh::BlendFactor::eOne)
                                     .setDstAlphaBlendFactor(vkh::BlendFactor::eOneMinusSrcAlpha)
                                     .setAlphaBlendOp(vkh::BlendOp::eAdd)
                                     .setColorWriteMask(vkh::ColorComponentFlagBits::eR
                                                        | vkh::ColorComponentFlagBits::eG
                                                        | vkh::ColorComponentFlagBits::eB
                                                        | vkh::ColorComponentFlagBits::eA);
        const auto cb = vkh::PipelineColorBlendStateCreateInfo{}.setAttachments(blendAttach);
        const std::array<vkh::DynamicState, 2> dynStates{vkh::DynamicState::eViewport,
                                                         vkh::DynamicState::eScissor};
        const auto dyn = vkh::PipelineDynamicStateCreateInfo{}.setDynamicStates(dynStates);

        // The line pipeline uses anti-aliased (smooth) rasterization when the device
        // supports it (ADR-0026); triangles stay default. Smooth lines emit coverage as
        // alpha, composited by the existing alpha blend.
        const auto lineState = vkh::PipelineRasterizationLineStateCreateInfoEXT{}.setLineRasterizationMode(
            vkh::LineRasterizationModeEXT::eRectangularSmooth);
        auto rsLine = rs;
        if (smoothLines) {
            rsLine.setPNext(&lineState);
        }

        const std::array<vkh::PrimitiveTopology, 2> topos{vkh::PrimitiveTopology::eLineList,
                                                         vkh::PrimitiveTopology::eTriangleList};
        const std::array<Unique<vkh::Pipeline>*, 2> targets{&r.overlayLinePipeline_,
                                                           &r.overlayTrianglePipeline_};
        for (std::size_t i = 0; i < topos.size(); ++i) {
            const auto ia = vkh::PipelineInputAssemblyStateCreateInfo{}.setTopology(topos[i]);
            const vkh::PipelineRasterizationStateCreateInfo* rasterState =
                (i == 0) ? &rsLine : &rs; // i==0 is the line list
            auto pipe = take(device.createGraphicsPipeline(
                                 {}, vkh::GraphicsPipelineCreateInfo{}
                                         .setStages(stages)
                                         .setPVertexInputState(&vinput)
                                         .setPInputAssemblyState(&ia)
                                         .setPViewportState(&vp)
                                         .setPRasterizationState(rasterState)
                                         .setPMultisampleState(&ms)
                                         .setPColorBlendState(&cb)
                                         .setPDynamicState(&dyn)
                                         .setLayout(r.overlayPipelineLayout_.get())
                                         .setRenderPass(r.renderPass_.get())
                                         .setSubpass(0u)),
                             "createGraphicsPipeline(overlay)");
            if (!pipe) {
                return std::unexpected(std::move(pipe).error());
            }
            *targets[i] = Unique<vkh::Pipeline>(
                *pipe, [device](vkh::Pipeline h) { device.destroyPipeline(h); });
        }
    }

    // --- Slug glyph pipeline (ADR-0023): shares the overlay render pass ---
    {
        const auto binding = vkh::DescriptorSetLayoutBinding{}
                                 .setBinding(0u)
                                 .setDescriptorType(vkh::DescriptorType::eUniformTexelBuffer)
                                 .setDescriptorCount(1u)
                                 .setStageFlags(vkh::ShaderStageFlagBits::eFragment);
        auto sl = take(device.createDescriptorSetLayout(
                           vkh::DescriptorSetLayoutCreateInfo{}.setBindings(binding)),
                       "createDescriptorSetLayout(glyph)");
        if (!sl) {
            return std::unexpected(std::move(sl).error());
        }
        r.glyphSetLayout_ = Unique<vkh::DescriptorSetLayout>(
            *sl, [device](vkh::DescriptorSetLayout h) { device.destroyDescriptorSetLayout(h); });

        const vkh::DescriptorSetLayout setLayouts = r.glyphSetLayout_.get();
        auto pl = take(device.createPipelineLayout(
                           vkh::PipelineLayoutCreateInfo{}.setSetLayouts(setLayouts)),
                       "createPipelineLayout(glyph)");
        if (!pl) {
            return std::unexpected(std::move(pl).error());
        }
        r.glyphPipelineLayout_ = Unique<vkh::PipelineLayout>(
            *pl, [device](vkh::PipelineLayout h) { device.destroyPipelineLayout(h); });

        auto vert = makeShaderModule(device, shaders::glyph_vert_data, shaders::glyph_vert_size);
        if (!vert) {
            return std::unexpected(std::move(vert).error());
        }
        auto frag = makeShaderModule(device, shaders::glyph_frag_data, shaders::glyph_frag_size);
        if (!frag) {
            return std::unexpected(std::move(frag).error());
        }
        const std::array<vkh::PipelineShaderStageCreateInfo, 2> stages{{
            vkh::PipelineShaderStageCreateInfo{}
                .setStage(vkh::ShaderStageFlagBits::eVertex)
                .setModule(vert->get())
                .setPName("main"),
            vkh::PipelineShaderStageCreateInfo{}
                .setStage(vkh::ShaderStageFlagBits::eFragment)
                .setModule(frag->get())
                .setPName("main"),
        }};
        const auto vbind = vkh::VertexInputBindingDescription{}
                               .setBinding(0u)
                               .setStride(static_cast<std::uint32_t>(sizeof(GlyphVertex)))
                               .setInputRate(vkh::VertexInputRate::eVertex);
        const std::array<vkh::VertexInputAttributeDescription, 4> attrs{{
            vkh::VertexInputAttributeDescription{}.setLocation(0u).setBinding(0u).setFormat(
                vkh::Format::eR32G32Sfloat).setOffset(
                static_cast<std::uint32_t>(offsetof(GlyphVertex, pos))),
            vkh::VertexInputAttributeDescription{}.setLocation(1u).setBinding(0u).setFormat(
                vkh::Format::eR32G32Sfloat).setOffset(
                static_cast<std::uint32_t>(offsetof(GlyphVertex, texcoord))),
            vkh::VertexInputAttributeDescription{}.setLocation(2u).setBinding(0u).setFormat(
                vkh::Format::eR32Uint).setOffset(
                static_cast<std::uint32_t>(offsetof(GlyphVertex, glyphLoc))),
            vkh::VertexInputAttributeDescription{}.setLocation(3u).setBinding(0u).setFormat(
                vkh::Format::eR32G32B32A32Sfloat).setOffset(
                static_cast<std::uint32_t>(offsetof(GlyphVertex, color))),
        }};
        const auto vinput = vkh::PipelineVertexInputStateCreateInfo{}
                                .setVertexBindingDescriptions(vbind)
                                .setVertexAttributeDescriptions(attrs);
        const auto ia = vkh::PipelineInputAssemblyStateCreateInfo{}.setTopology(
            vkh::PrimitiveTopology::eTriangleList);
        const auto vp =
            vkh::PipelineViewportStateCreateInfo{}.setViewportCount(1u).setScissorCount(1u);
        const auto rs = vkh::PipelineRasterizationStateCreateInfo{}
                            .setPolygonMode(vkh::PolygonMode::eFill)
                            .setCullMode(vkh::CullModeFlagBits::eNone)
                            .setFrontFace(vkh::FrontFace::eCounterClockwise)
                            .setLineWidth(1.0f);
        const auto ms = vkh::PipelineMultisampleStateCreateInfo{}.setRasterizationSamples(
            vkh::SampleCountFlagBits::e1);
        const auto blendAttach = vkh::PipelineColorBlendAttachmentState{}
                                     .setBlendEnable(VK_TRUE)
                                     .setSrcColorBlendFactor(vkh::BlendFactor::eSrcAlpha)
                                     .setDstColorBlendFactor(vkh::BlendFactor::eOneMinusSrcAlpha)
                                     .setColorBlendOp(vkh::BlendOp::eAdd)
                                     .setSrcAlphaBlendFactor(vkh::BlendFactor::eOne)
                                     .setDstAlphaBlendFactor(vkh::BlendFactor::eOneMinusSrcAlpha)
                                     .setAlphaBlendOp(vkh::BlendOp::eAdd)
                                     .setColorWriteMask(vkh::ColorComponentFlagBits::eR
                                                        | vkh::ColorComponentFlagBits::eG
                                                        | vkh::ColorComponentFlagBits::eB
                                                        | vkh::ColorComponentFlagBits::eA);
        const auto cb = vkh::PipelineColorBlendStateCreateInfo{}.setAttachments(blendAttach);
        const std::array<vkh::DynamicState, 2> dynStates{vkh::DynamicState::eViewport,
                                                         vkh::DynamicState::eScissor};
        const auto dyn = vkh::PipelineDynamicStateCreateInfo{}.setDynamicStates(dynStates);
        auto pipe = take(device.createGraphicsPipeline(
                             {}, vkh::GraphicsPipelineCreateInfo{}
                                     .setStages(stages)
                                     .setPVertexInputState(&vinput)
                                     .setPInputAssemblyState(&ia)
                                     .setPViewportState(&vp)
                                     .setPRasterizationState(&rs)
                                     .setPMultisampleState(&ms)
                                     .setPColorBlendState(&cb)
                                     .setPDynamicState(&dyn)
                                     .setLayout(r.glyphPipelineLayout_.get())
                                     .setRenderPass(r.renderPass_.get())
                                     .setSubpass(0u)),
                         "createGraphicsPipeline(glyph)");
        if (!pipe) {
            return std::unexpected(std::move(pipe).error());
        }
        r.glyphPipeline_ =
            Unique<vkh::Pipeline>(*pipe, [device](vkh::Pipeline h) { device.destroyPipeline(h); });
    }

    return r;
}

Result<ImageReadback> Renderer::render(const Volume& volume, std::uint32_t width,
                                       std::uint32_t height, const RenderParams& params,
                                       const Overlay* overlay) {
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
                                                       | vkh::ImageUsageFlagBits::eTransferSrc
                                                       | vkh::ImageUsageFlagBits::eColorAttachment)
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

    // --- Overlay resources (ADR-0021): created only when an overlay is supplied ---
    const bool hasOverlay = overlay != nullptr && !overlay->empty();
    Unique<vkh::Buffer> overlayBuf;
    Unique<vkh::DeviceMemory> overlayMem;
    Unique<vkh::Framebuffer> overlayFb;
    GlyphResources glyphRes;
    std::uint32_t overlayLineVerts = 0u;
    std::uint32_t overlayTriVerts = 0u;
    if (hasOverlay) {
        // Line/triangle vertex buffer — only when there is line/triangle geometry
        // (a glyphs-only overlay must not create a zero-size buffer).
        if (!overlay->lines.empty() || !overlay->triangles.empty()) {
            auto ob = makeOverlayBuffer(device, physicalDevice_, *overlay);
            if (!ob) {
                return std::unexpected(std::move(ob).error());
            }
            overlayBuf = std::move(ob->buffer);
            overlayMem = std::move(ob->memory);
            overlayLineVerts = static_cast<std::uint32_t>(overlay->lines.size());
            overlayTriVerts = static_cast<std::uint32_t>(overlay->triangles.size());
        }
        auto gr = buildGlyphResources(overlay->glyphs,
                                      std::span<const std::int16_t>(overlay->glyphAtlas));
        if (!gr) {
            return std::unexpected(std::move(gr).error());
        }
        glyphRes = *std::move(gr);
        const vkh::ImageView attach = view.get();
        auto fb = take(device.createFramebuffer(vkh::FramebufferCreateInfo{}
                                                    .setRenderPass(renderPass_.get())
                                                    .setAttachments(attach)
                                                    .setWidth(width)
                                                    .setHeight(height)
                                                    .setLayers(1u)),
                       "createFramebuffer(render)");
        if (!fb) {
            return std::unexpected(std::move(fb).error());
        }
        overlayFb = Unique<vkh::Framebuffer>(
            *fb, [device](vkh::Framebuffer h) { device.destroyFramebuffer(h); });
    }

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
                if (hasOverlay) {
                    // general -> color attachment, draw the overlay, -> transfer src.
                    cmd.pipelineBarrier(
                        vkh::PipelineStageFlagBits::eComputeShader,
                        vkh::PipelineStageFlagBits::eColorAttachmentOutput, vkh::DependencyFlags{},
                        nullptr, nullptr,
                        imageBarrier(img, vkh::ImageLayout::eGeneral,
                                     vkh::ImageLayout::eColorAttachmentOptimal,
                                     vkh::AccessFlagBits::eShaderWrite,
                                     vkh::AccessFlagBits::eColorAttachmentWrite
                                         | vkh::AccessFlagBits::eColorAttachmentRead));
                    drawOverlay(cmd, overlayFb.get(), vkh::Extent2D{width, height},
                                overlayBuf.get(), overlayLineVerts, overlayTriVerts,
                                overlay->transform, glyphRes.vbuf.get(), glyphRes.set,
                                glyphRes.vertexCount);
                    cmd.pipelineBarrier(
                        vkh::PipelineStageFlagBits::eColorAttachmentOutput,
                        vkh::PipelineStageFlagBits::eTransfer, vkh::DependencyFlags{}, nullptr,
                        nullptr,
                        imageBarrier(img, vkh::ImageLayout::eColorAttachmentOptimal,
                                     vkh::ImageLayout::eTransferSrcOptimal,
                                     vkh::AccessFlagBits::eColorAttachmentWrite,
                                     vkh::AccessFlagBits::eTransferRead));
                } else {
                    cmd.pipelineBarrier(
                        vkh::PipelineStageFlagBits::eComputeShader,
                        vkh::PipelineStageFlagBits::eTransfer, vkh::DependencyFlags{}, nullptr,
                        nullptr,
                        imageBarrier(img, vkh::ImageLayout::eGeneral,
                                     vkh::ImageLayout::eTransferSrcOptimal,
                                     vkh::AccessFlagBits::eShaderWrite,
                                     vkh::AccessFlagBits::eTransferRead));
                }
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

Result<Renderer::GlyphResources> Renderer::buildGlyphResources(
    const std::vector<GlyphVertex>& glyphs, std::span<const std::int16_t> atlas) {
    const vkh::Device device = device_;
    GlyphResources g;
    if (glyphs.empty()) {
        return g; // vertexCount stays 0: nothing to draw
    }

    // 1. Vertex buffer (host-visible): the glyph quads.
    const vkh::DeviceSize vbytes = static_cast<vkh::DeviceSize>(glyphs.size()) * sizeof(GlyphVertex);
    {
        auto m = take(device.createBuffer(vkh::BufferCreateInfo{}
                                              .setSize(vbytes)
                                              .setUsage(vkh::BufferUsageFlagBits::eVertexBuffer)
                                              .setSharingMode(vkh::SharingMode::eExclusive)),
                      "createBuffer(glyph vbuf)");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        g.vbuf = Unique<vkh::Buffer>(*m, [device](vkh::Buffer h) { device.destroyBuffer(h); });
    }
    {
        auto mem = allocateAndBindBuffer(device, physicalDevice_, g.vbuf.get(),
                                         vkh::MemoryPropertyFlagBits::eHostVisible
                                             | vkh::MemoryPropertyFlagBits::eHostCoherent,
                                         "glyph vertices");
        if (!mem) {
            return std::unexpected(std::move(mem).error());
        }
        g.vmem = *std::move(mem);
    }
    {
        auto mapped = take(device.mapMemory(g.vmem.get(), 0, vbytes), "mapMemory(glyph vbuf)");
        if (!mapped) {
            return std::unexpected(std::move(mapped).error());
        }
        std::memcpy(*mapped, glyphs.data(), static_cast<std::size_t>(vbytes));
        device.unmapMemory(g.vmem.get());
    }

    // 2. Atlas uniform texel buffer (host-visible): the RGBA16I Slug stream.
    const vkh::DeviceSize abytes = static_cast<vkh::DeviceSize>(atlas.size()) * sizeof(std::int16_t);
    {
        auto m = take(device.createBuffer(vkh::BufferCreateInfo{}
                                              .setSize(abytes)
                                              .setUsage(vkh::BufferUsageFlagBits::eUniformTexelBuffer)
                                              .setSharingMode(vkh::SharingMode::eExclusive)),
                      "createBuffer(glyph atlas)");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        g.atlasBuf = Unique<vkh::Buffer>(*m, [device](vkh::Buffer h) { device.destroyBuffer(h); });
    }
    {
        auto mem = allocateAndBindBuffer(device, physicalDevice_, g.atlasBuf.get(),
                                         vkh::MemoryPropertyFlagBits::eHostVisible
                                             | vkh::MemoryPropertyFlagBits::eHostCoherent,
                                         "glyph atlas");
        if (!mem) {
            return std::unexpected(std::move(mem).error());
        }
        g.atlasMem = *std::move(mem);
    }
    {
        auto mapped = take(device.mapMemory(g.atlasMem.get(), 0, abytes), "mapMemory(glyph atlas)");
        if (!mapped) {
            return std::unexpected(std::move(mapped).error());
        }
        std::memcpy(*mapped, atlas.data(), static_cast<std::size_t>(abytes));
        device.unmapMemory(g.atlasMem.get());
    }
    {
        auto v = take(device.createBufferView(vkh::BufferViewCreateInfo{}
                                                  .setBuffer(g.atlasBuf.get())
                                                  .setFormat(vkh::Format::eR16G16B16A16Sint)
                                                  .setOffset(0)
                                                  .setRange(VK_WHOLE_SIZE)),
                      "createBufferView(glyph atlas)");
        if (!v) {
            return std::unexpected(std::move(v).error());
        }
        g.atlasView = Unique<vkh::BufferView>(
            *v, [device](vkh::BufferView h) { device.destroyBufferView(h); });
    }

    // 3. Descriptor pool + a set bound to the atlas view (set 0, binding 0).
    {
        const auto poolSize = vkh::DescriptorPoolSize{}
                                  .setType(vkh::DescriptorType::eUniformTexelBuffer)
                                  .setDescriptorCount(1u);
        auto p = take(device.createDescriptorPool(
                          vkh::DescriptorPoolCreateInfo{}.setMaxSets(1u).setPoolSizes(poolSize)),
                      "createDescriptorPool(glyph)");
        if (!p) {
            return std::unexpected(std::move(p).error());
        }
        g.pool = Unique<vkh::DescriptorPool>(
            *p, [device](vkh::DescriptorPool h) { device.destroyDescriptorPool(h); });
    }
    {
        const vkh::DescriptorSetLayout sl = glyphSetLayout_.get();
        auto s = take(device.allocateDescriptorSets(
                          vkh::DescriptorSetAllocateInfo{}.setDescriptorPool(g.pool.get()).setSetLayouts(
                              sl)),
                      "allocateDescriptorSets(glyph)");
        if (!s) {
            return std::unexpected(std::move(s).error());
        }
        g.set = (*s).front();
    }
    {
        const vkh::BufferView view = g.atlasView.get();
        const auto write = vkh::WriteDescriptorSet{}
                               .setDstSet(g.set)
                               .setDstBinding(0u)
                               .setDescriptorType(vkh::DescriptorType::eUniformTexelBuffer)
                               .setTexelBufferView(view);
        device.updateDescriptorSets(write, nullptr);
    }

    g.vertexCount = static_cast<std::uint32_t>(glyphs.size());
    return g;
}

void Renderer::drawOverlay(vkh::CommandBuffer cmd, vkh::Framebuffer framebuffer,
                           vkh::Extent2D extent, vkh::Buffer vbuf, std::uint32_t lineVertexCount,
                           std::uint32_t triangleVertexCount,
                           const std::array<float, 16>& transform, vkh::Buffer glyphVbuf,
                           vkh::DescriptorSet glyphSet, std::uint32_t glyphVertexCount) {
    cmd.beginRenderPass(vkh::RenderPassBeginInfo{}
                            .setRenderPass(renderPass_.get())
                            .setFramebuffer(framebuffer)
                            .setRenderArea(vkh::Rect2D{vkh::Offset2D{0, 0}, extent}),
                        vkh::SubpassContents::eInline);
    const auto viewport = vkh::Viewport{}
                              .setX(0.0f)
                              .setY(0.0f)
                              .setWidth(static_cast<float>(extent.width))
                              .setHeight(static_cast<float>(extent.height))
                              .setMinDepth(0.0f)
                              .setMaxDepth(1.0f);
    const auto scissor = vkh::Rect2D{vkh::Offset2D{0, 0}, extent};
    cmd.setViewport(0u, viewport);
    cmd.setScissor(0u, scissor);
    cmd.pushConstants<float>(overlayPipelineLayout_.get(), vkh::ShaderStageFlagBits::eVertex, 0u,
                             transform);
    const vkh::DeviceSize zero = 0;
    if (lineVertexCount > 0u) {
        cmd.bindPipeline(vkh::PipelineBindPoint::eGraphics, overlayLinePipeline_.get());
        cmd.bindVertexBuffers(0u, vbuf, zero);
        cmd.draw(lineVertexCount, 1u, 0u, 0u);
    }
    if (triangleVertexCount > 0u) {
        const vkh::DeviceSize triOffset =
            static_cast<vkh::DeviceSize>(lineVertexCount) * sizeof(OverlayVertex);
        cmd.bindPipeline(vkh::PipelineBindPoint::eGraphics, overlayTrianglePipeline_.get());
        cmd.bindVertexBuffers(0u, vbuf, triOffset);
        cmd.draw(triangleVertexCount, 1u, 0u, 0u);
    }
    // Slug glyph quads (ADR-0023): own pipeline + atlas descriptor set; positions
    // are already clip-space so the shared push-constant transform doesn't apply.
    if (glyphVertexCount > 0u && glyphVbuf && glyphSet) {
        const vkh::DeviceSize zeroOff = 0;
        cmd.bindPipeline(vkh::PipelineBindPoint::eGraphics, glyphPipeline_.get());
        cmd.bindDescriptorSets(vkh::PipelineBindPoint::eGraphics, glyphPipelineLayout_.get(), 0u,
                               glyphSet, nullptr);
        cmd.bindVertexBuffers(0u, glyphVbuf, zeroOff);
        cmd.draw(glyphVertexCount, 1u, 0u, 0u);
    }
    cmd.endRenderPass();
}

Status Renderer::ensureFrameGlyphResources(const std::vector<GlyphVertex>& glyphs,
                                           std::span<const std::int16_t> atlas) {
    frameGlyphVertexCount_ = static_cast<std::uint32_t>(glyphs.size());
    if (glyphs.empty()) {
        return {}; // nothing to draw this frame
    }
    const vkh::Device device = device_;

    // --- Glyph vertex buffer: grow on demand, then overwrite (positions move with
    //     the camera every frame). ---
    const vkh::DeviceSize vbytes = static_cast<vkh::DeviceSize>(glyphs.size()) * sizeof(GlyphVertex);
    if (vbytes > frameGlyphVbufCapacity_) {
        frameGlyphVbuf_.reset();
        frameGlyphVbufMapped_ = nullptr;
        auto m = take(device.createBuffer(vkh::BufferCreateInfo{}
                                              .setSize(vbytes)
                                              .setUsage(vkh::BufferUsageFlagBits::eVertexBuffer)
                                              .setSharingMode(vkh::SharingMode::eExclusive)),
                      "createBuffer(frame glyph vbuf)");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        frameGlyphVbuf_ =
            Unique<vkh::Buffer>(*m, [device](vkh::Buffer h) { device.destroyBuffer(h); });
        auto mem = allocateAndBindBuffer(device, physicalDevice_, frameGlyphVbuf_.get(),
                                         vkh::MemoryPropertyFlagBits::eHostVisible
                                             | vkh::MemoryPropertyFlagBits::eHostCoherent,
                                         "frame glyph vertices");
        if (!mem) {
            return std::unexpected(std::move(mem).error());
        }
        frameGlyphVbufMem_ = *std::move(mem);
        auto mapped = take(device.mapMemory(frameGlyphVbufMem_.get(), 0, vbytes),
                           "mapMemory(frame glyph vbuf)");
        if (!mapped) {
            return std::unexpected(std::move(mapped).error());
        }
        frameGlyphVbufMapped_ = *mapped;
        frameGlyphVbufCapacity_ = vbytes;
    }
    std::memcpy(frameGlyphVbufMapped_, glyphs.data(), static_cast<std::size_t>(vbytes));

    // --- Atlas uniform texel buffer + view + descriptor: recreate only on growth
    //     (glyph outlines are camera-independent), then re-upload the data. ---
    const vkh::DeviceSize abytes = static_cast<vkh::DeviceSize>(atlas.size()) * sizeof(std::int16_t);
    if (abytes > frameGlyphAtlasCapacity_) {
        frameGlyphAtlasView_.reset(); // references the old buffer
        frameGlyphAtlasBuf_.reset();
        frameGlyphAtlasMapped_ = nullptr;
        auto m = take(device.createBuffer(vkh::BufferCreateInfo{}
                                              .setSize(abytes)
                                              .setUsage(vkh::BufferUsageFlagBits::eUniformTexelBuffer)
                                              .setSharingMode(vkh::SharingMode::eExclusive)),
                      "createBuffer(frame glyph atlas)");
        if (!m) {
            return std::unexpected(std::move(m).error());
        }
        frameGlyphAtlasBuf_ =
            Unique<vkh::Buffer>(*m, [device](vkh::Buffer h) { device.destroyBuffer(h); });
        auto mem = allocateAndBindBuffer(device, physicalDevice_, frameGlyphAtlasBuf_.get(),
                                         vkh::MemoryPropertyFlagBits::eHostVisible
                                             | vkh::MemoryPropertyFlagBits::eHostCoherent,
                                         "frame glyph atlas");
        if (!mem) {
            return std::unexpected(std::move(mem).error());
        }
        frameGlyphAtlasMem_ = *std::move(mem);
        auto mapped = take(device.mapMemory(frameGlyphAtlasMem_.get(), 0, abytes),
                           "mapMemory(frame glyph atlas)");
        if (!mapped) {
            return std::unexpected(std::move(mapped).error());
        }
        frameGlyphAtlasMapped_ = *mapped;
        auto v = take(device.createBufferView(vkh::BufferViewCreateInfo{}
                                                  .setBuffer(frameGlyphAtlasBuf_.get())
                                                  .setFormat(vkh::Format::eR16G16B16A16Sint)
                                                  .setOffset(0)
                                                  .setRange(VK_WHOLE_SIZE)),
                      "createBufferView(frame glyph atlas)");
        if (!v) {
            return std::unexpected(std::move(v).error());
        }
        frameGlyphAtlasView_ = Unique<vkh::BufferView>(
            *v, [device](vkh::BufferView h) { device.destroyBufferView(h); });
        frameGlyphAtlasCapacity_ = abytes;

        // Descriptor pool + set: created once; (re)point the set at the new view.
        if (!frameGlyphPool_) {
            const auto poolSize = vkh::DescriptorPoolSize{}
                                      .setType(vkh::DescriptorType::eUniformTexelBuffer)
                                      .setDescriptorCount(1u);
            auto p = take(device.createDescriptorPool(
                              vkh::DescriptorPoolCreateInfo{}.setMaxSets(1u).setPoolSizes(poolSize)),
                          "createDescriptorPool(frame glyph)");
            if (!p) {
                return std::unexpected(std::move(p).error());
            }
            frameGlyphPool_ = Unique<vkh::DescriptorPool>(
                *p, [device](vkh::DescriptorPool h) { device.destroyDescriptorPool(h); });
            const vkh::DescriptorSetLayout sl = glyphSetLayout_.get();
            auto s = take(device.allocateDescriptorSets(
                              vkh::DescriptorSetAllocateInfo{}.setDescriptorPool(frameGlyphPool_.get())
                                  .setSetLayouts(sl)),
                          "allocateDescriptorSets(frame glyph)");
            if (!s) {
                return std::unexpected(std::move(s).error());
            }
            frameGlyphSet_ = (*s).front();
        }
        const vkh::BufferView view = frameGlyphAtlasView_.get();
        const auto write = vkh::WriteDescriptorSet{}
                               .setDstSet(frameGlyphSet_)
                               .setDstBinding(0u)
                               .setDescriptorType(vkh::DescriptorType::eUniformTexelBuffer)
                               .setTexelBufferView(view);
        device.updateDescriptorSets(write, nullptr);
    }
    std::memcpy(frameGlyphAtlasMapped_, atlas.data(), static_cast<std::size_t>(abytes));
    return {};
}

Status Renderer::ensureFrameResources(const Volume& volume, vkh::Extent2D extent) {
    // Nothing relevant changed since the last frame: reuse everything (ADR-0017).
    if (frameImage_ && frameExtent_ == extent && frameVolumeView_ == volume.view()) {
        return {};
    }
    const vkh::Device device = device_;
    frameFramebuffer_.reset(); // references the old frameView_, recreated below (ADR-0021)

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
                                        | vkh::ImageUsageFlagBits::eTransferSrc
                                        | vkh::ImageUsageFlagBits::eColorAttachment)
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
    {
        // Overlay framebuffer (ADR-0021) wrapping the new view, at the new extent.
        const vkh::ImageView attach = frameView_.get();
        auto fb = take(device.createFramebuffer(vkh::FramebufferCreateInfo{}
                                                    .setRenderPass(renderPass_.get())
                                                    .setAttachments(attach)
                                                    .setWidth(extent.width)
                                                    .setHeight(extent.height)
                                                    .setLayers(1u)),
                       "createFramebuffer(frame)");
        if (!fb) {
            return std::unexpected(std::move(fb).error());
        }
        frameFramebuffer_ = Unique<vkh::Framebuffer>(
            *fb, [device](vkh::Framebuffer h) { device.destroyFramebuffer(h); });
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
                             vkh::Extent2D dstExtent, const Overlay* overlay) {
    checkAffinity();
    IV_ASSERT(dstExtent.width > 0u && dstExtent.height > 0u,
              "Renderer::recordFrame: extent must be non-zero");
    if (auto s = ensureFrameResources(volume, dstExtent); !s) {
        return std::unexpected(std::move(s).error());
    }

    // Update the UBO in place (persistently mapped, host-coherent — no flush needed).
    const Ubo data = fillUbo(params, dstExtent.width, dstExtent.height, volume.magnitudeRange());
    std::memcpy(frameUboMapped_, &data, sizeof(Ubo));

    // Overlay (ADR-0021): grow the persistent vertex buffer on demand, copy vertices.
    const bool hasOverlay = overlay != nullptr && !overlay->empty();
    std::uint32_t ovLineVerts = 0u;
    std::uint32_t ovTriVerts = 0u;
    if (hasOverlay) {
        ovLineVerts = static_cast<std::uint32_t>(overlay->lines.size());
        ovTriVerts = static_cast<std::uint32_t>(overlay->triangles.size());
        const vkh::DeviceSize bytes =
            static_cast<vkh::DeviceSize>(ovLineVerts + ovTriVerts) * sizeof(OverlayVertex);
        if (bytes > frameOverlayCapacity_) {
            const vkh::Device device = device_;
            frameOverlayBuf_.reset();
            frameOverlayMapped_ = nullptr;
            {
                auto m = take(
                    device.createBuffer(vkh::BufferCreateInfo{}
                                            .setSize(bytes)
                                            .setUsage(vkh::BufferUsageFlagBits::eVertexBuffer)
                                            .setSharingMode(vkh::SharingMode::eExclusive)),
                    "createBuffer(frame overlay)");
                if (!m) {
                    return std::unexpected(std::move(m).error());
                }
                frameOverlayBuf_ =
                    Unique<vkh::Buffer>(*m, [device](vkh::Buffer h) { device.destroyBuffer(h); });
            }
            {
                auto mem = allocateAndBindBuffer(device, physicalDevice_, frameOverlayBuf_.get(),
                                                 vkh::MemoryPropertyFlagBits::eHostVisible
                                                     | vkh::MemoryPropertyFlagBits::eHostCoherent,
                                                 "frame overlay vertices");
                if (!mem) {
                    return std::unexpected(std::move(mem).error());
                }
                frameOverlayMem_ = *std::move(mem);
            }
            {
                auto mapped =
                    take(device.mapMemory(frameOverlayMem_.get(), 0, bytes), "mapMemory(overlay)");
                if (!mapped) {
                    return std::unexpected(std::move(mapped).error());
                }
                frameOverlayMapped_ = *mapped;
            }
            frameOverlayCapacity_ = bytes;
        }
        auto* dst = static_cast<unsigned char*>(frameOverlayMapped_);
        const std::size_t lineBytes = overlay->lines.size() * sizeof(OverlayVertex);
        if (!overlay->lines.empty()) {
            std::memcpy(dst, overlay->lines.data(), lineBytes);
        }
        if (!overlay->triangles.empty()) {
            std::memcpy(dst + lineBytes, overlay->triangles.data(),
                        overlay->triangles.size() * sizeof(OverlayVertex));
        }
    }
    // Present-path Slug glyph resources (ADR-0025): persist + re-upload (host prep,
    // before recording). Done even for a glyphs-only overlay.
    if (hasOverlay) {
        if (auto s = ensureFrameGlyphResources(overlay->glyphs,
                                               std::span<const std::int16_t>(overlay->glyphAtlas));
            !s) {
            return std::unexpected(std::move(s).error());
        }
    } else {
        frameGlyphVertexCount_ = 0u;
    }

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
    if (hasOverlay) {
        // storage: general -> color attachment, draw the overlay, -> transfer src.
        cmd.pipelineBarrier(
            vkh::PipelineStageFlagBits::eComputeShader,
            vkh::PipelineStageFlagBits::eColorAttachmentOutput, vkh::DependencyFlags{}, nullptr,
            nullptr,
            imageBarrier(storage, vkh::ImageLayout::eGeneral,
                         vkh::ImageLayout::eColorAttachmentOptimal,
                         vkh::AccessFlagBits::eShaderWrite,
                         vkh::AccessFlagBits::eColorAttachmentWrite
                             | vkh::AccessFlagBits::eColorAttachmentRead));
        // Present path draws lines/triangles AND Slug glyphs (ADR-0025): the
        // persistent glyph resources were (re)built above.
        drawOverlay(cmd, frameFramebuffer_.get(), dstExtent, frameOverlayBuf_.get(), ovLineVerts,
                    ovTriVerts, overlay->transform, frameGlyphVbuf_.get(), frameGlyphSet_,
                    frameGlyphVertexCount_);
        cmd.pipelineBarrier(
            vkh::PipelineStageFlagBits::eColorAttachmentOutput,
            vkh::PipelineStageFlagBits::eTransfer, vkh::DependencyFlags{}, nullptr, nullptr,
            imageBarrier(storage, vkh::ImageLayout::eColorAttachmentOptimal,
                         vkh::ImageLayout::eTransferSrcOptimal,
                         vkh::AccessFlagBits::eColorAttachmentWrite,
                         vkh::AccessFlagBits::eTransferRead));
    } else {
        // storage: general -> transferSrc for the blit.
        cmd.pipelineBarrier(
            vkh::PipelineStageFlagBits::eComputeShader, vkh::PipelineStageFlagBits::eTransfer,
            vkh::DependencyFlags{}, nullptr, nullptr,
            imageBarrier(storage, vkh::ImageLayout::eGeneral, vkh::ImageLayout::eTransferSrcOptimal,
                         vkh::AccessFlagBits::eShaderWrite, vkh::AccessFlagBits::eTransferRead));
    }
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
