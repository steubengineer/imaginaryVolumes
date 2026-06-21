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

namespace iv {

namespace {

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

    // One mixed-font glyph builder spans the annotations + legend labels; finish() merges the
    // roman/italic/math atlases once (ADR-0032/0033). Labels may carry inline `$…$` math.
    iv::vk::Overlay ov;
    iv::text::MixedGlyphs glyphs(*fonts);
    iv::text::buildAnnotations(ov, glyphs, options.axes, p, width, height);
    if (options.showLegend) {
        const iv::LegendSpec ls = legendFor(options, vol->magnitudeRange());
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
