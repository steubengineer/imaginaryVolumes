#ifndef IV_VK_SHADERS_HPP
#define IV_VK_SHADERS_HPP

// Embedded SPIR-V (ADR-0011, D-0022). GLSL under shaders/ is compiled by glslc at
// build time; tools/embed_spirv.cmake emits the matching byte-array definitions
// (<symbol>.spv.cpp) which are compiled into libiv. The arrays are 4-byte aligned
// so they can be read as a uint32 SPIR-V stream.

#include "iv/error.hpp"
#include "iv/vk/unique.hpp"
#include "iv/vk/vulkan.hpp"

#include <cstddef>

namespace iv::vk::shaders {

// shaders/ray_march.comp
extern const unsigned char ray_march_comp_data[];
extern const ::std::size_t ray_march_comp_size;

} // namespace iv::vk::shaders

namespace iv::vk {

// Create a shader module from embedded SPIR-V (4-byte aligned bytes, size in
// bytes). The void* hop keeps the uint32 view alignment-clean (-Wcast-align).
[[nodiscard]] Result<Unique<::vk::ShaderModule>> makeShaderModule(::vk::Device device,
                                                                  const unsigned char* spirv,
                                                                  ::std::size_t sizeBytes);

} // namespace iv::vk

#endif // IV_VK_SHADERS_HPP
