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

} // namespace iv::text::math

#endif // IV_TEXT_MATH_LAYOUT_HPP
