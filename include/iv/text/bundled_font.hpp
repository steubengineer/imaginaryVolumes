#ifndef IV_TEXT_BUNDLED_FONT_HPP
#define IV_TEXT_BUNDLED_FONT_HPP

// The default label font, embedded into iv_text at build time (ADR-0022). The
// New Computer Modern "Book" Roman face (NCM 8.1.0, GUST Font License) is turned
// into a byte array by tools/embed_bytes.cmake — the matching definitions live in
// the generated ncm_book_otf.cpp — so the default font needs no runtime file path
// (consistent with the embedded shaders / colormap LUT). See third_party/fonts/.

#include <cstddef>
#include <span>

namespace iv::text {

// Raw bytes of third_party/fonts/NewCM10-Book.otf (generated; do not reference
// these directly — use bundledFont()).
extern const unsigned char ncm_book_otf_data[];
extern const ::std::size_t ncm_book_otf_size;

// The bundled default face (New Computer Modern Book, OpenType) as raw bytes,
// suitable for Shaper::create(). Valid for the program's lifetime.
[[nodiscard]] ::std::span<const ::std::byte> bundledFont() noexcept;

} // namespace iv::text

#endif // IV_TEXT_BUNDLED_FONT_HPP
