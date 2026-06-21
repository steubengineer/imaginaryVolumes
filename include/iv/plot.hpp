#ifndef IV_PLOT_HPP
#define IV_PLOT_HPP

// High-level plot facade (ADR-0029): one call to turn a complex field + dims + options into a
// fully labeled plot — interactive (makePlot returns a configured iv::vk::Viewer the caller
// runs) or headless (renderPlot returns a labeled image). Packages Volume + Renderer/Viewer +
// PlotAxes (ADR-0024/0026) + the legend (ADR-0028) behind PlotOptions, whose transfer state is
// the SINGLE source for both the render and the legend (so they cannot disagree).
//
// NOT part of core `iv`: renderPlot needs the text layer (labels); makePlot needs the viewer
// (GLFW) and text. Core iv / tests / iv_bench keep building with both gates OFF. renderPlot is
// linked from iv_text; makePlot from iv_plot (needs the viewer + text).

#include "iv/error.hpp"
#include "iv/legend.hpp"
#include "iv/plot_axes.hpp"
#include "iv/volume.hpp"
#include "iv/vk/offscreen.hpp" // ImageReadback (ADR-0006)
#include "iv/vk/renderer.hpp"  // RenderParams
#include "iv/vk/viewer.hpp"    // iv::vk::Viewer (GLFW-free header)

#include <array>
#include <complex>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace iv {

// One options aggregate for the whole plot. The transfer state (colormap/opacity/density/
// decades) is held ONCE here and fanned out by the facade to both the RenderParams used to
// render and the LegendSpec, so the legend always matches the image (ADR-0028/0029).
struct PlotOptions {
    PlotAxes axes{};                     // box / ticks / labels / title (ADR-0024)
    bool showLegend{true};               // the phase x magnitude legend (ADR-0028)
    std::string magnitudeLabel{"|z|"};
    std::string phaseLabel{"arg z"};

    std::uint32_t colormapMode{0};       // ADR-0014: 0 = twilight LUT, 1 = HSV
    std::uint32_t opacityMode{0};        // ADR-0013: 0 = linear, 1 = logarithmic
    float densityScale{1.0f};
    float logDecades{0.0f};              // ADR-0027 (0 = full range)
    std::array<float, 4> background{0.05f, 0.05f, 0.07f, 1.0f};

    std::optional<MagnitudeRange> magnitudeRange{}; // else the Volume's auto range (ADR-0010)
    float labelPixelSize{16.0f};         // Shaper size for tick labels (ADR-0026 scales the rest)

    std::uint32_t width{1000};           // window size (makePlot); renderPlot takes explicit w/h
    std::uint32_t height{1000};
    const char* windowTitle{"imaginaryVolumes"}; // window caption (axes.title is the plot title)
};

// Interactive: build the window/Context/Volume, set params from `options`, install a per-frame
// callback that rebuilds the box/axes (ADR-0026) and legend (ADR-0028) from the LIVE
// RenderParams, and return the configured Viewer WITHOUT running it. The caller does v->run()
// (or runFrames) and may edit v->params()/camera() first. Requires the viewer + text builds.
[[nodiscard]] Result<iv::vk::Viewer> makePlot(std::span<const std::complex<float>> field,
                                              GridDims dims, const PlotOptions& options = {});
[[nodiscard]] Result<iv::vk::Viewer> makePlot(std::span<const std::complex<double>> field,
                                              GridDims dims, const PlotOptions& options = {});

// Headless: render one fully labeled image (box/axes + legend) of width x height and read it
// back (ADR-0006 layout: top-left origin, row-major, RGBA8). No window. Requires the text build.
[[nodiscard]] Result<iv::vk::ImageReadback> renderPlot(std::span<const std::complex<float>> field,
                                                       GridDims dims, std::uint32_t width,
                                                       std::uint32_t height,
                                                       const PlotOptions& options = {});
[[nodiscard]] Result<iv::vk::ImageReadback> renderPlot(std::span<const std::complex<double>> field,
                                                       GridDims dims, std::uint32_t width,
                                                       std::uint32_t height,
                                                       const PlotOptions& options = {});

} // namespace iv

#endif // IV_PLOT_HPP
