#ifndef IV_VK_MEMORY_HPP
#define IV_VK_MEMORY_HPP

// Shared device-memory helpers (ADR-0009, D-0017): the generalized successors to
// M2's file-local findMemoryType. Raw vkAllocateMemory; a general allocator (VMA)
// stays deferred (Backlog B-0006). Used by both clearAndReadback (ADR-0006) and
// iv::vk::Volume (ADR-0009).

#include "iv/error.hpp"
#include "iv/vk/unique.hpp"
#include "iv/vk/vulkan.hpp"

#include <cstdint>
#include <string_view>

namespace iv::vk {

// First memory-type index whose bit is set in `typeBits` and whose properties
// include `required`. Errc::unsupported_configuration if none qualifies.
[[nodiscard]] Result<std::uint32_t> findMemoryType(::vk::PhysicalDevice phys,
                                                   std::uint32_t typeBits,
                                                   ::vk::MemoryPropertyFlags required,
                                                   std::string_view what);

// Allocate memory satisfying `image`'s requirements with `props`, bind it, and
// return the owning handle. The image must be destroyed before the returned
// memory is freed (declare/own them in that order).
[[nodiscard]] Result<Unique<::vk::DeviceMemory>> allocateAndBindImage(
    ::vk::Device device, ::vk::PhysicalDevice phys, ::vk::Image image,
    ::vk::MemoryPropertyFlags props, std::string_view what);

// As allocateAndBindImage, for a buffer.
[[nodiscard]] Result<Unique<::vk::DeviceMemory>> allocateAndBindBuffer(
    ::vk::Device device, ::vk::PhysicalDevice phys, ::vk::Buffer buffer,
    ::vk::MemoryPropertyFlags props, std::string_view what);

} // namespace iv::vk

#endif // IV_VK_MEMORY_HPP
