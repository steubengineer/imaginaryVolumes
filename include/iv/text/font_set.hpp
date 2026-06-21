#ifndef IV_TEXT_FONT_SET_HPP
#define IV_TEXT_FONT_SET_HPP

// The bundled font faces for mixed-font text (ADR-0032): roman (text + upright math
// structure / \mathrm), true italic (math variables / the legend field name), and the
// OpenType MATH face (symbols, large operators, stretchy delimiters, and the MATH-table
// positioning constants the math layout reads via hb_ot_math_*). One Shaper per face, all
// at the same pixel size; their Slug glyph atlases are merged into one Overlay glyph
// channel by the glyph builder (text_layout, ADR-0032). HarfBuzz stays behind the Shaper
// (ADR-0004) — no HarfBuzz type appears here.

#include "iv/error.hpp"
#include "iv/text/shaper.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace iv::text {

// The faces a mixed-font build draws from. The numeric values are the merge order and the
// index into FontSet (Roman first, so existing roman-only text keeps atlas base 0).
enum class Face : std::uint8_t { Roman = 0, Italic = 1, Math = 2 };
inline constexpr std::size_t kFaceCount = 3;

// Owns the three bundled Shapers (one font + Slug atlas each), all sized identically.
// Move-only (each Shaper owns its font). Single-threaded per instance (ADR-0007).
class FontSet {
public:
    // Create the roman/italic/math shapers from the bundled faces at `pixelSizePx`.
    // Fails (invalid_argument) if pixelSizePx <= 0 or any bundled face won't parse.
    [[nodiscard]] static Result<FontSet> create(float pixelSizePx);

    FontSet(const FontSet&) = delete;
    FontSet& operator=(const FontSet&) = delete;
    FontSet(FontSet&&) noexcept = default;
    FontSet& operator=(FontSet&&) noexcept = default;
    ~FontSet() = default;

    [[nodiscard]] Shaper& shaper(Face f) noexcept {
        return shapers_[static_cast<std::size_t>(f)];
    }
    [[nodiscard]] const Shaper& shaper(Face f) const noexcept {
        return shapers_[static_cast<std::size_t>(f)];
    }
    // The common pixel size (em square, px) all faces are shaped at.
    [[nodiscard]] float pixelSize() const noexcept { return shaper(Face::Roman).pixelSize(); }

private:
    explicit FontSet(std::array<Shaper, kFaceCount> shapers) : shapers_(std::move(shapers)) {}
    std::array<Shaper, kFaceCount> shapers_;
};

} // namespace iv::text

#endif // IV_TEXT_FONT_SET_HPP
