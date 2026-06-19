#include "iv/text/bundled_font.hpp"

namespace iv::text {

std::span<const std::byte> bundledFont() noexcept {
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(ncm_book_otf_data),
                                      ncm_book_otf_size);
}

} // namespace iv::text
