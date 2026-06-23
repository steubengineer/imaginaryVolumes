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

// Outward offset (px), along the unit screen normal n=(nx,ny), from a box-edge midpoint to the
// axis-label center, chosen so the axis label clears the tick-label band instead of overlapping
// it (B-0013; an ADR-0026 placement refinement). Each label is modeled as an axis-aligned box of
// the given half-extents; its reach along n is the box support |nx|·halfW + |ny|·halfH, so the
// offset adapts to the edge's screen orientation (tick *width* dominates a vertical edge, tick
// *height* a horizontal one). Pure geometry, exposed for direct testing.
float axisLabelOutwardPx(float nx, float ny, float tickMargin, float tickHalfW, float tickHalfH,
                         float axisHalfW, float axisHalfH, float gap);

} // namespace iv::text

#endif // IV_TEXT_ANNOTATIONS_HPP
