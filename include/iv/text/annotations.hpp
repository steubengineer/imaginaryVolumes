#ifndef IV_TEXT_ANNOTATIONS_HPP
#define IV_TEXT_ANNOTATIONS_HPP

// Plot annotation builder (ADR-0026): project the declarative model (ADR-0024) into
// an iv::vk::Overlay for the renderer — world-space bounding box / tick / through-axis
// lines (overlay.transform = the ADR-0012 view-projection) and screen-space glyph
// labels (major tick values, axis labels, title). Lives in the text/annotation layer
// because labels need a Shaper. The renderer draws the resulting Overlay identically
// headless (render()) and in the viewer (recordFrame(), ADR-0025).

#include "iv/plot_axes.hpp"
#include "iv/text/text_layout.hpp" // iv::text::MixedGlyphs (mixed-font label glyphs)
#include "iv/vk/renderer.hpp"      // iv::vk::Overlay, RenderParams

#include <cstdint>

namespace iv::text {

// Build the annotations for `axes` under `camera` into `overlay` (cleared first),
// targeting a `fbWidth` x `fbHeight` framebuffer. Honors every PlotAxes visibility
// toggle. Box/tick/axis lines are world-space (projected by overlay.transform); labels
// (title, axis labels, tick values) are appended to `glyphs` as mixed-font, math-aware
// runs (ADR-0033: a label may carry inline `$…$` math). Tick labels sit on the box
// silhouette, offset outward, so they never overlap the data (ADR-0026). The caller
// appends any further text (e.g. buildLegend) into the SAME `glyphs`, then calls
// glyphs.finish(overlay) once to merge the atlases (ADR-0032).
void buildAnnotations(iv::vk::Overlay& overlay, MixedGlyphs& glyphs, const iv::PlotAxes& axes,
                      const iv::vk::RenderParams& camera, std::uint32_t fbWidth,
                      std::uint32_t fbHeight);

} // namespace iv::text

#endif // IV_TEXT_ANNOTATIONS_HPP
