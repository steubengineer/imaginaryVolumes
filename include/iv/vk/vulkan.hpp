#ifndef IV_VK_VULKAN_HPP
#define IV_VK_VULKAN_HPP

// Centralized Vulkan-Hpp include (ADR-0004). Every TU that touches Vulkan
// includes THIS header — never <vulkan/vulkan.hpp> directly — so the macro
// configuration below is uniform across the program (ODR-safe).
//
// - VULKAN_HPP_NO_EXCEPTIONS: entry points return vk::Result / vk::ResultValue<T>
//   instead of throwing, matching our exception-free error model (ADR-0003).
// - VULKAN_HPP_ASSERT_ON_RESULT -> no-op: we check every result explicitly via
//   the boundary helpers in iv/vk/result.hpp.
// - Default static dispatch (no dynamic dispatcher): M2 uses only core (<= 1.3)
//   functionality exported by the loader; the few extension procs we need
//   (debug-utils) are loaded by hand (ADR-0004).

#define VULKAN_HPP_NO_EXCEPTIONS

#ifndef VULKAN_HPP_ASSERT_ON_RESULT
#define VULKAN_HPP_ASSERT_ON_RESULT(expr) ((void)0)
#endif

#include <vulkan/vulkan.hpp>

#endif // IV_VK_VULKAN_HPP
