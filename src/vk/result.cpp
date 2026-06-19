#include "iv/vk/result.hpp"

namespace iv::vk {

Errc to_errc(::vk::Result result) noexcept {
    switch (result) {
    case ::vk::Result::eErrorOutOfHostMemory:
    case ::vk::Result::eErrorOutOfDeviceMemory:
        return Errc::allocation_failed;
    case ::vk::Result::eErrorDeviceLost:
    case ::vk::Result::eErrorInitializationFailed:
        return Errc::device_unavailable;
    case ::vk::Result::eErrorExtensionNotPresent:
    case ::vk::Result::eErrorFeatureNotPresent:
    case ::vk::Result::eErrorLayerNotPresent:
    case ::vk::Result::eErrorFormatNotSupported:
        return Errc::unsupported_configuration;
    default:
        return Errc::internal;
    }
}

Status check(::vk::Result result, std::string_view context) {
    if (result != ::vk::Result::eSuccess) {
        return make_error(to_errc(result),
                          std::string(context) + ": " + ::vk::to_string(result));
    }
    return Status{};
}

} // namespace iv::vk
