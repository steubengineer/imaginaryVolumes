// Legend builder (ADR-0028): the 2-D phase x magnitude swatch + ticks + labels. Gated with
// the text build. Structural teeth (host): screen-space channels + labels are populated, the
// opacity gradient runs bottom(transparent)->top(opaque), and a column carries the right phase
// color. End-to-end teeth (GPU): the rendered swatch shows the phase color across and the
// opacity gradient up. Together with the phaseColor<->shader cross-check in test_vk_renderer,
// this pins "the legend matches the transfer function/colormap".

#include "iv/legend.hpp"
#include "iv/plot_axes.hpp"
#include "iv/text/annotations.hpp"
#include "iv/text/bundled_font.hpp"
#include "iv/text/legend_builder.hpp"
#include "iv/text/shaper.hpp"
#include "iv/vk/context.hpp"
#include "iv/vk/renderer.hpp"
#include "iv/vk/volume.hpp"

#include "catch_amalgamated.hpp"

#include <algorithm>
#include <complex>
#include <vector>

using iv::GridDims;
using iv::vk::Context;
using iv::vk::Renderer;
using iv::vk::RenderParams;
using iv::vk::Volume;

TEST_CASE("Legend: buildLegend populates screen channels + labels and honors show", "[legend]") {
    auto shaper = iv::text::Shaper::create(iv::text::bundledFont(), 16.0f);
    REQUIRE(shaper.has_value());

    iv::LegendSpec spec;
    spec.range = {0.01f, 1.0f};
    spec.colormapMode = 1; // HSV: the theta=0 column is cyan (0,1,1)
    spec.opacityMode = 0;
    spec.densityScale = 1.0f;

    iv::vk::Overlay ov;
    iv::text::buildLegend(ov, spec, 256u, 256u, *shaper);

    // Screen-space swatch/border + glyph labels populated; world-space channels untouched.
    CHECK_FALSE(ov.screenTriangles.empty());
    CHECK_FALSE(ov.screenLines.empty());
    CHECK_FALSE(ov.glyphs.empty());
    CHECK(ov.lines.empty());
    CHECK(ov.triangles.empty());

    // Opacity gradient: the swatch bottom is transparent (alpha ~ 0). If alpha were constant
    // (a gradient bug) the minimum would be ~1, not 0.
    float minA = 1.0f;
    for (const auto& v : ov.screenTriangles) {
        minA = std::min(minA, v.color[3]);
    }
    CHECK(minA == Catch::Approx(0.0f).margin(1e-4));

    // Color: the center column (theta = 0) is HSV cyan; such a vertex must exist.
    bool hasCyan = false;
    for (const auto& v : ov.screenTriangles) {
        if (v.color[0] < 0.05f && v.color[1] > 0.95f && v.color[2] > 0.95f) {
            hasCyan = true;
        }
    }
    CHECK(hasCyan);

    // show = false draws nothing.
    iv::vk::Overlay off;
    spec.show = false;
    iv::text::buildLegend(off, spec, 256u, 256u, *shaper);
    CHECK(off.empty());
}

TEST_CASE("Legend: rebuilding into a reused overlay does not accumulate", "[legend]") {
    // The viewer reuses ONE overlay every frame (buildAnnotations then buildLegend). The
    // screen-space legend channels must be cleared each frame, or the swatch stacks copies →
    // compounding opacity. buildAnnotations() resets all channels; buildLegend() appends.
    auto shaper = iv::text::Shaper::create(iv::text::bundledFont(), 16.0f);
    REQUIRE(shaper.has_value());

    iv::PlotAxes axes;
    iv::LegendSpec spec;
    spec.range = {0.01f, 1.0f};
    iv::vk::Overlay ov;
    iv::vk::RenderParams cam;

    iv::text::buildAnnotations(ov, axes, cam, 256u, 256u, *shaper);
    iv::text::buildLegend(ov, spec, 256u, 256u, *shaper);
    const std::size_t triangles1 = ov.screenTriangles.size();
    const std::size_t lines1 = ov.screenLines.size();
    const std::size_t glyphs1 = ov.glyphs.size();
    REQUIRE(triangles1 > 0);

    // A second frame into the same overlay must produce the SAME counts, not doubled.
    iv::text::buildAnnotations(ov, axes, cam, 256u, 256u, *shaper);
    iv::text::buildLegend(ov, spec, 256u, 256u, *shaper);
    CHECK(ov.screenTriangles.size() == triangles1);
    CHECK(ov.screenLines.size() == lines1);
    CHECK(ov.glyphs.size() == glyphs1);
}

TEST_CASE("Legend: log mode uses decade ticks that track the window (B-0011)", "[legend]") {
    auto shaper = iv::text::Shaper::create(iv::text::bundledFont(), 16.0f);
    REQUIRE(shaper.has_value());

    iv::LegendSpec base;
    base.range = {1e-6f, 1.0f};
    base.opacityMode = 1; // log

    iv::LegendSpec narrow = base;
    narrow.logDecades = 2.0f; // window spans ~3 decade ticks
    iv::LegendSpec wide = base;
    wide.logDecades = 6.0f; // ~7 decade ticks

    iv::vk::Overlay on;
    iv::vk::Overlay ow;
    iv::text::buildLegend(on, narrow, 256u, 256u, *shaper);
    iv::text::buildLegend(ow, wide, 256u, 256u, *shaper);

    // A wider decade window shows more decade ticks -> more right-edge tick lines + labels. If
    // the decade window were ignored (e.g. linear ticks over ~[0,max]), the two would match.
    CHECK(ow.screenLines.size() > on.screenLines.size());
    CHECK(ow.glyphs.size() > on.glyphs.size());
}

TEST_CASE("Legend: swatch renders phase color across and opacity up (ADR-0028)", "[vk][legend]") {
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());
    auto rend = Renderer::create(*ctx);
    REQUIRE(rend.has_value());
    auto shaper = iv::text::Shaper::create(iv::text::bundledFont(), 16.0f);
    REQUIRE(shaper.has_value());

    const GridDims d{8, 8, 8};
    auto vol = Volume::create(*ctx, std::vector<std::complex<float>>(d.count(), {0.0f, 0.0f}), d);
    REQUIRE(vol.has_value()); // empty field -> background, so only the legend paints

    const std::uint32_t W = 256u;
    const std::uint32_t H = 256u;
    iv::LegendSpec spec;
    spec.range = {0.01f, 1.0f};
    spec.colormapMode = 1; // HSV: theta=0 -> cyan, theta=+-pi -> red
    spec.opacityMode = 0;
    spec.densityScale = 1.0f;
    spec.rectNdc = {0.2f, -0.7f, 0.7f, 0.7f}; // a large, easy-to-sample swatch

    iv::vk::Overlay ov;
    iv::text::buildLegend(ov, spec, W, H, *shaper);
    REQUIRE_FALSE(ov.screenTriangles.empty());

    RenderParams p;
    p.background = {0.0f, 0.0f, 0.0f, 1.0f};
    auto img = rend->render(*vol, W, H, p, &ov);
    REQUIRE(img.has_value());

    // NDC (y-down) -> pixel. Center column = theta 0 (cyan); sample near the top (opaque) and
    // near the bottom (transparent over the dark backing).
    const auto px = [&](float nx) { return static_cast<std::uint32_t>((nx + 1.0f) * 0.5f * static_cast<float>(W)); };
    const auto py = [&](float ny) { return static_cast<std::uint32_t>((ny + 1.0f) * 0.5f * static_cast<float>(H)); };
    const float xc = 0.5f * (0.2f + 0.7f); // center column -> theta 0
    const auto top = img->at(px(xc), py(-0.6f));  // high magnitude -> opaque cyan
    const auto bot = img->at(px(xc), py(0.6f));   // low magnitude -> transparent (dark backing)

    // Top: opaque HSV cyan (low R, high G/B).
    CHECK(top.r < 70);
    CHECK(top.g > 170);
    CHECK(top.b > 170);
    // Bottom: dark backing, NOT the bright cyan (proves the vertical opacity gradient).
    CHECK(bot.g < 90);
    CHECK(bot.b < 90);
    CHECK(ctx->validationClean());
}
