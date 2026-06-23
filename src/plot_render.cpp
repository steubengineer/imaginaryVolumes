// Headless plot facade (ADR-0029): renderPlot(). Builds a transient Context + Renderer +
// Volume + Shaper, composites the box/axes (ADR-0026) and legend (ADR-0028) into one Overlay
// from a single source of transfer state, renders, and returns the readback. Needs the text
// layer (labels) but NOT GLFW, so it is compiled into iv_text.

#include "iv/plot.hpp"

#include "iv/text/annotations.hpp"
#include "iv/text/font_set.hpp"
#include "iv/text/legend_builder.hpp"
#include "iv/text/text_layout.hpp" // MixedGlyphs
#include "iv/vk/context.hpp"
#include "iv/vk/renderer.hpp"
#include "iv/vk/volume.hpp"

#include <array>
#include <cmath>

namespace iv {

namespace {

// Orbit distances at which the facade frames the unit cube. Without a legend the cube fills ~75%
// of the full frame (room for labels, B-0013/D-0049). With a legend (ADR-0035) the plot is shifted
// into the left 75% and framed a touch smaller so the box AND its left axis labels fit there
// without clipping; the legend takes the right 25%. Facade-local (no global default change); kept
// in step with makePlot (plot_make.cpp).
constexpr float kPlotFrameDistance = 3.3f;
constexpr float kPlotFrameDistanceLegend = 4.0f;
// Image shift (NDC, ADR-0035) that centers the plot in the left 75% of the frame: splitFrac − 1.
constexpr float kLegendShiftNdcX = -0.25f; // = 0.75 − 1

// Pull the camera back along its view direction to `distance` from the target. Preserves the view
// angle and target.
void frameUnitCube(iv::vk::RenderParams& p, float distance) {
    const std::array<float, 3> dir{p.eye[0] - p.target[0], p.eye[1] - p.target[1],
                                   p.eye[2] - p.target[2]};
    const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len <= 0.0f) {
        return;
    }
    const float s = distance / len;
    p.eye = {p.target[0] + dir[0] * s, p.target[1] + dir[1] * s, p.target[2] + dir[2] * s};
}

// Fan the single-source transfer state out to the RenderParams (ADR-0029).
void applyTransfer(iv::vk::RenderParams& p, const PlotOptions& o) {
    p.colormapMode = o.colormapMode;
    p.opacityMode = o.opacityMode;
    p.densityScale = o.densityScale;
    p.logDecades = o.logDecades;
    p.legendThickness = o.legendThickness; // render-inert; carried for consistency (ADR-0030)
    p.background = o.background;
}

// ... and to the LegendSpec, so the legend matches the render (ADR-0028).
iv::LegendSpec legendFor(const PlotOptions& o, iv::MagnitudeRange range) {
    iv::LegendSpec ls;
    ls.range = range;
    ls.colormapMode = o.colormapMode;
    ls.opacityMode = o.opacityMode;
    ls.densityScale = o.densityScale;
    ls.logDecades = o.logDecades;
    ls.referenceThickness = o.legendThickness; // ADR-0030
    ls.fieldName = o.fieldName;                 // ADR-0031
    ls.magnitudeLabel = o.magnitudeLabel;
    ls.phaseLabel = o.phaseLabel;
    return ls;
}

template <class T>
Result<iv::vk::ImageReadback> renderPlotImpl(std::span<const std::complex<T>> field, GridDims dims,
                                             std::uint32_t width, std::uint32_t height,
                                             const PlotOptions& options) {
    auto ctx = iv::vk::Context::create();
    if (!ctx) {
        return std::unexpected(std::move(ctx).error());
    }
    auto rend = iv::vk::Renderer::create(*ctx);
    if (!rend) {
        return std::unexpected(std::move(rend).error());
    }
    iv::VolumeOptions vo;
    vo.magnitudeRange = options.magnitudeRange;
    auto vol = iv::vk::Volume::create(*ctx, field, dims, vo);
    if (!vol) {
        return std::unexpected(std::move(vol).error());
    }
    auto fonts = iv::text::FontSet::create(options.labelPixelSize);
    if (!fonts) {
        return std::unexpected(std::move(fonts).error());
    }

    iv::vk::RenderParams p;
    applyTransfer(p, options);
    // Frame the plot: with a legend, shift it into the left 75% and frame a touch smaller so the
    // legend has the right 25% (ADR-0035); otherwise fill the frame (B-0013/D-0049).
    if (options.showLegend) {
        p.imageShiftNdcX = kLegendShiftNdcX;
        frameUnitCube(p, kPlotFrameDistanceLegend);
    } else {
        frameUnitCube(p, kPlotFrameDistance);
    }

    // One mixed-font glyph builder spans the annotations + legend labels; finish() merges the
    // roman/italic/math atlases once (ADR-0032/0033). Labels may carry inline `$…$` math.
    iv::vk::Overlay ov;
    iv::text::MixedGlyphs glyphs(*fonts);
    iv::text::buildAnnotations(ov, glyphs, options.axes, p, width, height);
    if (options.showLegend) {
        iv::LegendSpec ls = legendFor(options, vol->magnitudeRange());
        iv::text::placeLegendRight(ls, p, width, height); // clear the box, aspect-aware (ADR-0034)
        iv::text::buildLegend(ov, glyphs, ls, width, height);
    }
    glyphs.finish(ov);
    return rend->render(*vol, width, height, p, &ov);
}

} // namespace

Result<iv::vk::ImageReadback> renderPlot(std::span<const std::complex<float>> field, GridDims dims,
                                         std::uint32_t width, std::uint32_t height,
                                         const PlotOptions& options) {
    return renderPlotImpl<float>(field, dims, width, height, options);
}

Result<iv::vk::ImageReadback> renderPlot(std::span<const std::complex<double>> field, GridDims dims,
                                         std::uint32_t width, std::uint32_t height,
                                         const PlotOptions& options) {
    return renderPlotImpl<double>(field, dims, width, height, options);
}

} // namespace iv
