// Interactive plot facade (ADR-0029): makePlot(). Builds the Viewer + Volume, sets params from
// options, and installs a per-frame callback that rebuilds the box/axes (ADR-0026) and legend
// (ADR-0028) from the LIVE RenderParams — so toggling colormap/opacity/density/decades updates
// the legend. Returns the configured (not-yet-running) Viewer. Needs the viewer (GLFW) + text,
// so it lives in iv_plot; the closure owns its Shaper/models (via a shared_ptr) to keep the
// returned Viewer text-agnostic.

#include "iv/plot.hpp"

#include "iv/text/annotations.hpp"
#include "iv/text/bundled_font.hpp"
#include "iv/text/legend_builder.hpp"
#include "iv/text/shaper.hpp"
#include "iv/vk/viewer.hpp"
#include "iv/vk/volume.hpp"

#include <memory>
#include <string>
#include <utility>

namespace iv {

namespace {

// Rendering state owned by the Viewer's onFrame closure, kept in a shared_ptr so the closure
// stays copyable for std::function (a move-only captured Shaper would not). The transfer state
// is read LIVE from each frame's RenderParams, so hotkeys update the legend.
struct PlotState {
    iv::text::Shaper shaper;
    iv::PlotAxes axes;
    iv::MagnitudeRange range;
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

    iv::vk::RenderParams& p = viewer->params();
    p.colormapMode = options.colormapMode;
    p.opacityMode = options.opacityMode;
    p.densityScale = options.densityScale;
    p.logDecades = options.logDecades;
    p.background = options.background;

    auto shaper = iv::text::Shaper::create(iv::text::bundledFont(), options.labelPixelSize);
    if (!shaper) {
        return std::unexpected(std::move(shaper).error());
    }

    auto state = std::make_shared<PlotState>(PlotState{std::move(*shaper), options.axes, range,
                                                       options.magnitudeLabel, options.phaseLabel,
                                                       options.showLegend});
    viewer->setOnFrame([state](iv::vk::Overlay& ov, const iv::vk::RenderParams& cam,
                               std::uint32_t w, std::uint32_t h) {
        iv::text::buildAnnotations(ov, state->axes, cam, w, h, state->shaper); // clears + box/axes
        if (state->showLegend) {
            iv::LegendSpec ls;
            ls.range = state->range;
            ls.colormapMode = cam.colormapMode; // live transfer state
            ls.opacityMode = cam.opacityMode;
            ls.densityScale = cam.densityScale;
            ls.logDecades = cam.logDecades;
            ls.magnitudeLabel = state->magnitudeLabel;
            ls.phaseLabel = state->phaseLabel;
            iv::text::buildLegend(ov, ls, w, h, state->shaper); // appends the screen legend
        }
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
