#ifndef IV_TEXT_SHAPER_HPP
#define IV_TEXT_SHAPER_HPP

// Unicode text shaping (ADR-0022): turn a UTF-8 string + font + pixel size into
// positioned glyphs, using vendored HarfBuzz. HarfBuzz is fully hidden behind
// this boundary (ADR-0004) — no HarfBuzz type appears in this header. Shaping
// resolves OpenType features (ligatures, kerning, complex scripts), so the
// output is NOT a 1:1 codepoint→glyph map.
//
// Usage:
//   auto shaper = iv::text::Shaper::create(iv::text::bundledFont(), 48.0f);
//   for (const auto& g : shaper->shape("Volume |z|")) { ... g.glyphId, g.xAdvance ... }
//
// Single-threaded per instance (ADR-0007): one Shaper per thread.

#include "iv/error.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace iv::text {

// One positioned glyph from shaping. `glyphId` indexes the font's glyphs (a
// shaped glyph, e.g. a ligature — NOT a Unicode codepoint). Advances/offsets are
// in pixels at the Shaper's configured size. `cluster` is the byte offset into
// the input UTF-8 that produced this glyph (monotonic for LTR text), for
// hit-testing and selection.
struct ShapedGlyph {
    std::uint32_t glyphId{};
    std::uint32_t cluster{};
    float xAdvance{}; // pen advance after this glyph, pixels
    float yAdvance{};
    float xOffset{};  // glyph origin offset from the pen, pixels
    float yOffset{};
};

// Shapes UTF-8 text for one font at one pixel size. Move-only; owns the parsed
// font. The glyph ids it emits index THIS font (consumed by the glyph renderer,
// ADR-0023).
class Shaper {
public:
    // Parse an in-memory OpenType/TrueType font and configure it at `pixelSizePx`
    // (the em square in pixels; advances scale with it). The bytes are referenced
    // for the Shaper's lifetime, so they must outlive it (the bundled font and
    // string literals do); pass a copy otherwise. Fails with invalid_argument if
    // the font cannot be parsed or pixelSizePx <= 0.
    [[nodiscard]] static Result<Shaper> create(std::span<const std::byte> fontBytes,
                                               float pixelSizePx);
    // As create(), reading the font from a file on disk.
    [[nodiscard]] static Result<Shaper> createFromFile(const std::string& path,
                                                       float pixelSizePx);

    Shaper(const Shaper&) = delete;
    Shaper& operator=(const Shaper&) = delete;
    Shaper(Shaper&&) noexcept;
    Shaper& operator=(Shaper&&) noexcept;
    ~Shaper();

    // Shape one run of UTF-8 text into positioned glyphs (auto-detected
    // script/direction). Returns an empty vector for empty input. Allocation
    // failure propagates as std::bad_alloc (fatal, ADR-0003).
    [[nodiscard]] std::vector<ShapedGlyph> shape(std::string_view utf8) const;

    // The configured pixel size (em square, pixels) and the font's units-per-em.
    [[nodiscard]] float pixelSize() const noexcept;
    [[nodiscard]] std::uint32_t unitsPerEm() const noexcept;

private:
    Shaper() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace iv::text

#endif // IV_TEXT_SHAPER_HPP
