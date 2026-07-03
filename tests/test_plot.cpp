// High-level plot facade (ADR-0029): renderPlot headless. Teeth: a labeled image has overlay
// pixels (box/axes/legend) the bare volume lacks; and the legend matches the render — because
// PlotOptions is the single source of transfer state, a legend swatch pixel and a volume pixel
// of the same phase show the same hue. (makePlot is exercised by iv_view --frames under a
// display; it needs a swapchain so it is off the headless ctest gate.)

#include "iv/plot.hpp"

#include "catch_amalgamated.hpp"

#include <complex>
#include <cstdint>
#include <span>
#include <vector>

using iv::GridDims;

namespace {

std::vector<std::complex<float>> vortex(GridDims d) {
    std::vector<std::complex<float>> v(d.count());
    for (std::uint32_t z = 0; z < d.nz; ++z) {
        for (std::uint32_t y = 0; y < d.ny; ++y) {
            for (std::uint32_t x = 0; x < d.nx; ++x) {
                const float fx = (static_cast<float>(x) + 0.5f) / static_cast<float>(d.nx) - 0.5f;
                const float fy = (static_cast<float>(y) + 0.5f) / static_cast<float>(d.ny) - 0.5f;
                v[d.index(x, y, z)] = {fx, fy};
            }
        }
    }
    return v;
}

} // namespace

TEST_CASE("renderPlot: a labeled image adds overlay pixels over the bare volume", "[vk][plot]") {
    const GridDims d{16, 16, 16};
    const auto field = vortex(d);
    const std::uint32_t W = 200u;
    const std::uint32_t H = 200u;

    iv::PlotOptions opts;
    opts.colormapMode = 1;
    opts.axes.title = "test";
    auto labeled = iv::renderPlot(std::span<const std::complex<float>>(field), d, W, H, opts);
    REQUIRE(labeled.has_value());

    iv::PlotOptions bare = opts;
    bare.showLegend = false;
    bare.axes.boundingBox = false;
    bare.axes.boxTicks = false;
    bare.axes.tickLabels = false;
    bare.axes.axisLabels = false;
    bare.axes.showTitle = false;
    auto plain = iv::renderPlot(std::span<const std::complex<float>>(field), d, W, H, bare);
    REQUIRE(plain.has_value());

    // The volume render is identical; only the overlay (box/axes/legend/labels) differs. If the
    // facade did not build the overlay, the two images would match -> red.
    int diff = 0;
    for (std::uint32_t y = 0; y < H; ++y) {
        for (std::uint32_t x = 0; x < W; ++x) {
            const auto a = labeled->at(x, y);
            const auto b = plain->at(x, y);
            if (a.r != b.r || a.g != b.g || a.b != b.b) {
                ++diff;
            }
        }
    }
    CHECK(diff > 500);
}

TEST_CASE("renderPlot: the legend matches the render (single-source transfer)", "[vk][plot]") {
    const GridDims d{8, 8, 8};
    const float theta = 0.0f; // HSV phase 0 -> cyan
    const std::vector<std::complex<float>> field(d.count(), std::polar(1.0f, theta));
    const std::uint32_t W = 256u;
    const std::uint32_t H = 256u;

    iv::PlotOptions opts;
    opts.colormapMode = 1;        // HSV
    opts.densityScale = 2.0f;     // saturate the volume so it shows the phase color
    opts.background = {0.0f, 0.0f, 0.0f, 1.0f};
    auto img = iv::renderPlot(std::span<const std::complex<float>>(field), d, W, H, opts);
    REQUIRE(img.has_value());

    const auto px = [&](float nx) {
        return static_cast<std::uint32_t>((nx + 1.0f) * 0.5f * static_cast<float>(W));
    };
    const auto py = [&](float ny) {
        return static_cast<std::uint32_t>((ny + 1.0f) * 0.5f * static_cast<float>(H));
    };
    const auto isCyan = [](const auto& p) { return p.r < 45 && p.g > 200 && p.b > 200; };

    // The legend swatch's phase-0 column reads HSV cyan. placeLegendRight positions the swatch
    // aspect-dependently (ADR-0034; B-0016 slides it to keep value labels on screen), so scan the
    // legend region (right of center) along a row near the swatch top for the most-cyan pixel rather
    // than assuming a fixed column. The border/labels are near-white (high R) and are not picked.
    iv::vk::ImageReadback::Rgba legendPx{255, 0, 0, 255};
    for (std::uint32_t x = px(0.20f); x < W; ++x) {
        const auto c = img->at(x, py(-0.40f));
        if (c.g > 200 && c.b > 200 && c.r < legendPx.r) {
            legendPx = c; // the least-red cyan in the swatch's phase-0 column
        }
    }

    // A volume pixel: the uniform phase-0 cube reads HSV cyan. The facade frames the plot into the
    // left ~75% (ADR-0035 image shift), so scan the left half along the vertical center and take the
    // most-cyan pixel (robust to the exact framing; avoids box-edge/anti-aliased samples).
    iv::vk::ImageReadback::Rgba volPx{255, 0, 0, 255};
    for (std::uint32_t x = px(-0.85f); x <= px(0.10f); ++x) {
        const auto c = img->at(x, py(0.0f));
        if (c.g > 200 && c.b > 200 && c.r < volPx.r) {
            volPx = c; // the least-red cyan along the row
        }
    }

    // Both the legend swatch (phase-0 column) and the volume (uniform phase 0) read as HSV cyan
    // (R~0, high G, high B) under the SAME options -> the legend matches the render. A wrong
    // colormap (twilight is purple: R~48,G~20) or phase (red: G~0) in either breaks the band.
    // (Exact equality is sampling-sensitive: the half-pixel column center sits at theta~0.07,
    // where HSV green is ~0.93 not 1.0 — the legend faithfully shows that column's color.)
    CAPTURE(static_cast<int>(legendPx.r), static_cast<int>(legendPx.g), static_cast<int>(legendPx.b),
            static_cast<int>(volPx.r), static_cast<int>(volPx.g), static_cast<int>(volPx.b));
    CHECK(isCyan(legendPx));
    CHECK(isCyan(volPx));
}
