#ifndef IV_VK_RESULT_HPP
#define IV_VK_RESULT_HPP

// Boundary adapter between Vulkan results and our error model (ADR-0004).
// vk::Result / VkResult never crosses past these helpers into consumer-facing
// signatures (§8.5).

#include "iv/error.hpp"
#include "iv/vk/vulkan.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace iv::vk {

// Map a Vulkan result code to an iv::Errc (ADR-0004 table).
[[nodiscard]] Errc to_errc(::vk::Result result) noexcept;

// eSuccess -> ok Status; otherwise a mapped Error carrying "context: <vkResult>".
[[nodiscard]] Status check(::vk::Result result, std::string_view context);

// Convert a Vulkan-Hpp vk::ResultValue<T> into our Result<T>.
template <class T>
[[nodiscard]] Result<T> take(::vk::ResultValue<T> rv, std::string_view context) {
    if (rv.result != ::vk::Result::eSuccess) {
        return make_error(to_errc(rv.result),
                          std::string(context) + ": " + ::vk::to_string(rv.result));
    }
    return Result<T>{std::move(rv.value)};
}

} // namespace iv::vk

#endif // IV_VK_RESULT_HPP
