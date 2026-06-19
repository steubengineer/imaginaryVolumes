#include "iv/error.hpp"

namespace iv {

std::string_view to_string(Errc code) noexcept {
    switch (code) {
    case Errc::invalid_argument:
        return "invalid_argument";
    case Errc::unsupported_configuration:
        return "unsupported_configuration";
    case Errc::device_unavailable:
        return "device_unavailable";
    case Errc::allocation_failed:
        return "allocation_failed";
    case Errc::internal:
        return "internal";
    }
    // Unreachable for a valid Errc; present to satisfy control flow for an
    // out-of-range value produced by, e.g., a cast.
    return "unknown";
}

std::string format(const Error& error) {
    std::string out{to_string(error.code)};
    if (!error.message.empty()) {
        out += ": ";
        out += error.message;
    }
    return out;
}

std::unexpected<Error> make_error(Errc code, std::string message) {
    return std::unexpected<Error>(Error{code, std::move(message)});
}

} // namespace iv
