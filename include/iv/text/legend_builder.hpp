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

namespace iv::text {

void buildLegend(iv::vk::Overlay& overlay, MixedGlyphs& glyphs, const iv::LegendSpec& spec,
                 std::uint32_t fbWidth, std::uint32_t fbHeight);

// Place the legend swatch just right of the projected volume box so it does not collide with the
// plot, regardless of window aspect (ADR-0034). Projects the 8 unit-cube corners with the same
// view-projection the annotations use (ADR-0012/0026), and sets spec.rectNdc's left/right (the
// swatch WIDTH and vertical extent are preserved) so the swatch clears the box's right NDC extent
// — clamped to a no-op for wide windows and to keep the right-edge value labels on screen. A
// degenerate/behind-camera projection leaves spec.rectNdc unchanged. Call before buildLegend; the
// facade calls it per frame (live camera) so the legend tracks orbit/resize.
void placeLegendRight(iv::LegendSpec& spec, const iv::vk::RenderParams& camera,
                      std::uint32_t fbWidth, std::uint32_t fbHeight);

} // namespace iv::text

#endif // IV_TEXT_LEGEND_BUILDER_HPP
