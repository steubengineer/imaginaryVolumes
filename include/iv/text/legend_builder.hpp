#ifndef IV_TEXT_LEGEND_BUILDER_HPP
#define IV_TEXT_LEGEND_BUILDER_HPP

// Legend builder (ADR-0028): project an iv::LegendSpec into an iv::vk::Overlay — the 2-D
// phase x magnitude swatch + border + ticks go into the overlay's SCREEN-SPACE channels
// (screenTriangles / screenLines), and the labels (the -pi/0/pi phase ticks, the nice-number
// magnitude tick values, the axis captions) are appended to the mixed-font builder `glyphs` as
// math-aware labels (ADR-0033: the captions may carry inline `$…$` math — the default field name
// is `$f$`). Lives in the text/annotation layer because labels need fonts. The swatch
// colors/alphas are produced ONLY through the host transfer evaluators (iv::phaseColor /
// iv::transferNormalized / iv::transferOpacity), so the legend cannot drift from the render.
// APPENDS to the overlay (never clears), so a caller may buildAnnotations() first then
// buildLegend() into the same Overlay + `glyphs`, then call glyphs.finish(overlay) once.

#include "iv/legend.hpp"
#include "iv/text/text_layout.hpp" // iv::text::MixedGlyphs
#include "iv/vk/renderer.hpp"      // iv::vk::Overlay, RenderParams

#include <cstdint>
#include <string>

namespace iv::text {

void buildLegend(iv::vk::Overlay& overlay, MixedGlyphs& glyphs, const iv::LegendSpec& spec,
                 std::uint32_t fbWidth, std::uint32_t fbHeight);

// Magnitude-axis tick-label formatting for the legend (B-0016), promoted to inline `$…$` math so
// the M9 layout (ADR-0033) typesets true superscript exponents instead of ASCII "1e-3". Pure host
// string policy — public so it is unit-testable independent of glyph emission.
//   decadeTickLabel: a log-mode power-of-ten tick -> "1" for 10^0, else "$10^{e}$" (a pure power;
//     the mantissa is always 1 on a decade axis, so it is elided).
//   linearTickLabel: a linear-mode tick over [0, axisMax]. Ordinary ranges keep plain decimals
//     (iv::formatTick); only very small or very large axes (|axisMax| < 1e-2 or >= 1e4) switch to a
//     SHARED-exponent scientific form "$m\times10^{exp}$" (exp = floor(log10|axisMax|)), so ticks
//     read e.g. 4×10⁻³ / 2×10⁻³ rather than 0.004 / 0.002; zero stays "0". (maintainer 2026-07-03.)
[[nodiscard]] std::string decadeTickLabel(int exponent);
[[nodiscard]] std::string linearTickLabel(double value, double step, double axisMax);

// Horizontal room (px) placeLegendRight must reserve to the right of the swatch for the magnitude
// value labels: the widest label buildLegend will draw (at the base tick size) plus the tick stub
// and swatch->label gap. Content-aware because the B-0016 sci labels vary a lot in width (a decade
// "10⁻⁴" is narrow, a linear "2.5×10⁻³" is wide); a fixed reserve either clips or wastes space.
[[nodiscard]] float magnitudeValueReservePx(const iv::LegendSpec& spec, MixedGlyphs& glyphs);

// Place the legend swatch just right of the projected volume box so it does not collide with the
// plot, regardless of window aspect (ADR-0034). Projects the 8 unit-cube corners with the same
// view-projection the annotations use (ADR-0012/0026), and sets spec.rectNdc's left/right (the
// swatch WIDTH and vertical extent are preserved) so the swatch clears the box's right NDC extent
// — clamped to a no-op for wide windows and to keep the right-edge value labels on screen. A
// degenerate/behind-camera projection leaves spec.rectNdc unchanged. Call before buildLegend; the
// facade calls it per frame (live camera) so the legend tracks orbit/resize. `valueReservePx` is the
// right-edge room for the magnitude value labels (default: a fixed fallback; the facade passes the
// content-aware magnitudeValueReservePx so wide B-0016 sci labels stay on screen).
void placeLegendRight(iv::LegendSpec& spec, const iv::vk::RenderParams& camera,
                      std::uint32_t fbWidth, std::uint32_t fbHeight, float valueReservePx = 46.0f);

} // namespace iv::text

#endif // IV_TEXT_LEGEND_BUILDER_HPP
