// Interactive plot facade (ADR-0029): makePlot(). Builds the Viewer + Volume, sets params from
// options, and installs a per-frame callback that rebuilds the box/axes (ADR-0026) and legend
// (ADR-0028) from the LIVE RenderParams — so toggling colormap/opacity/density/decades updates
// the legend. Returns the configured (not-yet-running) Viewer. Needs the viewer (GLFW) + text,
// so it lives in iv_plot; the closure owns its Shaper/models (via a shared_ptr) to keep the
// returned Viewer text-agnostic.

#include "iv/plot.hpp"

#include "iv/text/annotations.hpp"
#include "iv/text/font_set.hpp"
#include "iv/text/legend_builder.hpp"
#include "iv/text/text_layout.hpp" // MixedGlyphs
#include "iv/vk/viewer.hpp"
#include "iv/vk/volume.hpp"

#include <memory>
#include <string>
#include <utility>

namespace iv {

namespace {

// Initial orbit distance that frames the unit cube with a margin for axis labels (B-0013).
// Facade-local; kept in step with renderPlot's kPlotFrameDistance (plot_render.cpp) so the
// interactive and headless plots frame identically. (Reset/`R` returns to the OrbitCamera
// default, ADR-0018.)
constexpr float kPlotFrameDistance = 3.3f;

// Rendering state owned by the Viewer's onFrame closure, kept in a shared_ptr so the closure
// stays copyable for std::function (a move-only captured Shaper would not). The transfer state
// is read LIVE from each frame's RenderParams, so hotkeys update the legend.
struct PlotState {
    iv::text::FontSet fonts;
    iv::PlotAxes axes;
    iv::MagnitudeRange range;
    std::string fieldName;
    std::string magnitudeLabel;
    std::string phaseLabel;
    bool showLegend;
};

template <class T>
Result<iv::vk::Viewer> makePlotImpl(std::span<const std::complex<T>> field, GridDims dims,
                                    const PlotOptions& options) {
    auto viewer = iv::vk::Viewer::create(
        iv::vk::Viewer::Options{options.width, options.height, options.windowTitle});
    if (!viewer) {
        return std::unexpected(std::move(viewer).error());
    }

    iv::VolumeOptions vo;
    vo.magnitudeRange = options.magnitudeRange;
    auto vol = iv::vk::Volume::create(viewer->context(), field, dims, vo);
    if (!vol) {
        return std::unexpected(std::move(vol).error());
    }
    const iv::MagnitudeRange range = vol->magnitudeRange();
    viewer->setVolume(std::move(*vol));

    viewer->camera().setDistance(kPlotFrameDistance); // frame with a label margin (B-0013)

    iv::vk::RenderParams& p = viewer->params();
    p.colormapMode = options.colormapMode;
    p.opacityMode = options.opacityMode;
    p.densityScale = options.densityScale;
    p.logDecades = options.logDecades;
    p.legendThickness = options.legendThickness; // ADR-0030 (the live [ / ] channel)
    p.background = options.background;

    auto fonts = iv::text::FontSet::create(options.labelPixelSize);
    if (!fonts) {
        return std::unexpected(std::move(fonts).error());
    }

    auto state = std::make_shared<PlotState>(PlotState{std::move(*fonts), options.axes, range,
                                                       options.fieldName, options.magnitudeLabel,
                                                       options.phaseLabel, options.showLegend});
    viewer->setOnFrame([state](iv::vk::Overlay& ov, const iv::vk::RenderParams& cam,
                               std::uint32_t w, std::uint32_t h) {
        // One mixed-font builder per frame spans annotations + legend; finish() merges the
        // roman/italic/math atlases once (ADR-0032). Labels may carry inline `$…$` math (ADR-0033).
        iv::text::MixedGlyphs glyphs(state->fonts);
        iv::text::buildAnnotations(ov, glyphs, state->axes, cam, w, h); // clears + box/axes
        if (state->showLegend) {
            iv::LegendSpec ls;
            ls.range = state->range;
            ls.colormapMode = cam.colormapMode; // live transfer state
            ls.opacityMode = cam.opacityMode;
            ls.densityScale = cam.densityScale;
            ls.logDecades = cam.logDecades;
            ls.referenceThickness = cam.legendThickness; // live thickness (ADR-0030)
            ls.fieldName = state->fieldName;              // ADR-0031
            ls.magnitudeLabel = state->magnitudeLabel;
            ls.phaseLabel = state->phaseLabel;
            iv::text::placeLegendRight(ls, cam, w, h);   // clear the box, live aspect (ADR-0034)
            iv::text::buildLegend(ov, glyphs, ls, w, h); // appends the screen legend
        }
        glyphs.finish(ov); // merge the glyph atlases into the overlay
    });
    return viewer;
}

} // namespace

Result<iv::vk::Viewer> makePlot(std::span<const std::complex<float>> field, GridDims dims,
                                const PlotOptions& options) {
    return makePlotImpl<float>(field, dims, options);
}

Result<iv::vk::Viewer> makePlot(std::span<const std::complex<double>> field, GridDims dims,
                                const PlotOptions& options) {
    return makePlotImpl<double>(field, dims, options);
}

} // namespace iv
