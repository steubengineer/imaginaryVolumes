#include "iv/vk/volume.hpp"

#include "iv/assert.hpp"
#include "iv/vk/commands.hpp"
#include "iv/vk/memory.hpp"
#include "iv/vk/result.hpp"

#include <cstring>

namespace iv::vk {

namespace vkh = ::vk;

namespace {

constexpr vkh::Format kVolumeFormat = vkh::Format::eR32G32Sfloat;
constexpr std::size_t kChannels = 2; // (magnitude, phase)

// One region covering the whole 3D image, tightly packed in the buffer
// (bufferRowLength = bufferImageHeight = 0). Tight packing matches the x-fastest
// layout (ADR-0009 / D-0006), so a plain linear buffer maps to (x,y,z).
vkh::BufferImageCopy fullCopyRegion(GridDims dims) {
    return vkh::BufferImageCopy{}
        .setBufferOffset(0)
        .setBufferRowLength(0u)
        .setBufferImageHeight(0u)
        .setImageSubresource(
            vkh::ImageSubresourceLayers{vkh::ImageAspectFlagBits::eColor, 0u, 0u, 1u})
        .setImageOffset(vkh::Offset3D{0, 0, 0})
        .setImageExtent(vkh::Extent3D{dims.nx, dims.ny, dims.nz});
}

} // namespace

VolumeReadback::VolumeReadback(GridDims dims, std::vector<float> data)
    : dims_(dims), data_(std::move(data)) {}

VolumeReadback::Texel VolumeReadback::at(std::uint32_t x, std::uint32_t y,
                                         std::uint32_t z) const noexcept {
    IV_ASSERT(x < dims_.nx && y < dims_.ny && z < dims_.nz,
              "VolumeReadback::at: coordinate out of bounds");
    const std::size_t i = dims_.index(x, y, z) * kChannels;
    return Texel{data_[i], data_[i + 1u]};
}

void Volume::checkAffinity() const noexcept {
    IV_DEBUG_ASSERT(std::this_thread::get_id() == ownerThread_,
                    "Volume used from a thread other than its owner (ADR-0007)");
}

template <class T>
Result<Volume> Volume::createImpl(const Context& ctx, std::span<const std::complex<T>> field,
                                  GridDims dims, const VolumeOptions& options) {
    // Host validation first (ADR-0008/0010), before any GPU work.
    if (auto s = validateGrid(dims); !s) {
        return std::unexpected(std::move(s).error());
    }
    if (auto s = validateShape(field.size(), dims); !s) {
        return std::unexpected(std::move(s).error());
    }
    if (auto s = validateOptions(options); !s) {
        return std::unexpected(std::move(s).error());
    }

    const vkh::Device device = ctx.device();
    const vkh::PhysicalDevice phys = ctx.physicalDevice();
    const vkh::Queue queue = ctx.queue();
    const vkh::CommandPool pool = ctx.commandPool();

    // Device 3D-image dimension limit (ADR-0009).
    const auto limits = phys.getProperties().limits;
    if (dims.nx > limits.maxImageDimension3D || dims.ny > limits.maxImageDimension3D ||
        dims.nz > limits.maxImageDimension3D) {
        return make_error(Errc::unsupported_configuration,
                          "volume dimension exceeds device maxImageDimension3D");
    }

    const vkh::DeviceSize byteSize =
        static_cast<vkh::DeviceSize>(dims.count()) * kChannels * sizeof(float);

    // Declared so destruction (reverse order) destroys each object before freeing
    // the memory bound to it, and the staging pair before its memory.
    Unique<vkh::DeviceMemory> imageMem;
    Unique<vkh::Image> image;
    Unique<vkh::ImageView> view;
    Unique<vkh::DeviceMemory> stagingMem;
    Unique<vkh::Buffer> staging;

    // --- 3D RG32F image (device-local) ---
    const auto imageInfo =
        vkh::ImageCreateInfo{}
            .setImageType(vkh::ImageType::e3D)
            .setFormat(kVolumeFormat)
            .setExtent(vkh::Extent3D{dims.nx, dims.ny, dims.nz})
            .setMipLevels(1u)
            .setArrayLayers(1u)
            .setSamples(vkh::SampleCountFlagBits::e1)
            .setTiling(vkh::ImageTiling::eOptimal)
            .setUsage(vkh::ImageUsageFlagBits::eSampled | vkh::ImageUsageFlagBits::eTransferDst
                      | vkh::ImageUsageFlagBits::eTransferSrc)
            .setInitialLayout(vkh::ImageLayout::eUndefined);
    {
        auto r = take(device.createImage(imageInfo), "createImage(volume)");
        if (!r) {
            return std::unexpected(std::move(r).error());
        }
        image = Unique<vkh::Image>(*r, [device](vkh::Image h) { device.destroyImage(h); });
    }
    {
        auto mem = allocateAndBindImage(device, phys, image.get(),
                                        vkh::MemoryPropertyFlagBits::eDeviceLocal, "volume image");
        if (!mem) {
            return std::unexpected(std::move(mem).error());
        }
        imageMem = *std::move(mem);
    }

    // --- Host-visible staging buffer; derive (magnitude, phase) into it ---
    {
        auto r = take(device.createBuffer(vkh::BufferCreateInfo{}
                                              .setSize(byteSize)
                                              .setUsage(vkh::BufferUsageFlagBits::eTransferSrc)
                                              .setSharingMode(vkh::SharingMode::eExclusive)),
                      "createBuffer(volume upload)");
        if (!r) {
            return std::unexpected(std::move(r).error());
        }
        staging = Unique<vkh::Buffer>(*r, [device](vkh::Buffer h) { device.destroyBuffer(h); });
    }
    {
        auto mem = allocateAndBindBuffer(device, phys, staging.get(),
                                         vkh::MemoryPropertyFlagBits::eHostVisible
                                             | vkh::MemoryPropertyFlagBits::eHostCoherent,
                                         "volume upload staging");
        if (!mem) {
            return std::unexpected(std::move(mem).error());
        }
        stagingMem = *std::move(mem);
    }

    MagnitudeRange autoRange{};
    {
        auto mapped = take(device.mapMemory(stagingMem.get(), 0, byteSize), "mapMemory(upload)");
        if (!mapped) {
            return std::unexpected(std::move(mapped).error());
        }
        const std::span<float> out{static_cast<float*>(*mapped), dims.count() * kChannels};
        autoRange = deriveField<T>(field, dims, out);
        device.unmapMemory(stagingMem.get());
    }

    // --- Upload: undefined -> transferDst, copy, -> shaderReadOnly (resting) ---
    const vkh::Image img = image.get();
    const vkh::Buffer buf = staging.get();
    const auto region = fullCopyRegion(dims);
    if (auto s = submitOneShot(
            device, queue, pool,
            [&](vkh::CommandBuffer cmd) {
                cmd.pipelineBarrier(
                    vkh::PipelineStageFlagBits::eTopOfPipe, vkh::PipelineStageFlagBits::eTransfer,
                    vkh::DependencyFlags{}, nullptr, nullptr,
                    imageBarrier(img, vkh::ImageLayout::eUndefined,
                                  vkh::ImageLayout::eTransferDstOptimal,
                                  vkh::AccessFlagBits::eNone, vkh::AccessFlagBits::eTransferWrite));
                cmd.copyBufferToImage(buf, img, vkh::ImageLayout::eTransferDstOptimal, region);
                cmd.pipelineBarrier(
                    vkh::PipelineStageFlagBits::eTransfer,
                    vkh::PipelineStageFlagBits::eFragmentShader, vkh::DependencyFlags{}, nullptr,
                    nullptr,
                    imageBarrier(img, vkh::ImageLayout::eTransferDstOptimal,
                                  vkh::ImageLayout::eShaderReadOnlyOptimal,
                                  vkh::AccessFlagBits::eTransferWrite,
                                  vkh::AccessFlagBits::eShaderRead));
            });
        !s) {
        return std::unexpected(std::move(s).error());
    }

    // --- Sampling view for M4 (the sampler itself is deferred to M4) ---
    {
        auto r = take(
            device.createImageView(vkh::ImageViewCreateInfo{}
                                       .setImage(image.get())
                                       .setViewType(vkh::ImageViewType::e3D)
                                       .setFormat(kVolumeFormat)
                                       .setSubresourceRange(vkh::ImageSubresourceRange{
                                           vkh::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u})),
            "createImageView(volume)");
        if (!r) {
            return std::unexpected(std::move(r).error());
        }
        view =
            Unique<vkh::ImageView>(*r, [device](vkh::ImageView v) { device.destroyImageView(v); });
    }

    Volume vol;
    vol.device_ = device;
    vol.physicalDevice_ = phys;
    vol.queue_ = queue;
    vol.commandPool_ = pool;
    vol.dims_ = dims;
    vol.autoRange_ = autoRange;
    vol.range_ = options.magnitudeRange ? *options.magnitudeRange : autoRange;
    vol.ownerThread_ = std::this_thread::get_id();
    vol.memory_ = std::move(imageMem);
    vol.image_ = std::move(image);
    vol.view_ = std::move(view);
    return vol;
}

Result<Volume> Volume::create(const Context& ctx, std::span<const std::complex<float>> field,
                              GridDims dims, const VolumeOptions& options) {
    return createImpl<float>(ctx, field, dims, options);
}

Result<Volume> Volume::create(const Context& ctx, std::span<const std::complex<double>> field,
                              GridDims dims, const VolumeOptions& options) {
    return createImpl<double>(ctx, field, dims, options);
}

Result<VolumeReadback> Volume::readback() const {
    checkAffinity();
    const vkh::Device device = device_;
    const vkh::DeviceSize byteSize =
        static_cast<vkh::DeviceSize>(dims_.count()) * kChannels * sizeof(float);

    Unique<vkh::DeviceMemory> stagingMem;
    Unique<vkh::Buffer> staging;
    {
        auto r = take(device.createBuffer(vkh::BufferCreateInfo{}
                                              .setSize(byteSize)
                                              .setUsage(vkh::BufferUsageFlagBits::eTransferDst)
                                              .setSharingMode(vkh::SharingMode::eExclusive)),
                      "createBuffer(volume readback)");
        if (!r) {
            return std::unexpected(std::move(r).error());
        }
        staging = Unique<vkh::Buffer>(*r, [device](vkh::Buffer h) { device.destroyBuffer(h); });
    }
    {
        auto mem = allocateAndBindBuffer(device, physicalDevice_, staging.get(),
                                         vkh::MemoryPropertyFlagBits::eHostVisible
                                             | vkh::MemoryPropertyFlagBits::eHostCoherent,
                                         "volume readback staging");
        if (!mem) {
            return std::unexpected(std::move(mem).error());
        }
        stagingMem = *std::move(mem);
    }

    const vkh::Image img = image_.get();
    const vkh::Buffer buf = staging.get();
    const auto region = fullCopyRegion(dims_);
    if (auto s = submitOneShot(
            device, queue_, commandPool_,
            [&](vkh::CommandBuffer cmd) {
                cmd.pipelineBarrier(
                    vkh::PipelineStageFlagBits::eFragmentShader,
                    vkh::PipelineStageFlagBits::eTransfer, vkh::DependencyFlags{}, nullptr, nullptr,
                    imageBarrier(img, vkh::ImageLayout::eShaderReadOnlyOptimal,
                                  vkh::ImageLayout::eTransferSrcOptimal,
                                  vkh::AccessFlagBits::eShaderRead,
                                  vkh::AccessFlagBits::eTransferRead));
                cmd.copyImageToBuffer(img, vkh::ImageLayout::eTransferSrcOptimal, buf, region);
                cmd.pipelineBarrier(
                    vkh::PipelineStageFlagBits::eTransfer,
                    vkh::PipelineStageFlagBits::eFragmentShader, vkh::DependencyFlags{}, nullptr,
                    nullptr,
                    imageBarrier(img, vkh::ImageLayout::eTransferSrcOptimal,
                                  vkh::ImageLayout::eShaderReadOnlyOptimal,
                                  vkh::AccessFlagBits::eTransferRead,
                                  vkh::AccessFlagBits::eShaderRead));
            });
        !s) {
        return std::unexpected(std::move(s).error());
    }

    auto mapped = take(device.mapMemory(stagingMem.get(), 0, byteSize), "mapMemory(readback)");
    if (!mapped) {
        return std::unexpected(std::move(mapped).error());
    }
    std::vector<float> data(dims_.count() * kChannels);
    std::memcpy(data.data(), *mapped, static_cast<std::size_t>(byteSize));
    device.unmapMemory(stagingMem.get());

    return VolumeReadback(dims_, std::move(data));
}

} // namespace iv::vk
