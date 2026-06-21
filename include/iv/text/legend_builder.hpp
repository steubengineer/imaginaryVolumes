#ifndef IV_TEXT_LEGEND_BUILDER_HPP
#define IV_TEXT_LEGEND_BUILDER_HPP

// Legend builder (ADR-0028): project an iv::LegendSpec into an iv::vk::Overlay — the 2-D
// phase x magnitude swatch + border + ticks go into the overlay's SCREEN-SPACE channels
// (screenTriangles / screenLines), and the labels (the -pi/0/pi phase ticks, the nice-number
// magnitude tick values, the axis captions) into the glyphs. Lives in the text/annotation
// layer because labels need a Shaper. The swatch colors/alphas are produced ONLY through the
// host transfer evaluators (iv::phaseColor / iv::transferNormalized / iv::transferOpacity), so
// the legend cannot drift from the render. APPENDS to the overlay (never clears), so a caller
// may buildAnnotations() first then buildLegend() into the same Overlay sharing one Shaper.

#include "iv/legend.hpp"
#include "iv/text/shaper.hpp"
#include "iv/vk/renderer.hpp" // iv::vk::Overlay

#include <cstdint>

namespace iv::text {

void buildLegend(iv::vk::Overlay& overlay, const iv::LegendSpec& spec, std::uint32_t fbWidth,
                 std::uint32_t fbHeight, Shaper& shaper);

} // namespace iv::text

#endif // IV_TEXT_LEGEND_BUILDER_HPP
