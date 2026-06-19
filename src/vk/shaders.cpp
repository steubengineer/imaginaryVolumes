#include "iv/vk/shaders.hpp"

#include "iv/vk/result.hpp"

#include <cstdint>

namespace iv::vk {

namespace vkh = ::vk;

Result<Unique<vkh::ShaderModule>> makeShaderModule(vkh::Device device,
                                                   const unsigned char* spirv,
                                                   std::size_t sizeBytes) {
    // SPIR-V is a stream of uint32 words; `spirv` is 4-byte aligned (ADR-0011).
    // The static_cast through void* avoids a -Wcast-align diagnostic.
    const auto* code = static_cast<const std::uint32_t*>(static_cast<const void*>(spirv));
    auto r = take(device.createShaderModule(
                      vkh::ShaderModuleCreateInfo{}.setCodeSize(sizeBytes).setPCode(code)),
                  "createShaderModule");
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    return Unique<vkh::ShaderModule>(*r,
                                     [device](vkh::ShaderModule m) { device.destroyShaderModule(m); });
}

} // namespace iv::vk
