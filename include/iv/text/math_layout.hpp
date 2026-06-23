#ifndef IV_TEXT_MATH_LAYOUT_HPP
#define IV_TEXT_MATH_LAYOUT_HPP

// OpenType-MATH box layout (ADR-0033 §3): turn a parsed math::List into positioned glyphs +
// rules. Every vertical/positioning constant comes from the math face's MATH table via the
// Shaper math API (no hardcoded TeX constants — the metrics-from-font invariant); glyphs are
// emitted through the mixed-font channel (ADR-0032, MixedGlyphs) and rules (fraction bars,
// radical vinculums, overlines) as screen-space filled rects on the overlay. HarfBuzz stays
// behind the Shaper boundary (ADR-0004).

#include "iv/text/font_set.hpp"
#include "iv/text/math.hpp"
#include "iv/text/text_layout.hpp" // MixedGlyphs
#include "iv/vk/renderer.hpp"      // iv::vk::Overlay

#include <array>
#include <cstdint>
#include <string_view>

namespace iv::text::math {

// Result of laying out a math list: the box metrics (px) relative to the baseline pen.
struct Metrics {
    float width{0.0f};
    float ascent{0.0f};  // above the baseline (>= 0)
    float descent{0.0f}; // below the baseline (>= 0)
};

// Lay out `list` (text style — inline labels) and emit it: glyph quads via `glyphs`, rules
// appended to `overlay.screenTriangles`, with the math drawn at `basePixelSize` px in `color`,
// the baseline origin at (penXpx, penYpx) in a top-left-origin (fbWidth x fbHeight) framebuffer.
// Returns the laid-out metrics (advance = .width). Call MixedGlyphs::finish() after to merge.
Metrics layout(MixedGlyphs& glyphs, iv::vk::Overlay& overlay, FontSet& fonts, const List& list,
               float penXpx, float penYpx, std::uint32_t fbWidth, std::uint32_t fbHeight,
               float basePixelSize, const std::array<float, 4>& color);

// Render a full label string (plain text with inline `$…$` math, ADR-0033) into `glyphs` +
// `overlay`: text spans go to the roman face (MixedGlyphs::appendRun), math spans through
// layout() above. The baseline origin is (penXpx, penYpx) at `pixelSize` px in `color`; returns
// the horizontal advance (px). A label with no `$` is the plain roman text path (so it is
// byte-identical to appendText — the backward-compat invariant). The caller finishes the
// MixedGlyphs once after all labels are appended.
float appendLabel(MixedGlyphs& glyphs, iv::vk::Overlay& overlay, std::string_view label,
                  float penXpx, float penYpx, std::uint32_t fbWidth, std::uint32_t fbHeight,
                  float pixelSize, const std::array<float, 4>& color);

// The width (px) `label` would occupy at `pixelSize`, without emitting anything (for placement).
[[nodiscard]] float measureLabel(FontSet& fonts, std::string_view label, float pixelSize);

// Emit `label` rotated by `angleRad` about the anchor (pivotXpx, pivotYpx) — the rotated baseline
// origin (ADR-0034). Lays the label out horizontally at the pivot (appendLabel), then rotates the
// emitted glyph quads in pixel space (MixedGlyphs::rotateSince). Returns the advance ALONG the
// rotated baseline (px) = the horizontal measureLabel width. For the legend magnitude caption,
// angleRad = −π/2 (reads bottom-to-top: the +x baseline advances upward). Glyph quads only —
// inline-math RULES (fraction bars / vinculums / overlines) are NOT rotated, so a rotated label
// must not contain them (captions never do).
float appendLabelRotated(MixedGlyphs& glyphs, iv::vk::Overlay& overlay, std::string_view label,
                         float pivotXpx, float pivotYpx, std::uint32_t fbWidth,
                         std::uint32_t fbHeight, float pixelSize,
                         const std::array<float, 4>& color, float angleRad);

} // namespace iv::text::math

#endif // IV_TEXT_MATH_LAYOUT_HPP
