#include "iv/vk/offscreen.hpp"

#include "iv/assert.hpp"
#include "iv/vk/result.hpp"
#include "iv/vk/unique.hpp"

#include <cstring>
#include <limits>
#include <optional>

namespace iv::vk {
namespace {

namespace vkh = ::vk;

std::optional<std::uint32_t> findMemoryType(vkh::PhysicalDevice pd,
                                            std::uint32_t typeBits,
                                            vkh::MemoryPropertyFlags required) {
    const auto props = pd.getMemoryProperties();
    for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        const bool typeAllowed = (typeBits & (1u << i)) != 0u;
        const bool hasProps = (props.memoryTypes[i].propertyFlags & required) == required;
        if (typeAllowed && hasProps) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace

ImageReadback::ImageReadback(std::uint32_t width, std::uint32_t height,
                             std::vector<std::uint8_t> bytes)
    : width_(width), height_(height), bytes_(std::move(bytes)) {}

ImageReadback::Rgba ImageReadback::at(std::uint32_t x, std::uint32_t y) const noexcept {
    IV_ASSERT(x < width_ && y < height_, "ImageReadback::at: coordinate out of bounds");
    const std::size_t offset = (static_cast<std::size_t>(y) * width_ + x) * 4u;
    return Rgba{bytes_[offset], bytes_[offset + 1], bytes_[offset + 2], bytes_[offset + 3]};
}

Result<ImageReadback> clearAndReadback(const Context& ctx, std::uint32_t width,
                                       std::uint32_t height, std::array<float, 4> color) {
    IV_ASSERT(width > 0u && height > 0u, "clearAndReadback: extent must be non-zero");

    namespace vkh = ::vk;
    const vkh::Device device = ctx.device();
    const vkh::PhysicalDevice phys = ctx.physicalDevice();
    const VkDeviceSize size = static_cast<VkDeviceSize>(width) * height * 4u;

    // Declared so destruction (reverse order) destroys each object before freeing
    // the memory bound to it, and frees the command buffer while the pool lives.
    Unique<vkh::DeviceMemory> imageMem;
    Unique<vkh::Image> image;
    Unique<vkh::DeviceMemory> bufferMem;
    Unique<vkh::Buffer> buffer;
    Unique<vkh::Fence> fence;
    Unique<vkh::CommandBuffer> cmd;

    // --- Offscreen image (device-local) ---
    const auto imageInfo =
        vkh::ImageCreateInfo{}
            .setImageType(vkh::ImageType::e2D)
            .setFormat(vkh::Format::eR8G8B8A8Unorm)
            .setExtent(vkh::Extent3D{width, height, 1u})
            .setMipLevels(1u)
            .setArrayLayers(1u)
            .setSamples(vkh::SampleCountFlagBits::e1)
            .setTiling(vkh::ImageTiling::eOptimal)
            .setUsage(vkh::ImageUsageFlagBits::eColorAttachment
                      | vkh::ImageUsageFlagBits::eTransferSrc
                      | vkh::ImageUsageFlagBits::eTransferDst)
            .setInitialLayout(vkh::ImageLayout::eUndefined);
    {
        auto r = take(device.createImage(imageInfo), "createImage");
        if (!r) {
            return std::unexpected(std::move(r).error());
        }
        image = Unique<vkh::Image>(*r, [device](vkh::Image h) { device.destroyImage(h); });
    }
    {
        const auto req = device.getImageMemoryRequirements(image.get());
        const auto type = findMemoryType(phys, req.memoryTypeBits,
                                         vkh::MemoryPropertyFlagBits::eDeviceLocal);
        if (!type) {
            return make_error(Errc::unsupported_configuration,
                              "no device-local memory type for the offscreen image");
        }
        auto r = take(device.allocateMemory(vkh::MemoryAllocateInfo{}
                                                .setAllocationSize(req.size)
                                                .setMemoryTypeIndex(*type)),
                      "allocateMemory(image)");
        if (!r) {
            return std::unexpected(std::move(r).error());
        }
        imageMem = Unique<vkh::DeviceMemory>(
            *r, [device](vkh::DeviceMemory m) { device.freeMemory(m); });
        if (auto s = check(device.bindImageMemory(image.get(), imageMem.get(), 0),
                           "bindImageMemory");
            !s) {
            return std::unexpected(std::move(s).error());
        }
    }

    // --- Host-visible staging buffer for readback ---
    {
        auto r = take(device.createBuffer(vkh::BufferCreateInfo{}
                                              .setSize(size)
                                              .setUsage(vkh::BufferUsageFlagBits::eTransferDst)
                                              .setSharingMode(vkh::SharingMode::eExclusive)),
                      "createBuffer");
        if (!r) {
            return std::unexpected(std::move(r).error());
        }
        buffer = Unique<vkh::Buffer>(*r, [device](vkh::Buffer h) { device.destroyBuffer(h); });
    }
    {
        const auto req = device.getBufferMemoryRequirements(buffer.get());
        const auto type = findMemoryType(phys, req.memoryTypeBits,
                                         vkh::MemoryPropertyFlagBits::eHostVisible
                                             | vkh::MemoryPropertyFlagBits::eHostCoherent);
        if (!type) {
            return make_error(Errc::unsupported_configuration,
                              "no host-visible|coherent memory type for the staging buffer");
        }
        auto r = take(device.allocateMemory(vkh::MemoryAllocateInfo{}
                                                .setAllocationSize(req.size)
                                                .setMemoryTypeIndex(*type)),
                      "allocateMemory(buffer)");
        if (!r) {
            return std::unexpected(std::move(r).error());
        }
        bufferMem = Unique<vkh::DeviceMemory>(
            *r, [device](vkh::DeviceMemory m) { device.freeMemory(m); });
        if (auto s = check(device.bindBufferMemory(buffer.get(), bufferMem.get(), 0),
                           "bindBufferMemory");
            !s) {
            return std::unexpected(std::move(s).error());
        }
    }

    // --- Command buffer ---
    {
        const vkh::CommandPool pool = ctx.commandPool();
        auto r = take(device.allocateCommandBuffers(
                          vkh::CommandBufferAllocateInfo{}
                              .setCommandPool(pool)
                              .setLevel(vkh::CommandBufferLevel::ePrimary)
                              .setCommandBufferCount(1u)),
                      "allocateCommandBuffers");
        if (!r) {
            return std::unexpected(std::move(r).error());
        }
        const vkh::CommandBuffer handle = (*r).front();
        cmd = Unique<vkh::CommandBuffer>(
            handle, [device, pool](vkh::CommandBuffer c) { device.freeCommandBuffers(pool, c); });
    }

    // --- Record: undefined -> transferDst, clear, -> transferSrc, copy to buffer ---
    const vkh::ImageSubresourceRange range{vkh::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u};
    if (auto s = check(cmd.get().begin(vkh::CommandBufferBeginInfo{}.setFlags(
                           vkh::CommandBufferUsageFlagBits::eOneTimeSubmit)),
                       "beginCommandBuffer");
        !s) {
        return std::unexpected(std::move(s).error());
    }

    const auto toTransferDst =
        vkh::ImageMemoryBarrier{}
            .setOldLayout(vkh::ImageLayout::eUndefined)
            .setNewLayout(vkh::ImageLayout::eTransferDstOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(image.get())
            .setSubresourceRange(range)
            .setSrcAccessMask(vkh::AccessFlagBits::eNone)
            .setDstAccessMask(vkh::AccessFlagBits::eTransferWrite);
    cmd.get().pipelineBarrier(vkh::PipelineStageFlagBits::eTopOfPipe,
                              vkh::PipelineStageFlagBits::eTransfer,
                              vkh::DependencyFlags{}, nullptr, nullptr, toTransferDst);

    const vkh::ClearColorValue clearColor{color};
    cmd.get().clearColorImage(image.get(), vkh::ImageLayout::eTransferDstOptimal, clearColor,
                              range);

    const auto toTransferSrc =
        vkh::ImageMemoryBarrier{}
            .setOldLayout(vkh::ImageLayout::eTransferDstOptimal)
            .setNewLayout(vkh::ImageLayout::eTransferSrcOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(image.get())
            .setSubresourceRange(range)
            .setSrcAccessMask(vkh::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vkh::AccessFlagBits::eTransferRead);
    cmd.get().pipelineBarrier(vkh::PipelineStageFlagBits::eTransfer,
                              vkh::PipelineStageFlagBits::eTransfer, vkh::DependencyFlags{},
                              nullptr, nullptr, toTransferSrc);

    const auto region =
        vkh::BufferImageCopy{}
            .setBufferOffset(0)
            .setBufferRowLength(0u)   // 0 => tightly packed (ADR-0006)
            .setBufferImageHeight(0u)
            .setImageSubresource(
                vkh::ImageSubresourceLayers{vkh::ImageAspectFlagBits::eColor, 0u, 0u, 1u})
            .setImageOffset(vkh::Offset3D{0, 0, 0})
            .setImageExtent(vkh::Extent3D{width, height, 1u});
    cmd.get().copyImageToBuffer(image.get(), vkh::ImageLayout::eTransferSrcOptimal, buffer.get(),
                                region);

    if (auto s = check(cmd.get().end(), "endCommandBuffer"); !s) {
        return std::unexpected(std::move(s).error());
    }

    // --- Submit and wait (host reads only after the fence; ADR-0007) ---
    {
        auto r = take(device.createFence(vkh::FenceCreateInfo{}), "createFence");
        if (!r) {
            return std::unexpected(std::move(r).error());
        }
        fence = Unique<vkh::Fence>(*r, [device](vkh::Fence f) { device.destroyFence(f); });
    }
    const vkh::CommandBuffer submitCmd = cmd.get();
    if (auto s = check(ctx.queue().submit(vkh::SubmitInfo{}.setCommandBuffers(submitCmd),
                                          fence.get()),
                       "queueSubmit");
        !s) {
        return std::unexpected(std::move(s).error());
    }
    if (auto s = check(device.waitForFences(fence.get(), VK_TRUE,
                                            std::numeric_limits<std::uint64_t>::max()),
                       "waitForFences");
        !s) {
        return std::unexpected(std::move(s).error());
    }

    // --- Map staging memory and copy out (host-coherent: no invalidate needed) ---
    auto mapped = take(device.mapMemory(bufferMem.get(), 0, size), "mapMemory");
    if (!mapped) {
        return std::unexpected(std::move(mapped).error());
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::memcpy(bytes.data(), *mapped, static_cast<std::size_t>(size));
    device.unmapMemory(bufferMem.get());

    return ImageReadback(width, height, std::move(bytes));
}

} // namespace iv::vk
