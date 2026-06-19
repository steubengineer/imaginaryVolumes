#ifndef IV_ERROR_HPP
#define IV_ERROR_HPP

// Recoverable-failure types (ADR-0003). Recoverable runtime failures are
// reported by value via Result<T>/Status; the library throws no exceptions of
// its own (only std::bad_alloc may propagate, as fatal). Contract violations
// are handled by IV_ASSERT (see assert.hpp), not by these types.

#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace iv {

// Error codes. APPEND-ONLY (ADR-0003): never renumber or remove a value; new
// codes are appended at the end.
enum class Errc {
    invalid_argument,
    unsupported_configuration,
    device_unavailable,
    allocation_failed,
    internal,
};

// Stable, lowercase name of a code (e.g. "invalid_argument").
[[nodiscard]] std::string_view to_string(Errc code) noexcept;

struct Error {
    Errc code;
    std::string message;
};

// Renders "code", or "code: message" when a message is present.
[[nodiscard]] std::string format(const Error& error);

template <class T>
using Result = std::expected<T, Error>;
using Status = std::expected<void, Error>;

// Builds an unexpected Error usable as the error state of any Result<T>/Status:
//   return iv::make_error(iv::Errc::invalid_argument, "nx must be > 0");
[[nodiscard]] std::unexpected<Error> make_error(Errc code,
                                                std::string message = {});

} // namespace iv

#endif // IV_ERROR_HPP
