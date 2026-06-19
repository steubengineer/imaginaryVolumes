#include "iv/plot_axes.hpp"
#include "iv/text/annotations.hpp"
#include "iv/text/bundled_font.hpp"
#include "iv/text/shaper.hpp"
#include "iv/vk/context.hpp"
#include "iv/vk/renderer.hpp"
#include "iv/vk/view_projection.hpp"
#include "iv/vk/volume.hpp"
#include "iv/volume.hpp"

#include "catch_amalgamated.hpp"

#include <algorithm>
#include <array>
#include <complex>
#include <cstdint>
#include <vector>

using iv::Axis;
using iv::Dim;
using iv::PlotAxes;
using iv::ThroughAxis;
using iv::text::Shaper;

namespace {

iv::vk::RenderParams cubeCamera() {
    iv::vk::RenderParams p;
    p.eye = {2.4f, 1.7f, 2.1f};
    p.target = {0.5f, 0.5f, 0.5f};
    p.up = {0.0f, 1.0f, 0.0f};
    p.vfovRadians = 0.7f;
    p.background = {0.0f, 0.0f, 0.0f, 1.0f};
    return p;
}

PlotAxes labeledAxes() {
    PlotAxes a;
    a.x = Axis{0.0, 10.0, "x", "mm", {}, {}};
    a.y = Axis{-5.0, 5.0, "y", "", {}, {}};
    a.z = Axis{0.0, 1.0, "z", "", {}, {}};
    a.title = "Field";
    return a;
}

using Pt = std::array<float, 2>;
float cross2(const Pt& o, const Pt& a, const Pt& b) {
    return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
}

// CCW convex hull (Andrew's monotone chain).
std::vector<Pt> convexHull(std::vector<Pt> pts) {
    std::sort(pts.begin(), pts.end(), [](const Pt& a, const Pt& b) {
        return a[0] < b[0] || (a[0] == b[0] && a[1] < b[1]);
    });
    std::vector<Pt> h;
    for (const Pt& p : pts) { // lower
        while (h.size() >= 2 && cross2(h[h.size() - 2], h[h.size() - 1], p) <= 0.0f) {
            h.pop_back();
        }
        h.push_back(p);
    }
    const std::size_t lower = h.size() + 1;
    for (std::size_t i = pts.size(); i-- > 0;) { // upper
        const Pt& p = pts[i];
        while (h.size() >= lower && cross2(h[h.size() - 2], h[h.size() - 1], p) <= 0.0f) {
            h.pop_back();
        }
        h.push_back(p);
    }
    h.pop_back();
    return h;
}

// Is p strictly inside the CCW convex polygon by more than `margin`?
bool insideConvex(const std::vector<Pt>& poly, const Pt& p, float margin) {
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (cross2(poly[i], poly[(i + 1) % n], p) < margin) {
            return false; // outside / on an edge
        }
    }
    return true;
}

// The projected cube silhouette (8 corners -> pixels -> hull).
std::vector<Pt> cubeSilhouette(const std::array<float, 16>& m, std::uint32_t w, std::uint32_t h) {
    std::vector<Pt> corners;
    for (int i = 0; i < 8; ++i) {
        const std::array<float, 3> c{static_cast<float>(i & 1), static_cast<float>((i >> 1) & 1),
                                     static_cast<float>((i >> 2) & 1)};
        const auto px = iv::vk::projectToPixel(m, c, w, h);
        corners.push_back({px[0], px[1]});
    }
    return convexHull(corners);
}

std::vector<std::complex<float>> emptyField(iv::GridDims d) {
    return std::vector<std::complex<float>>(d.count(), std::complex<float>{0.0f, 0.0f});
}

} // namespace

// ADR-0026: the builder fills an Overlay from the model + camera, honoring every
// visibility toggle, with the ADR-0012 view-projection as the transform.
TEST_CASE("buildAnnotations honors the model and visibility toggles", "[annot]") {
    auto shaper = Shaper::create(iv::text::bundledFont(), 18.0f);
    REQUIRE(shaper);
    const std::uint32_t W = 512;
    const std::uint32_t H = 512;
    const auto cam = cubeCamera();

    iv::vk::Overlay full;
    iv::text::buildAnnotations(full, labeledAxes(), cam, W, H, *shaper);
    // Transform is the view-projection (not identity).
    CHECK(full.transform == iv::vk::viewProjection(cam, static_cast<float>(W) / H));
    CHECK(full.lines.size() >= 24);      // at least the 12 box edges (2 verts each)
    CHECK_FALSE(full.glyphs.empty());    // labels present
    CHECK_FALSE(full.glyphAtlas.empty());

    SECTION("disabling the box drops the box edges") {
        PlotAxes a = labeledAxes();
        a.boundingBox = false;
        a.boxTicks = false;
        iv::vk::Overlay ov;
        iv::text::buildAnnotations(ov, a, cam, W, H, *shaper);
        CHECK(ov.lines.empty());          // no box, no ticks
        CHECK_FALSE(ov.glyphs.empty());   // labels still there
    }
    SECTION("disabling all text drops the glyphs") {
        PlotAxes a = labeledAxes();
        a.tickLabels = false;
        a.axisLabels = false;
        a.showTitle = false;
        iv::vk::Overlay ov;
        iv::text::buildAnnotations(ov, a, cam, W, H, *shaper);
        CHECK(ov.glyphs.empty());
        CHECK(ov.glyphAtlas.empty());
        CHECK_FALSE(ov.lines.empty());    // box/ticks remain
    }
    SECTION("a through-axis adds a line through the volume") {
        PlotAxes a = labeledAxes();
        a.boundingBox = false;
        a.boxTicks = false;
        a.tickLabels = false;
        a.axisLabels = false;
        a.showTitle = false;
        a.throughAxes.push_back(ThroughAxis{Dim::X, {0.0, 0.0, 0.0}, true, false});
        iv::vk::Overlay ov;
        iv::text::buildAnnotations(ov, a, cam, W, H, *shaper);
        CHECK(ov.lines.size() == 2u);     // exactly the one through-axis line
    }
    SECTION("an empty model draws nothing") {
        PlotAxes a;
        a.boundingBox = a.boxTicks = a.tickLabels = a.axisLabels = a.showTitle = false;
        iv::vk::Overlay ov;
        iv::text::buildAnnotations(ov, a, cam, W, H, *shaper);
        CHECK(ov.empty());
    }
}

// ADR-0026 (the load-bearing placement contract): tick/axis labels are anchored to the
// box silhouette and offset outward, so they never overlap the projected data.
// teeth: inverting the down-and-out edge choice puts labels on an inner edge -> glyph
// quads land inside the silhouette -> this check goes red.
TEST_CASE("annotation labels stay outside the projected box silhouette", "[annot]") {
    auto shaper = Shaper::create(iv::text::bundledFont(), 18.0f);
    REQUIRE(shaper);
    const std::uint32_t W = 512;
    const std::uint32_t H = 512;
    const auto cam = cubeCamera();

    // Labels only (no box/tick lines), so every glyph is a label.
    PlotAxes a = labeledAxes();
    a.boundingBox = false;
    a.boxTicks = false;
    iv::vk::Overlay ov;
    iv::text::buildAnnotations(ov, a, cam, W, H, *shaper);
    REQUIRE_FALSE(ov.glyphs.empty());

    const std::vector<Pt> hull = cubeSilhouette(ov.transform, W, H);
    REQUIRE(hull.size() >= 3);

    int inside = 0;
    for (const auto& g : ov.glyphs) {
        // glyph positions are NDC; convert to the same pixel space as the hull.
        const Pt px{(g.pos[0] * 0.5f + 0.5f) * static_cast<float>(W),
                    (g.pos[1] * 0.5f + 0.5f) * static_cast<float>(H)};
        if (insideConvex(hull, px, 1.0f)) {
            ++inside;
        }
    }
    INFO("glyph verts inside silhouette: " << inside << " / " << ov.glyphs.size());
    CHECK(inside == 0);
}

// ADR-0026 end-to-end: the annotated overlay renders over the volume (headless). An
// empty (background) volume + full annotations paints box/line pixels and white label
// ink, validation-clean. teeth: the line + glyph draws are guarded by ADR-0021/0023.
TEST_CASE("annotations render over the volume", "[annot][vk]") {
    using iv::GridDims;
    using iv::vk::Context;
    using iv::vk::Renderer;
    using iv::vk::Volume;

    auto ctx = Context::create();
    REQUIRE(ctx.has_value());
    auto rend = Renderer::create(*ctx);
    REQUIRE(rend.has_value());
    const GridDims d{8, 8, 8};
    auto vol = Volume::create(*ctx, emptyField(d), d);
    REQUIRE(vol.has_value());
    auto shaper = Shaper::create(iv::text::bundledFont(), 18.0f);
    REQUIRE(shaper);

    const std::uint32_t W = 384;
    const std::uint32_t H = 384;
    iv::vk::RenderParams p = cubeCamera();
    iv::vk::Overlay ov;
    iv::text::buildAnnotations(ov, labeledAxes(), p, W, H, *shaper);
    REQUIRE_FALSE(ov.lines.empty());
    REQUIRE_FALSE(ov.glyphs.empty());

    auto img = rend->render(*vol, W, H, p, &ov);
    REQUIRE(img.has_value());

    int linePixels = 0;  // annotation lines: light gray (all channels high-ish, not pure white)
    int labelPixels = 0; // label ink: near white
    for (std::uint32_t y = 0; y < H; ++y) {
        for (std::uint32_t x = 0; x < W; ++x) {
            const auto c = img->at(x, y);
            const bool bright = c.r > 150 && c.g > 150 && c.b > 150;
            if (bright) {
                ++labelPixels;
            } else if (c.r > 80 && c.g > 80 && c.b > 90) {
                ++linePixels;
            }
        }
    }
    INFO("line~=" << linePixels << " label~=" << labelPixels);
    CHECK(linePixels > 100); // the box frame + ticks
    CHECK(labelPixels > 50); // the labels
    CHECK(ctx->validationClean());
}
