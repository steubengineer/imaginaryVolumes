#include "iv/vk/memory.hpp"

#include "iv/vk/result.hpp"

#include <string>

namespace iv::vk {

namespace vkh = ::vk;

Result<std::uint32_t> findMemoryType(vkh::PhysicalDevice phys, std::uint32_t typeBits,
                                     vkh::MemoryPropertyFlags required, std::string_view what) {
    const auto props = phys.getMemoryProperties();
    for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        const bool typeAllowed = (typeBits & (1u << i)) != 0u;
        const bool hasProps = (props.memoryTypes[i].propertyFlags & required) == required;
        if (typeAllowed && hasProps) {
            return i;
        }
    }
    return make_error(Errc::unsupported_configuration,
                      "no suitable memory type for " + std::string(what));
}

namespace {

// Allocate `size` bytes of `typeIndex` memory, wrapped in an owning Unique.
Result<Unique<vkh::DeviceMemory>> allocate(vkh::Device device, std::uint32_t typeIndex,
                                           vkh::DeviceSize size) {
    auto r = take(device.allocateMemory(vkh::MemoryAllocateInfo{}
                                            .setAllocationSize(size)
                                            .setMemoryTypeIndex(typeIndex)),
                  "allocateMemory");
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    return Unique<vkh::DeviceMemory>(*r, [device](vkh::DeviceMemory m) { device.freeMemory(m); });
}

} // namespace

Result<Unique<vkh::DeviceMemory>> allocateAndBindImage(vkh::Device device,
                                                       vkh::PhysicalDevice phys, vkh::Image image,
                                                       vkh::MemoryPropertyFlags props,
                                                       std::string_view what) {
    const auto req = device.getImageMemoryRequirements(image);
    auto type = findMemoryType(phys, req.memoryTypeBits, props, what);
    if (!type) {
        return std::unexpected(std::move(type).error());
    }
    auto mem = allocate(device, *type, req.size);
    if (!mem) {
        return std::unexpected(std::move(mem).error());
    }
    if (auto s = check(device.bindImageMemory(image, mem->get(), 0), "bindImageMemory"); !s) {
        return std::unexpected(std::move(s).error());
    }
    return mem;
}

Result<Unique<vkh::DeviceMemory>> allocateAndBindBuffer(vkh::Device device,
                                                        vkh::PhysicalDevice phys,
                                                        vkh::Buffer buffer,
                                                        vkh::MemoryPropertyFlags props,
                                                        std::string_view what) {
    const auto req = device.getBufferMemoryRequirements(buffer);
    auto type = findMemoryType(phys, req.memoryTypeBits, props, what);
    if (!type) {
        return std::unexpected(std::move(type).error());
    }
    auto mem = allocate(device, *type, req.size);
    if (!mem) {
        return std::unexpected(std::move(mem).error());
    }
    if (auto s = check(device.bindBufferMemory(buffer, mem->get(), 0), "bindBufferMemory"); !s) {
        return std::unexpected(std::move(s).error());
    }
    return mem;
}

} // namespace iv::vk
