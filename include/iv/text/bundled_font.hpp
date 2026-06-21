#ifndef IV_TEXT_BUNDLED_FONT_HPP
#define IV_TEXT_BUNDLED_FONT_HPP

// The bundled label fonts, embedded into iv_text at build time (ADR-0022/0032). The
// New Computer Modern faces (NCM 8.1.0, GUST Font License) are turned into byte arrays
// by tools/embed_bytes.cmake — the matching definitions live in the generated
// ncm_*_otf.cpp — so the default fonts need no runtime file path (consistent with the
// embedded shaders / colormap LUT). See third_party/fonts/. Three faces:
//   - Roman  (NewCM10-Book)       — text + upright math structure   → bundledFont()
//   - Italic (NewCM10-BookItalic) — math variables / field name     → bundledFontItalic()
//   - Math   (NewCMMath-Book)     — OpenType MATH (symbols, layout)  → bundledFontMath()
// The italic + math faces back the M9 mixed-font glyph substrate (ADR-0032).

#include <cstddef>
#include <span>

namespace iv::text {

// Raw bytes of the embedded faces (generated; do not reference these directly — use the
// accessors below).
extern const unsigned char ncm_book_otf_data[];
extern const ::std::size_t ncm_book_otf_size;
extern const unsigned char ncm_bookitalic_otf_data[];
extern const ::std::size_t ncm_bookitalic_otf_size;
extern const unsigned char ncmmath_book_otf_data[];
extern const ::std::size_t ncmmath_book_otf_size;

// The bundled faces (New Computer Modern, OpenType) as raw bytes, each suitable for
// Shaper::create(). Valid for the program's lifetime.
[[nodiscard]] ::std::span<const ::std::byte> bundledFont() noexcept;       // Roman (Book)
[[nodiscard]] ::std::span<const ::std::byte> bundledFontItalic() noexcept; // BookItalic
[[nodiscard]] ::std::span<const ::std::byte> bundledFontMath() noexcept;   // Math (Book)

} // namespace iv::text

#endif // IV_TEXT_BUNDLED_FONT_HPP
