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

// Em-space ink bounding box of a glyph (in font units, y-up: minY is the bottom).
struct GlyphExtents {
    float minX{};
    float minY{};
    float maxX{};
    float maxY{};
    [[nodiscard]] bool empty() const noexcept { return maxX <= minX || maxY <= minY; }
};

// A glyph encoded into the Shaper's GPU glyph atlas (ADR-0023, Slug /
// libharfbuzz-gpu): its texel offset into glyphAtlas() and its em-space extents.
// `blank` glyphs (whitespace, .notdef with no outline) need no quad.
struct EncodedGlyph {
    std::uint32_t atlasOffset{};
    GlyphExtents extents{};
    bool blank{true};
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

    // ADR-0023 (GPU glyph rendering). The Shaper owns the font, so it also serves
    // glyph outlines for the Slug GPU renderer (libharfbuzz-gpu), behind the same
    // HarfBuzz boundary (ADR-0004).
    //
    // Encode `glyphId`'s outline into this Shaper's atlas (in FONT units, not
    // pixels — Slug quantizes coordinates, so font-unit coordinates keep
    // precision). Cached per glyphId; the returned reference is stable until the
    // Shaper is destroyed. Encoding never fails recoverably (a missing outline is
    // a `blank` glyph); allocation failure propagates as std::bad_alloc.
    [[nodiscard]] const EncodedGlyph& encodeGlyph(std::uint32_t glyphId);

    // The packed Slug atlas: a stream of int16, four per texel (an ivec4). Upload
    // as an R16G16B16A16_SINT uniform texel buffer; the Slug shader fetches texel i
    // (the unit of EncodedGlyph::atlasOffset) as ints [4i .. 4i+3]. Grows as glyphs
    // are encoded; the span is invalidated by the next encodeGlyph().
    [[nodiscard]] std::span<const std::int16_t> glyphAtlas() const noexcept;

    // --- Math metrics (ADR-0033): font-unit queries the OpenType-MATH layout draws through.
    // All lengths are in FONT (design) units — scale by pixelSizePx / unitsPerEm(). HarfBuzz
    // stays fully behind this boundary (ADR-0004): the enums below are ours, mapped internally.

    // The glyph index for a Unicode codepoint via the cmap (no shaping features); 0 = .notdef.
    [[nodiscard]] std::uint32_t glyphForCodepoint(char32_t cp) const noexcept;
    // Horizontal advance of a glyph, in font units.
    [[nodiscard]] float glyphAdvance(std::uint32_t glyphId) const noexcept;

    // True if the face carries an OpenType MATH table (the math face does; text faces don't).
    [[nodiscard]] bool hasMathTable() const noexcept;

    // The subset of OpenType MATH constants the layout needs (mirrors hb_ot_math_constant_t).
    // Values are font units, EXCEPT the two *PercentScaleDown entries (a 0–100 percentage —
    // use scriptScaleDown()). Returns 0 if the face lacks a MATH table.
    enum class MathConstant {
        axisHeight,
        fractionRuleThickness,
        fractionNumeratorShiftUp,
        fractionDenominatorShiftDown,
        fractionNumeratorGapMin,
        fractionDenominatorGapMin,
        superscriptShiftUp,
        subscriptShiftDown,
        superscriptBottomMin,
        subscriptTopMax,
        subSuperscriptGapMin,
        superscriptBaselineDropMax,
        subscriptBaselineDropMin,
        radicalRuleThickness,
        radicalVerticalGap,
        radicalExtraAscender,
        radicalKernBeforeDegree,
        radicalKernAfterDegree,
        radicalDegreeBottomRaisePercent, // a 0–100 percentage
        accentBaseHeight,
        overbarVerticalGap,
        overbarRuleThickness,
        overbarExtraAscender,
    };
    [[nodiscard]] float mathConstant(MathConstant c) const noexcept;

    // The script / scriptscript size factor (0–1); the MATH *PercentScaleDown constants over
    // 100, with sane CM defaults (0.7 / 0.5) if the face lacks the table.
    [[nodiscard]] float scriptScaleDown(bool scriptScript) const noexcept;

    // The MATH italic correction of a glyph, in font units (0 if none / no table).
    [[nodiscard]] float mathItalicCorrection(std::uint32_t glyphId) const noexcept;

    // The stretched glyph variant of a delimiter / radical surd sized to at least `minExtent`
    // font units along the given direction; returns `glyphId` unchanged if no larger variant
    // exists (extreme-size part assemblies are not built — the largest single variant is used).
    [[nodiscard]] std::uint32_t glyphVariant(std::uint32_t glyphId, bool vertical,
                                             float minExtent) const noexcept;

    // The horizontal attachment point (font units) for a top accent placed over `glyphId`
    // (defaults to the glyph's advance/2 when the table has no entry).
    [[nodiscard]] float mathTopAccentAttachment(std::uint32_t glyphId) const noexcept;

private:
    Shaper() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace iv::text

#endif // IV_TEXT_SHAPER_HPP
