// Legend builder (ADR-0028): the 2-D phase x magnitude swatch + ticks + labels. Gated with
// the text build. Structural teeth (host): screen-space channels + labels are populated, the
// opacity gradient runs bottom(transparent)->top(opaque), and a column carries the right phase
// color. End-to-end teeth (GPU): the rendered swatch shows the phase color across and the
// opacity gradient up. Together with the phaseColor<->shader cross-check in test_vk_renderer,
// this pins "the legend matches the transfer function/colormap".

#include "iv/legend.hpp"
#include "iv/plot_axes.hpp"
#include "iv/text/annotations.hpp"
#include "iv/text/font_set.hpp"
#include "iv/text/legend_builder.hpp"
#include "iv/text/math_layout.hpp"
#include "iv/text/text_layout.hpp"
#include "iv/vk/context.hpp"
#include "iv/vk/renderer.hpp"
#include "iv/vk/view_projection.hpp"
#include "iv/vk/volume.hpp"

#include "catch_amalgamated.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

using iv::GridDims;
using iv::text::FontSet;
using iv::text::MixedGlyphs;
using iv::vk::Context;
using iv::vk::Renderer;
using iv::vk::RenderParams;
using iv::vk::Volume;

namespace {
// buildLegend through the M9 mixed-font path + finish (ADR-0032/0033); fonts last to match the
// old single-Shaper call shape.
void legendG(iv::vk::Overlay& ov, const iv::LegendSpec& spec, std::uint32_t W, std::uint32_t H,
             FontSet& fonts) {
    MixedGlyphs g(fonts);
    iv::text::buildLegend(ov, g, spec, W, H);
    g.finish(ov);
}
// One viewer-style frame: annotations + legend share ONE builder + ONE finish into the overlay.
void frameG(iv::vk::Overlay& ov, const iv::PlotAxes& axes, const RenderParams& cam,
            const iv::LegendSpec& spec, std::uint32_t W, std::uint32_t H, FontSet& fonts) {
    MixedGlyphs g(fonts);
    iv::text::buildAnnotations(ov, g, axes, cam, W, H);
    iv::text::buildLegend(ov, g, spec, W, H);
    g.finish(ov);
}

// A 3/4 view framed like the facade does (D-0049: the cube at orbit distance ~3.3, vfov 0.6) so
// the projected box extent matches a real plot (placeLegendRight is built for that framing).
RenderParams cubeCamera() {
    RenderParams p;
    p.eye = {2.77f, 1.94f, 2.41f};
    p.target = {0.5f, 0.5f, 0.5f};
    p.up = {0.0f, 1.0f, 0.0f};
    p.vfovRadians = 0.6f;
    return p;
}

// The rightmost NDC-x of the projected unit cube (the reference placeLegendRight clears).
float projectedBoxRight(const RenderParams& cam, std::uint32_t W, std::uint32_t H) {
    const auto M = iv::vk::viewProjection(cam, static_cast<float>(W) / static_cast<float>(H));
    const float halfW = static_cast<float>(W) * 0.5f;
    float r = -2.0f;
    for (int i = 0; i < 8; ++i) {
        const std::array<float, 3> c{static_cast<float>(i & 1), static_cast<float>((i >> 1) & 1),
                                     static_cast<float>((i >> 2) & 1)};
        const auto px = iv::vk::projectToPixel(M, c, W, H);
        if (px[2] > 0.0f) {
            r = std::max(r, px[0] / halfW - 1.0f);
        }
    }
    return r;
}
} // namespace

TEST_CASE("Legend: field name derives the captions; explicit labels override (ADR-0031)",
          "[legend]") {
    iv::LegendSpec s; // default fieldName "$f$" (math italic), empty overrides
    CHECK(s.magnitudeCaption() == "|$f$|");
    CHECK(s.phaseCaption() == "arg($f$)");

    s.fieldName = "Phi";
    CHECK(s.magnitudeCaption() == "|Phi|");
    CHECK(s.phaseCaption() == "arg(Phi)");

    // An explicit label overrides only its own caption (the escape hatch).
    s.magnitudeLabel = "|psi| (a.u.)";
    CHECK(s.magnitudeCaption() == "|psi| (a.u.)");
    CHECK(s.phaseCaption() == "arg(Phi)");

    // Empty field name + empty overrides -> no captions.
    iv::LegendSpec e;
    e.fieldName.clear();
    CHECK(e.magnitudeCaption().empty());
    CHECK(e.phaseCaption().empty());
}

TEST_CASE("Legend: buildLegend populates screen channels + labels and honors show", "[legend]") {
    auto fonts = FontSet::create(16.0f);
    REQUIRE(fonts.has_value());

    iv::LegendSpec spec;
    spec.range = {0.01f, 1.0f};
    spec.colormapMode = 1; // HSV: the theta=0 column is cyan (0,1,1)
    spec.opacityMode = 0;
    spec.densityScale = 1.0f;

    iv::vk::Overlay ov;
    legendG(ov, spec, 256u, 256u, *fonts);

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
    legendG(off, spec, 256u, 256u, *fonts);
    CHECK(off.empty());

    // The ADR-0030 thickness label uses plain "L" because the bundled NCM-Book face lacks
    // U+2113 (the script small l) — it maps to .notdef (0). Pin both so the label symbol stays a
    // present glyph (don't "upgrade" the label back to U+2113 without a font that has it).
    const auto ell = fonts->shaper(iv::text::Face::Roman).shape("\xE2\x84\x93");
    const auto cap = fonts->shaper(iv::text::Face::Roman).shape("L");
    REQUIRE(ell.size() == 1);
    REQUIRE(cap.size() == 1);
    CHECK(ell[0].glyphId == 0u); // U+2113 absent in NCM-Book
    CHECK(cap[0].glyphId != 0u); // "L" present
}

TEST_CASE("Legend: rebuilding into a reused overlay does not accumulate", "[legend]") {
    // The viewer reuses ONE overlay every frame (buildAnnotations then buildLegend). The
    // screen-space legend channels must be cleared each frame, or the swatch stacks copies →
    // compounding opacity. buildAnnotations() resets all channels; buildLegend() appends.
    auto fonts = FontSet::create(16.0f);
    REQUIRE(fonts.has_value());

    iv::PlotAxes axes;
    iv::LegendSpec spec;
    spec.range = {0.01f, 1.0f};
    iv::vk::Overlay ov;
    iv::vk::RenderParams cam;

    frameG(ov, axes, cam, spec, 256u, 256u, *fonts);
    const std::size_t triangles1 = ov.screenTriangles.size();
    const std::size_t lines1 = ov.screenLines.size();
    const std::size_t glyphs1 = ov.glyphs.size();
    REQUIRE(triangles1 > 0);

    // A second frame into the same overlay must produce the SAME counts, not doubled.
    frameG(ov, axes, cam, spec, 256u, 256u, *fonts);
    CHECK(ov.screenTriangles.size() == triangles1);
    CHECK(ov.screenLines.size() == lines1);
    CHECK(ov.glyphs.size() == glyphs1);
}

TEST_CASE("Legend: log mode uses decade ticks that track the window (B-0011)", "[legend]") {
    auto fonts = FontSet::create(16.0f);
    REQUIRE(fonts.has_value());

    iv::LegendSpec base;
    base.range = {1e-6f, 1.0f};
    base.opacityMode = 1; // log

    iv::LegendSpec narrow = base;
    narrow.logDecades = 2.0f; // window spans ~3 decade ticks
    iv::LegendSpec wide = base;
    wide.logDecades = 6.0f; // ~7 decade ticks

    iv::vk::Overlay on;
    iv::vk::Overlay ow;
    legendG(on, narrow, 256u, 256u, *fonts);
    legendG(ow, wide, 256u, 256u, *fonts);

    // A wider decade window shows more decade ticks -> more right-edge tick lines + labels. If
    // the decade window were ignored (e.g. linear ticks over ~[0,max]), the two would match.
    CHECK(ow.screenLines.size() > on.screenLines.size());
    CHECK(ow.glyphs.size() > on.glyphs.size());
}

// B-0016: the auto-generated magnitude-axis tick labels are promoted to inline `$…$` math so the
// M9 layout typesets true superscript exponents. Pin the pure formatting policy (maintainer choice
// 2026-07-03: decade ticks are a pure power 10^e; linear ticks stay plain decimals except for very
// small/large ranges, which use a shared-exponent m×10^exp). These strings are what buildLegend
// feeds the math layout, so a superscript actually renders (a `$`-free label would not).
TEST_CASE("Legend: magnitude tick labels use inline-math scientific notation (B-0016)", "[legend]") {
    SECTION("log decade ticks are a pure power of ten") {
        CHECK(iv::text::decadeTickLabel(0) == "1"); // 10^0 elided to the plain mantissa
        CHECK(iv::text::decadeTickLabel(-3) == "$10^{-3}$");
        CHECK(iv::text::decadeTickLabel(4) == "$10^{4}$");
        // The exponent is inside a `$…$` span (else the M9 layout would not superscript it).
        CHECK(iv::text::decadeTickLabel(-3).front() == '$');
    }
    SECTION("ordinary linear ranges keep plain decimals (no 1.2×10⁰)") {
        CHECK(iv::text::linearTickLabel(1.2, 0.1, 5.0) == "1.2");
        CHECK(iv::text::linearTickLabel(0.0, 0.1, 5.0) == iv::formatTick(0.0, 0.1)); // "0.0", unchanged
        CHECK(iv::text::linearTickLabel(0.5, 0.5, 2.0) == iv::formatTick(0.5, 0.5));
        // Same value, plain when the axis is ordinary — no `$` (teeth: the sci path is gated).
        CHECK(iv::text::linearTickLabel(1.2, 0.1, 5.0).find('$') == std::string::npos);
    }
    SECTION("very small linear ranges use a shared exponent m×10^exp") {
        // axisMax 0.004 < 1e-2 -> shared exp = -3; ticks read against it, zero excepted.
        CHECK(iv::text::linearTickLabel(0.004, 0.001, 0.004) == "$4\\times10^{-3}$");
        CHECK(iv::text::linearTickLabel(0.002, 0.001, 0.004) == "$2\\times10^{-3}$");
        CHECK(iv::text::linearTickLabel(0.0, 0.001, 0.004) == "0");
    }
    SECTION("very large linear ranges use a shared exponent too") {
        // axisMax 5e4 >= 1e4 -> shared exp = 4.
        CHECK(iv::text::linearTickLabel(30000.0, 10000.0, 50000.0) == "$3\\times10^{4}$");
        CHECK(iv::text::linearTickLabel(10000.0, 10000.0, 50000.0) == "$1\\times10^{4}$");
    }
}

// B-0016 follow-on: the sci-notation labels are much wider than the old decimals, so the swatch's
// right-edge reserve must be measured from the ACTUAL labels — a fixed reserve clipped the exponent
// (seen in a headless render). magnitudeValueReservePx measures the widest label buildLegend draws.
TEST_CASE("Legend: value-label reserve grows for wide sci labels so none clip (B-0016)", "[legend]") {
    auto fonts = FontSet::create(16.0f);
    REQUIRE(fonts.has_value());
    MixedGlyphs g(*fonts);

    iv::LegendSpec ordinary; // linear, ordinary range -> short decimals ("1.0", "0.5")
    ordinary.opacityMode = 0;
    ordinary.range = {0.0f, 5.0f};

    iv::LegendSpec sci; // linear, tiny range -> wide sci labels ("4×10⁻³")
    sci.opacityMode = 0;
    sci.range = {0.0f, 0.004f};

    const float rOrdinary = iv::text::magnitudeValueReservePx(ordinary, g);
    const float rSci = iv::text::magnitudeValueReservePx(sci, g);

    // Teeth: the wide-label axis reserves strictly more room than the short-decimal axis; if the
    // reserve were a fixed constant (the old bug) these would be equal and the exponent would clip.
    CHECK(rSci > rOrdinary + 10.0f);
    // And the reserve actually covers the widest label it will draw (+ the tick/gap stubs).
    float widest = 0.0f;
    for (const double mv : iv::ticksFor(0.0, 0.004, iv::kDefaultMajor, 1).major) {
        widest = std::max(widest, iv::text::math::measureLabel(
                                      *fonts, iv::text::linearTickLabel(mv, 0.001, 0.004), 16.0f));
    }
    CHECK(rSci >= widest);
}

TEST_CASE("Legend: thickness correction boosts swatch opacity (ADR-0030)", "[legend]") {
    auto fonts = FontSet::create(16.0f);
    REQUIRE(fonts.has_value());

    iv::LegendSpec base;
    base.range = {0.01f, 1.0f};
    base.densityScale = 0.1f; // small per-sample alpha (capped at 0.1) so the boost is unmistakable

    iv::LegendSpec off = base;
    off.referenceThickness = 0.0f; // uncorrected per-sample legend
    iv::LegendSpec thick = base;
    thick.referenceThickness = 0.5f;

    iv::vk::Overlay oo;
    iv::vk::Overlay ot;
    legendG(oo, off, 256u, 256u, *fonts);
    legendG(ot, thick, 256u, 256u, *fonts);

    const auto maxAlpha = [](const iv::vk::Overlay& ov) {
        float m = 0.0f;
        for (const auto& v : ov.screenTriangles) {
            m = std::max(m, v.color[3]);
        }
        return m;
    };
    CHECK(maxAlpha(oo) <= 0.11f); // off: per-sample alpha capped at the density (0.1)
    CHECK(maxAlpha(ot) > 0.9f);   // thickness 0.5 accumulates the top toward opaque
}

// ADR-0034: rotated-label emission. A label laid out horizontally then rotated −π/2 about a pivot
// maps each glyph-quad corner's pixel offset (dx,dy) -> (dy,−dx) (advance +x -> up), leaving the
// Slug texcoord/atlas index untouched (only the on-screen placement rotates). angleRad = 0 is the
// identity. teeth: wrong sign/axis (e.g. +π/2) or rotating the texcoord lands corners in the wrong
// quadrant / corrupts the glyph; the identity check catches any spurious mutation at angle 0.
TEST_CASE("MixedGlyphs::rotateSince rotates quads in pixel space, not the glyph (ADR-0034)",
          "[legend][text]") {
    auto fonts = FontSet::create(20.0f);
    REQUIRE(fonts.has_value());
    const std::uint32_t W = 200u, H = 200u; // square -> isotropic NDC<->pixel
    const float halfW = static_cast<float>(W) * 0.5f, halfH = static_cast<float>(H) * 0.5f;
    const float pivotX = 70.0f, pivotY = 100.0f;

    const auto toPx = [&](const iv::vk::GlyphVertex& v) {
        return std::array<float, 2>{(v.pos[0] + 1.0f) * halfW, (v.pos[1] + 1.0f) * halfH};
    };

    // Horizontal reference.
    iv::vk::Overlay h;
    {
        MixedGlyphs g(*fonts);
        g.appendRun(iv::text::Face::Roman, "L", pivotX, pivotY, W, H, {{1, 1, 1, 1}, 20.0f});
        g.finish(h);
    }
    // Rotated −π/2 about the pivot.
    iv::vk::Overlay r;
    {
        MixedGlyphs g(*fonts);
        const auto m = g.marker();
        g.appendRun(iv::text::Face::Roman, "L", pivotX, pivotY, W, H, {{1, 1, 1, 1}, 20.0f});
        g.rotateSince(m, -1.57079632679f, pivotX, pivotY, W, H);
        g.finish(r);
    }
    // Identity (angle 0) must reproduce the horizontal quads byte-for-byte.
    iv::vk::Overlay id;
    {
        MixedGlyphs g(*fonts);
        const auto m = g.marker();
        g.appendRun(iv::text::Face::Roman, "L", pivotX, pivotY, W, H, {{1, 1, 1, 1}, 20.0f});
        g.rotateSince(m, 0.0f, pivotX, pivotY, W, H);
        g.finish(id);
    }

    REQUIRE_FALSE(h.glyphs.empty());
    REQUIRE(h.glyphs.size() == r.glyphs.size());
    REQUIRE(h.glyphs.size() == id.glyphs.size());
    for (std::size_t i = 0; i < h.glyphs.size(); ++i) {
        const auto ph = toPx(h.glyphs[i]);
        const auto pr = toPx(r.glyphs[i]);
        const float dx = ph[0] - pivotX, dy = ph[1] - pivotY;
        CHECK(pr[0] - pivotX == Catch::Approx(dy).margin(1e-3));  // (dx,dy) -> (dy,−dx)
        CHECK(pr[1] - pivotY == Catch::Approx(-dx).margin(1e-3));
        CHECK(r.glyphs[i].texcoord[0] == h.glyphs[i].texcoord[0]); // glyph outline untouched
        CHECK(r.glyphs[i].texcoord[1] == h.glyphs[i].texcoord[1]);
        CHECK(r.glyphs[i].glyphLoc == h.glyphs[i].glyphLoc);
        CHECK(id.glyphs[i].pos[0] == Catch::Approx(h.glyphs[i].pos[0])); // angle 0 == identity
        CHECK(id.glyphs[i].pos[1] == Catch::Approx(h.glyphs[i].pos[1]));
    }
}

// ADR-0034: the magnitude caption sits rotated to the LEFT of the swatch, vertically centered —
// not horizontally centered above it. teeth: the old "centered above" placement put the caption
// above rectNdc.top and centered in x, so NO caption glyph is in the swatch's vertical mid-band and
// left of its left edge -> the existence check goes red.
TEST_CASE("Legend: magnitude caption is rotated to the left of the swatch (ADR-0034)", "[legend]") {
    auto fonts = FontSet::create(16.0f);
    REQUIRE(fonts.has_value());

    iv::LegendSpec spec; // default fieldName "$f$" -> caption "|$f$|"
    spec.range = {0.01f, 1.0f};
    spec.rectNdc = {0.60f, -0.45f, 0.84f, 0.45f}; // explicit default (don't depend on placement)

    iv::vk::Overlay ov;
    legendG(ov, spec, 256u, 256u, *fonts);
    REQUIRE_FALSE(ov.glyphs.empty());

    const float left = spec.rectNdc[0];
    const float midY = 0.5f * (spec.rectNdc[1] + spec.rectNdc[3]); // swatch vertical center (NDC)
    const float band = 0.12f; // central band; the caption straddles the mid, the −π/0/π row is below

    int captionGlyphs = 0;
    for (const auto& v : ov.glyphs) {
        if (v.pos[0] < left - 0.005f && std::abs(v.pos[1] - midY) < band) {
            ++captionGlyphs;
        }
    }
    CHECK(captionGlyphs > 0); // the rotated |f| caption — the only label left of the mid-band swatch
}

// ADR-0034: placeLegendRight tracks the projected box — pushing the swatch right (by the box's
// right extent + a caption/gap allowance) so it does not collide — while never moving left of the
// default and keeping the right-edge value labels on screen. A wide window (narrow box) is the
// unchanged default. Constants below mirror legend_builder.cpp (px gaps -> NDC via halfW).
TEST_CASE("Legend: placeLegendRight clears the box, aspect-aware (ADR-0034)", "[legend]") {
    const RenderParams cam = cubeCamera();

    const auto place = [&](std::array<float, 4> rect, std::uint32_t W, std::uint32_t H,
                           float reservePx = 46.0f) {
        iv::LegendSpec s;
        s.rectNdc = rect;
        iv::text::placeLegendRight(s, cam, W, H, reservePx);
        return s.rectNdc;
    };

    const std::array<float, 4> def{0.60f, -0.45f, 0.84f, 0.45f};
    const float width = def[2] - def[0];

    SECTION("square/tall window (wide box): pushed to the on-screen limit, clearing the default") {
        const std::uint32_t W = 900u, H = 900u; // the box's right corner overruns the default left
        const float halfW = static_cast<float>(W) * 0.5f;
        const float valAllow = 46.0f / halfW;
        const float boxRight = projectedBoxRight(cam, W, H);
        const float hi = 0.98f - valAllow - width; // the rightmost on-screen swatch left
        CAPTURE(boxRight, hi);
        REQUIRE(boxRight > def[0]);  // the box really would overlap the default panel here...
        REQUIRE(hi > def[0]);        // ...and there is room to push right of it

        const auto r = place(def, W, H);
        CHECK(r[0] > def[0]);                        // pushed right (teeth: ignore-box stays default)
        CHECK(r[0] == Catch::Approx(hi));            // as far right as keeps value labels on screen
        CHECK(r[2] + valAllow <= 0.98f + 1e-4f);     // value labels on screen (teeth: no right clamp)
        CHECK(r[2] - r[0] == Catch::Approx(width));  // width preserved
        CHECK(r[1] == def[1]);                       // vertical extent unchanged
        CHECK(r[3] == def[3]);
    }
    SECTION("wide window (narrow box): the unchanged default") {
        const std::uint32_t W = 1400u, H = 480u;
        const float halfW = static_cast<float>(W) * 0.5f;
        const float boxRight = projectedBoxRight(cam, W, H);
        CAPTURE(boxRight);
        REQUIRE(boxRight + (18.0f + 30.0f) / halfW < def[0]); // box clears the default already
        const auto r = place(def, W, H);
        CHECK(r[0] == Catch::Approx(def[0])); // no-op (teeth: an always-push build would move it)
        CHECK(r[2] == Catch::Approx(def[2]));
    }
    SECTION("never moves the swatch left of the default (default reserve, all aspects)") {
        for (const auto wh : {std::array<std::uint32_t, 2>{600, 1000},
                              std::array<std::uint32_t, 2>{900, 900},
                              std::array<std::uint32_t, 2>{1400, 600}}) {
            const auto r = place(def, wh[0], wh[1]);
            CAPTURE(wh[0], wh[1], r[0]);
            CHECK(r[0] >= def[0] - 1e-5f); // narrow labels fit at/right-of the default at every aspect
        }
    }
    // B-0016: a wide value-label reserve (the sci labels) needs more room than the default panel
    // leaves before the screen edge. When the box is far enough left, the swatch slides LEFT of the
    // default just enough to keep the widest label on screen — but never into the box. A wide window
    // (box far left) is where there IS room to slide, so this is where the fix must bite.
    SECTION("wide labels slide the swatch left to stay on screen, but never into the box (B-0016)") {
        const std::uint32_t W = 1400u, H = 600u; // wide: the projected box sits well left of default
        const float halfW = static_cast<float>(W) * 0.5f;
        const float reservePx = 120.0f;          // a wide sci label ("2.5×10⁻³"-class) reserve
        const float boxRight = projectedBoxRight(cam, W, H);
        const float boxClear = boxRight + (18.0f + 30.0f) / halfW; // gap + caption room (mirror .cpp)
        const float hi = 0.98f - reservePx / halfW - width;
        CAPTURE(boxRight, boxClear, hi);
        REQUIRE(hi < def[0]);      // the wide label does NOT fit at the default...
        REQUIRE(boxClear < hi);    // ...but the box is far enough left that sliding to hi clears it

        const auto r = place(def, W, H, reservePx);
        CHECK(r[0] < def[0]);                              // slid left of the default (teeth: old pinned)
        CHECK(r[0] == Catch::Approx(hi));                  // exactly enough to keep the label on screen
        CHECK(r[2] + reservePx / halfW <= 0.98f + 1e-4f);  // widest value label on screen
        CHECK(r[0] >= boxClear - 1e-5f);                   // never slid into the box (caption clears it)
        CHECK(r[2] - r[0] == Catch::Approx(width));        // swatch width preserved
    }
}

TEST_CASE("Legend: swatch renders phase color across and opacity up (ADR-0028)", "[vk][legend]") {
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());
    auto rend = Renderer::create(*ctx);
    REQUIRE(rend.has_value());
    auto fonts = FontSet::create(16.0f);
    REQUIRE(fonts.has_value());

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
    spec.referenceThickness = 0.0f; // isolate the per-sample gradient (ADR-0030 has its own test)
    spec.rectNdc = {0.2f, -0.7f, 0.7f, 0.7f}; // a large, easy-to-sample swatch

    iv::vk::Overlay ov;
    legendG(ov, spec, W, H, *fonts);
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
