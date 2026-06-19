#ifndef IV_VK_COMMANDS_HPP
#define IV_VK_COMMANDS_HPP

// Shared one-shot command submission and classic-1.0 image-barrier helpers
// (ADR-0004 ownership, ADR-0007 fence-gated host reads, D-0016 classic barriers).
// Used by the volume upload (ADR-0009) and the renderer (ADR-0011).

#include "iv/error.hpp"
#include "iv/vk/result.hpp"
#include "iv/vk/unique.hpp"
#include "iv/vk/vulkan.hpp"

#include <cstdint>
#include <limits>

namespace iv::vk {

// A classic-1.0 image memory barrier over the full color subresource.
[[nodiscard]] inline ::vk::ImageMemoryBarrier imageBarrier(::vk::Image image,
                                                           ::vk::ImageLayout oldLayout,
                                                           ::vk::ImageLayout newLayout,
                                                           ::vk::AccessFlags srcAccess,
                                                           ::vk::AccessFlags dstAccess) {
    const ::vk::ImageSubresourceRange range{::vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u};
    return ::vk::ImageMemoryBarrier{}
        .setOldLayout(oldLayout)
        .setNewLayout(newLayout)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(image)
        .setSubresourceRange(range)
        .setSrcAccessMask(srcAccess)
        .setDstAccessMask(dstAccess);
}

// Allocate a primary command buffer, record via `record(cmd)`, submit to `queue`,
// and wait on a fence before returning (host reads happen only after the fence;
// ADR-0007). One-shot; frees the command buffer on return.
template <class Record>
[[nodiscard]] Status submitOneShot(::vk::Device device, ::vk::Queue queue, ::vk::CommandPool pool,
                                   Record&& record) {
    Unique<::vk::CommandBuffer> cmd;
    {
        auto r = take(device.allocateCommandBuffers(::vk::CommandBufferAllocateInfo{}
                                                        .setCommandPool(pool)
                                                        .setLevel(::vk::CommandBufferLevel::ePrimary)
                                                        .setCommandBufferCount(1u)),
                      "allocateCommandBuffers");
        if (!r) {
            return std::unexpected(std::move(r).error());
        }
        const ::vk::CommandBuffer handle = (*r).front();
        cmd = Unique<::vk::CommandBuffer>(
            handle, [device, pool](::vk::CommandBuffer c) { device.freeCommandBuffers(pool, c); });
    }

    if (auto s = check(cmd.get().begin(::vk::CommandBufferBeginInfo{}.setFlags(
                           ::vk::CommandBufferUsageFlagBits::eOneTimeSubmit)),
                       "beginCommandBuffer");
        !s) {
        return std::unexpected(std::move(s).error());
    }
    record(cmd.get());
    if (auto s = check(cmd.get().end(), "endCommandBuffer"); !s) {
        return std::unexpected(std::move(s).error());
    }

    Unique<::vk::Fence> fence;
    {
        auto r = take(device.createFence(::vk::FenceCreateInfo{}), "createFence");
        if (!r) {
            return std::unexpected(std::move(r).error());
        }
        fence = Unique<::vk::Fence>(*r, [device](::vk::Fence f) { device.destroyFence(f); });
    }
    const ::vk::CommandBuffer submitCmd = cmd.get();
    if (auto s = check(queue.submit(::vk::SubmitInfo{}.setCommandBuffers(submitCmd), fence.get()),
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
    return {};
}

} // namespace iv::vk

#endif // IV_VK_COMMANDS_HPP
