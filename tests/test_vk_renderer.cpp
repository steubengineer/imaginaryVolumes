#include "iv/transfer.hpp"
#include "iv/vk/colormap_lut.hpp"
#include "iv/vk/context.hpp"
#include "iv/vk/renderer.hpp"
#include "iv/vk/view_projection.hpp"
#include "iv/vk/volume.hpp"

#include "catch_amalgamated.hpp"

#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <vector>

using iv::GridDims;
using iv::vk::Context;
using iv::vk::Renderer;
using iv::vk::RenderParams;
using iv::vk::Volume;

namespace {

constexpr float kPi = 3.14159265358979323846f;

bool near8(std::uint8_t a, int target, int tol = 2) {
    return std::abs(static_cast<int>(a) - target) <= tol;
}

// A field where every voxel has the same magnitude and phase.
std::vector<std::complex<float>> uniformField(GridDims d, float mag, float phase) {
    return std::vector<std::complex<float>>(d.count(), std::polar(mag, phase));
}

// A field split along z (the texture axis ~ aligned with the default view): the
// high-z half (nearer the camera) gets phaseHi, the low-z half phaseLo.
std::vector<std::complex<float>> zSplitField(GridDims d, float mag, float phaseHi, float phaseLo) {
    std::vector<std::complex<float>> v(d.count());
    for (std::uint32_t z = 0; z < d.nz; ++z) {
        for (std::uint32_t y = 0; y < d.ny; ++y) {
            for (std::uint32_t x = 0; x < d.nx; ++x) {
                v[d.index(x, y, z)] = std::polar(mag, (z >= d.nz / 2u) ? phaseHi : phaseLo);
            }
        }
    }
    return v;
}

// Magnitude graded over z as 10^(span·z/(nz-1)) (phase 0): spans 1 .. 10^span, so
// `span` decades of dynamic range. For the ADR-0027 log-decade-window tests.
std::vector<std::complex<float>> gradedMagField(GridDims d, float span) {
    std::vector<std::complex<float>> v(d.count());
    for (std::uint32_t z = 0; z < d.nz; ++z) {
        const float t = d.nz > 1 ? static_cast<float>(z) / static_cast<float>(d.nz - 1) : 0.0f;
        const float mag = std::pow(10.0f, span * t);
        for (std::uint32_t y = 0; y < d.ny; ++y) {
            for (std::uint32_t x = 0; x < d.nx; ++x) {
                v[d.index(x, y, z)] = std::polar(mag, 0.0f);
            }
        }
    }
    return v;
}

// f = w = (x-0.5) + i(y-0.5): phase = azimuth (sweeps the full ±pi range,
// crossing the branch cut), magnitude = radius. z-independent.
std::vector<std::complex<float>> vortexField(GridDims d) {
    std::vector<std::complex<float>> v(d.count());
    for (std::uint32_t z = 0; z < d.nz; ++z) {
        for (std::uint32_t y = 0; y < d.ny; ++y) {
            for (std::uint32_t x = 0; x < d.nx; ++x) {
                const float fx = (static_cast<float>(x) + 0.5f) / static_cast<float>(d.nx);
                const float fy = (static_cast<float>(y) + 0.5f) / static_cast<float>(d.ny);
                v[d.index(x, y, z)] = std::complex<float>(fx - 0.5f, fy - 0.5f);
            }
        }
    }
    return v;
}

} // namespace

// teeth (pipeline + ADR-0013 zero handling): an all-zero field has alpha 0
// everywhere, so the whole image is the background. A nonzero opacity for m=0
// would tint the box region; the trivial-output path also proves the
// compute/dispatch/readback works at all.
TEST_CASE("Renderer: empty field renders the background everywhere", "[vk][renderer]") {
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());
    auto rend = Renderer::create(*ctx);
    REQUIRE(rend.has_value());

    const GridDims d{8, 8, 8};
    auto vol = Volume::create(*ctx, uniformField(d, 0.0f, 0.0f), d);
    REQUIRE(vol.has_value());

    RenderParams p;
    p.background = {0.25f, 0.25f, 0.25f, 1.0f}; // -> bytes (64,64,64,255)
    auto img = rend->render(*vol, 32, 32, p);
    REQUIRE(img.has_value());

    bool allBackground = true;
    for (std::uint32_t y = 0; y < 32; ++y) {
        for (std::uint32_t x = 0; x < 32; ++x) {
            const auto px = img->at(x, y);
            if (!(near8(px.r, 64) && near8(px.g, 64) && near8(px.b, 64) && px.a == 255)) {
                allBackground = false;
            }
        }
    }
    CHECK(allBackground);
    CHECK(ctx->validationClean());
}

// teeth (ADR-0012 camera/compositing, ADR-0014 colormap): a uniform field in
// linear mode has alpha 1, so a box-hitting ray returns the phase color exactly.
// phase 0 -> t=0.5 -> HSV cyan. A colormap phase-offset shifts the hue; a broken
// ray/box changes which pixels are background.
TEST_CASE("Renderer: uniform field (linear, HSV) paints the phase color over the box",
          "[vk][renderer]") {
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());
    auto rend = Renderer::create(*ctx);
    REQUIRE(rend.has_value());

    const GridDims d{8, 8, 8};
    auto vol = Volume::create(*ctx, uniformField(d, 1.0f, 0.0f), d); // phase 0 -> cyan
    REQUIRE(vol.has_value());

    RenderParams p;
    p.opacityMode = 0;   // linear
    p.colormapMode = 1;  // HSV
    p.background = {0.25f, 0.25f, 0.25f, 1.0f};
    auto img = rend->render(*vol, 64, 64, p);
    REQUIRE(img.has_value());

    const auto center = img->at(32, 32); // ray crosses the cube -> cyan
    CHECK(near8(center.r, 0));
    CHECK(near8(center.g, 255));
    CHECK(near8(center.b, 255));

    const auto corner = img->at(0, 0); // misses the cube -> background
    CHECK(near8(corner.r, 64));
    CHECK(near8(corner.g, 64));
    CHECK(near8(corner.b, 64));
    CHECK(ctx->validationClean());
}

// teeth (ADR-0013 log + degenerate range): a uniform-magnitude field has
// minPositive == max, so the log mapping is degenerate -> alpha 0 -> transparent.
// Paired with the linear test, swapping the linear/log branch flips both results.
TEST_CASE("Renderer: log opacity on a uniform (degenerate-range) field is transparent",
          "[vk][renderer]") {
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());
    auto rend = Renderer::create(*ctx);
    REQUIRE(rend.has_value());

    const GridDims d{8, 8, 8};
    auto vol = Volume::create(*ctx, uniformField(d, 1.0f, 0.0f), d);
    REQUIRE(vol.has_value());

    RenderParams p;
    p.opacityMode = 1;  // logarithmic; minPositive == max == 1 -> degenerate -> 0
    p.colormapMode = 1;
    p.background = {0.25f, 0.25f, 0.25f, 1.0f};
    auto img = rend->render(*vol, 64, 64, p);
    REQUIRE(img.has_value());

    const auto center = img->at(32, 32); // over the box, but transparent -> background
    CHECK(near8(center.r, 64));
    CHECK(near8(center.g, 64));
    CHECK(near8(center.b, 64));
    CHECK(ctx->validationClean());
}

// teeth (ADR-0027 log decade window): the same uniform field that log mode renders
// transparent (degenerate [minPositive==max] range, decades=0) becomes fully opaque
// with a decade window, because the window is anchored at `max` (m==max -> mn=1).
// Removing the ADR-0027 branch makes the decades=4 case fall back to transparent.
TEST_CASE("Renderer: log decade window makes a uniform field opaque (ADR-0027)",
          "[vk][renderer]") {
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());
    auto rend = Renderer::create(*ctx);
    REQUIRE(rend.has_value());

    const GridDims d{8, 8, 8};
    auto vol = Volume::create(*ctx, uniformField(d, 1.0f, 0.0f), d); // m == max == 1
    REQUIRE(vol.has_value());

    RenderParams p;
    p.opacityMode = 1;  // logarithmic
    p.colormapMode = 1; // HSV; phase 0 -> cyan
    p.background = {0.25f, 0.25f, 0.25f, 1.0f};

    // decades = 0: the ADR-0013 [minPositive, max] range is degenerate -> transparent.
    p.logDecades = 0.0f;
    auto off = rend->render(*vol, 64, 64, p);
    REQUIRE(off.has_value());
    CHECK(near8(off->at(32, 32).r, 64)); // background

    // decades = 4: m == max -> mn = 1 -> opaque -> the phase color (cyan).
    p.logDecades = 4.0f;
    auto on = rend->render(*vol, 64, 64, p);
    REQUIRE(on.has_value());
    const auto c = on->at(32, 32);
    CHECK(near8(c.r, 0));
    CHECK(near8(c.g, 255));
    CHECK(near8(c.b, 255));
    CHECK(ctx->validationClean());
}

// A wider decade window admits more of a graded field (the low-magnitude part the
// narrow window clips), so it composites brighter. teeth: a wrong window (or reverting
// the formula) breaks the ordering.
TEST_CASE("Renderer: a wider log decade window shows more of a graded field (ADR-0027)",
          "[vk][renderer]") {
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());
    auto rend = Renderer::create(*ctx);
    REQUIRE(rend.has_value());

    const GridDims d{16, 16, 16};
    auto vol = Volume::create(*ctx, gradedMagField(d, 2.0f), d); // magnitudes 1 .. 100
    REQUIRE(vol.has_value());

    RenderParams p;
    p.opacityMode = 1;
    p.colormapMode = 1;
    p.densityScale = 0.1f; // partial alpha so the window width is visible (not saturated)
    p.background = {0.0f, 0.0f, 0.0f, 1.0f};

    const auto totalBrightness = [](const iv::vk::ImageReadback& img) {
        long s = 0;
        for (std::uint32_t y = 0; y < img.height(); ++y) {
            for (std::uint32_t x = 0; x < img.width(); ++x) {
                const auto c = img.at(x, y);
                s += static_cast<long>(c.r) + c.g + c.b;
            }
        }
        return s;
    };

    p.logDecades = 1.0f; // only the top decade [10, 100]
    auto narrow = rend->render(*vol, 64, 64, p);
    REQUIRE(narrow.has_value());
    p.logDecades = 4.0f; // the whole field (and below) maps in
    auto wide = rend->render(*vol, 64, 64, p);
    REQUIRE(wide.has_value());

    const long sNarrow = totalBrightness(*narrow);
    const long sWide = totalBrightness(*wide);
    INFO("narrow(1 decade)=" << sNarrow << " wide(4 decades)=" << sWide);
    CHECK(sWide > sNarrow);
    CHECK(ctx->validationClean());
}

// teeth (ADR-0012 front-to-back order): a field split along the view axis (cyan
// near the camera, red behind) composites to a front-dominated color. Flipping
// the compositing order makes the back (red) dominate instead.
TEST_CASE("Renderer: front-to-back compositing is order-dependent", "[vk][renderer]") {
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());
    auto rend = Renderer::create(*ctx);
    REQUIRE(rend.has_value());

    const GridDims d{16, 16, 16};
    auto vol = Volume::create(*ctx, zSplitField(d, 1.0f, 0.0f, kPi), d); // hi-z cyan, lo-z red
    REQUIRE(vol.has_value());

    RenderParams p;
    p.opacityMode = 0;
    p.colormapMode = 1;
    p.densityScale = 0.05f; // partial per-sample alpha so both halves contribute
    p.background = {0.0f, 0.0f, 0.0f, 1.0f};
    auto img = rend->render(*vol, 64, 64, p);
    REQUIRE(img.has_value());

    const auto center = img->at(32, 32);
    // Front (cyan: high B, low R) dominates front-to-back; a back-to-front flip
    // would make red (high R, low B) dominate.
    CHECK(static_cast<int>(center.b) > static_cast<int>(center.r));
    CHECK(ctx->validationClean());
}

// teeth (ADR-0014 LUT path + selector): the default LUT colormap reproduces the
// committed twilight table and differs from HSV. phase 0 -> t=0.5 -> the average
// of twilight entries 127 and 128 (linear-interpolated texel centers).
TEST_CASE("Renderer: LUT colormap matches the twilight table and differs from HSV",
          "[vk][renderer]") {
    using iv::vk::kTwilightLut;
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());
    auto rend = Renderer::create(*ctx);
    REQUIRE(rend.has_value());

    const GridDims d{8, 8, 8};
    auto vol = Volume::create(*ctx, uniformField(d, 1.0f, 0.0f), d); // phase 0 -> t = 0.5
    REQUIRE(vol.has_value());

    RenderParams p;
    p.opacityMode = 0;
    p.background = {0.0f, 0.0f, 0.0f, 1.0f};
    p.colormapMode = 0; // LUT
    auto lut = rend->render(*vol, 32, 32, p);
    REQUIRE(lut.has_value());
    p.colormapMode = 1; // HSV
    auto hsv = rend->render(*vol, 32, 32, p);
    REQUIRE(hsv.has_value());

    const auto cl = lut->at(16, 16);
    const auto ch = hsv->at(16, 16);

    // texcoord 0.5 linearly interpolates twilight entries 127 and 128.
    const int er = (static_cast<int>(kTwilightLut[127u * 4u + 0u]) +
                    static_cast<int>(kTwilightLut[128u * 4u + 0u]) + 1) / 2;
    const int eg = (static_cast<int>(kTwilightLut[127u * 4u + 1u]) +
                    static_cast<int>(kTwilightLut[128u * 4u + 1u]) + 1) / 2;
    const int eb = (static_cast<int>(kTwilightLut[127u * 4u + 2u]) +
                    static_cast<int>(kTwilightLut[128u * 4u + 2u]) + 1) / 2;
    CHECK(near8(cl.r, er, 3));
    CHECK(near8(cl.g, eg, 3));
    CHECK(near8(cl.b, eb, 3));

    // The LUT color is clearly not the HSV cyan (selector actually switches maps).
    const int diff = std::abs(static_cast<int>(cl.r) - static_cast<int>(ch.r)) +
                     std::abs(static_cast<int>(cl.g) - static_cast<int>(ch.g)) +
                     std::abs(static_cast<int>(cl.b) - static_cast<int>(ch.b));
    CHECK(diff > 20);
    CHECK(ctx->validationClean());
}

// teeth (ADR-0028): the host phaseColor() evaluator — which the legend draws through — must
// match the GPU shader's arg->color exactly, so the legend cannot drift from the render. A
// saturated uniform field (m == max, density 1) over a black background renders ~ the phase
// color directly; assert the center pixel equals iv::phaseColor for several phases in BOTH
// colormap modes. Perturbing phaseColor (or the shared LUT) diverges from the GPU -> red.
TEST_CASE("Renderer: host phaseColor matches the GPU colormap (ADR-0028)", "[vk][renderer]") {
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());
    auto rend = Renderer::create(*ctx);
    REQUIRE(rend.has_value());

    const GridDims d{8, 8, 8};
    RenderParams p;
    p.opacityMode = 0;
    p.densityScale = 1.0f; // m == max -> mn == 1 -> alpha 1: the first sample saturates
    p.background = {0.0f, 0.0f, 0.0f, 1.0f};

    const float phases[5] = {-2.0f, -0.6f, 0.0f, 1.1f, 2.7f};
    for (std::uint32_t mode = 0u; mode <= 1u; ++mode) {
        p.colormapMode = mode;
        for (const float phase : phases) {
            auto vol = Volume::create(*ctx, uniformField(d, 1.0f, phase), d);
            REQUIRE(vol.has_value());
            auto img = rend->render(*vol, 32, 32, p);
            REQUIRE(img.has_value());
            const auto c = img->at(16, 16);
            const std::array<float, 3> host = iv::phaseColor(phase, mode);
            CHECK(near8(c.r, static_cast<int>(std::lround(host[0] * 255.0f)), 6));
            CHECK(near8(c.g, static_cast<int>(std::lround(host[1] * 255.0f)), 6));
            CHECK(near8(c.b, static_cast<int>(std::lround(host[2] * 255.0f)), 6));
        }
    }
    CHECK(ctx->validationClean());
}

// teeth (ADR-0015): phase varies across the ±π branch cut. Storing the complex
// value (not the angle) keeps interpolation correct, so the negative-real axis
// renders its true HSV color (red), not the interpolated-to-zero color (cyan).
// Regressing to a stored phase angle brings the cyan seam back here.
TEST_CASE("Renderer: phase is correct across the branch cut (no seam)", "[vk][renderer]") {
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());
    auto rend = Renderer::create(*ctx);
    REQUIRE(rend.has_value());

    // A coarse volume makes the interpolation band across the y=0.5 cut wide (many
    // pixels), so the test reliably samples it (a fine volume would tuck a thin
    // seam between sampled rows). Render finer than the volume.
    const GridDims d{8, 8, 8};
    auto vol = Volume::create(*ctx, vortexField(d), d);
    REQUIRE(vol.has_value());

    RenderParams p;
    p.eye = {0.5f, 0.5f, 2.2f}; // face-on, looking down -z
    p.target = {0.5f, 0.5f, 0.5f};
    p.up = {0.0f, 1.0f, 0.0f};
    p.colormapMode = 1; // HSV
    p.densityScale = 5.0f;
    p.background = {0.0f, 0.0f, 0.0f, 1.0f};
    auto img = rend->render(*vol, 96, 96, p);
    REQUIRE(img.has_value());

    // Left of center, center row: x<0.5, y≈0.5 (on the branch cut). theta ≈ ±π →
    // red. Storing/interpolating the angle instead would interpolate to 0 → cyan.
    const auto px = img->at(24, 48);
    CHECK(static_cast<int>(px.r) > static_cast<int>(px.g) + 40);
    CHECK(static_cast<int>(px.r) > static_cast<int>(px.b) + 40);
    CHECK(ctx->validationClean());
}

// teeth (ADR-0020): per-sample opacity is corrected for the step spacing dt, so the
// rendered density is invariant to stepCount. A uniform partial-opacity field is
// rendered at stepCount 32 and 256: because every sample is identical, corrected
// accumulation A = 1-(1-a)^(kReferenceSteps*pathLen) is independent of N, so the two
// renders match. Without the dt-correction, A = 1-(1-a)^N, so the 256-step render is
// far denser (much brighter over black) and the channels diverge by ~150.
TEST_CASE("Renderer: opacity is invariant to stepCount (dt-correction)", "[vk][renderer]") {
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());
    auto rend = Renderer::create(*ctx);
    REQUIRE(rend.has_value());

    const GridDims d{8, 8, 8};
    auto vol = Volume::create(*ctx, uniformField(d, 1.0f, 0.0f), d); // mag 1 -> per-sample a = density
    REQUIRE(vol.has_value());

    RenderParams p;
    p.eye = {0.5f, 0.5f, 2.2f}; // face-on; the central ray crosses the cube
    p.target = {0.5f, 0.5f, 0.5f};
    p.up = {0.0f, 1.0f, 0.0f};
    p.opacityMode = 0;         // linear: a = clamp(density) = 0.01 at every sample
    p.colormapMode = 1;        // HSV (phase 0 -> cyan)
    p.densityScale = 0.01f;    // small per-sample alpha so uncorrected A depends on N
    p.alphaTermination = 2.0f; // unreachable: no early-out can mask the step count
    p.background = {0.0f, 0.0f, 0.0f, 1.0f};

    p.stepCount = 32u;
    auto coarse = rend->render(*vol, 64, 64, p);
    REQUIRE(coarse.has_value());
    p.stepCount = 256u;
    auto fine = rend->render(*vol, 64, 64, p);
    REQUIRE(fine.has_value());

    const auto c = coarse->at(32, 32);
    const auto f = fine->at(32, 32);
    // Density is invariant to stepCount (corrected). Reverting the correction makes
    // the fine render much brighter, so these diverge by far more than the tolerance.
    CHECK(near8(c.g, f.g, 4));
    CHECK(near8(c.b, f.b, 4));
    // Sanity: the pixel is partially-to-mostly opaque cyan (in the accumulation
    // regime), not background or a degenerate clamp.
    CHECK(static_cast<int>(f.g) > 40);
    CHECK(static_cast<int>(f.b) > 40);
    CHECK(ctx->validationClean());
}

// teeth (ADR-0021): the overlay graphics pass composites colored geometry over the
// volume render with alpha blending. Over an empty (background-only) volume, a
// red 50%-alpha quad covering the LEFT half blends to (red+background)/2 there and
// leaves the RIGHT half as background. No overlay (or no blend) changes both checks.
TEST_CASE("Renderer: overlay composites a quad over the volume (alpha blend)", "[vk][renderer]") {
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());
    auto rend = Renderer::create(*ctx);
    REQUIRE(rend.has_value());

    const GridDims d{8, 8, 8};
    auto vol = Volume::create(*ctx, uniformField(d, 0.0f, 0.0f), d); // empty -> background only
    REQUIRE(vol.has_value());

    RenderParams p;
    p.background = {0.0f, 0.0f, 1.0f, 1.0f}; // blue -> bytes (0,0,255)

    // Screen-space (identity transform) red quad at 50% alpha over the left half:
    // clip x in [-1,0], full height. Two triangles; cullMode is none so winding is
    // irrelevant.
    iv::vk::Overlay ov;
    const std::array<float, 4> red{1.0f, 0.0f, 0.0f, 0.5f};
    auto vtx = [&](float x, float y) {
        return iv::vk::OverlayVertex{{x, y, 0.0f}, red};
    };
    ov.triangles = {vtx(-1.0f, -1.0f), vtx(0.0f, -1.0f), vtx(0.0f, 1.0f),
                    vtx(-1.0f, -1.0f), vtx(0.0f, 1.0f),  vtx(-1.0f, 1.0f)};

    auto img = rend->render(*vol, 64, 64, p, &ov);
    REQUIRE(img.has_value());

    const auto left = img->at(16, 32); // under the quad: red*0.5 + blue*0.5 ~ (128,0,128)
    CHECK(near8(left.r, 128, 4));
    CHECK(near8(left.g, 0));
    CHECK(near8(left.b, 128, 4));
    const auto right = img->at(48, 32); // outside the quad: background blue
    CHECK(near8(right.r, 0));
    CHECK(near8(right.b, 255));
    CHECK(ctx->validationClean());
}

// ADR-0026: the view-projection must match the ADR-0012 ray camera exactly — a world
// point projects to the pixel whose camera ray passes through it. Verify it the rigorous
// way: project several world points, reconstruct the ADR-0012 ray for the resulting
// (continuous) pixel, and assert the point lies on that ray. Pure host (no GPU).
// teeth: transposing M or flipping a sign breaks collinearity.
namespace {

using Vec3 = std::array<float, 3>;
Vec3 vsub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
float vdot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 vcross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec3 vnorm(const Vec3& a) {
    const float n = std::sqrt(vdot(a, a));
    return n > 0.0f ? Vec3{a[0] / n, a[1] / n, a[2] / n} : a;
}

} // namespace

TEST_CASE("viewProjection matches the ADR-0012 ray camera", "[viewproj]") {
    RenderParams cam;
    cam.eye = {2.4f, 1.7f, 2.1f};
    cam.target = {0.5f, 0.5f, 0.5f};
    cam.up = {0.0f, 1.0f, 0.0f};
    cam.vfovRadians = 0.7f;
    const std::uint32_t W = 320;
    const std::uint32_t H = 240;
    const float aspect = static_cast<float>(W) / static_cast<float>(H);
    const auto M = iv::vk::viewProjection(cam, aspect);

    // ADR-0012 ray basis.
    const Vec3 w = vnorm(vsub(cam.eye, cam.target));
    const Vec3 u = vnorm(vcross(cam.up, w));
    const Vec3 v = vcross(w, u);
    const float halfH = std::tan(cam.vfovRadians * 0.5f);
    const float halfW = aspect * halfH;

    const std::array<Vec3, 5> pts{{{0.5f, 0.5f, 0.5f},
                                   {0.0f, 0.0f, 0.0f},
                                   {1.0f, 1.0f, 1.0f},
                                   {0.2f, 0.8f, 0.4f},
                                   {1.0f, 0.0f, 1.0f}}};
    for (const Vec3& p : pts) {
        const auto px = iv::vk::projectToPixel(M, p, W, H);
        REQUIRE(px[2] > 0.0f); // in front of the camera
        // Continuous pixel -> ray parameters (ADR-0012, continuous form s=px/W).
        const float s = px[0] / static_cast<float>(W);
        const float t = px[1] / static_cast<float>(H);
        const Vec3 dir{(2.0f * s - 1.0f) * halfW * u[0] + (1.0f - 2.0f * t) * halfH * v[0] - w[0],
                       (2.0f * s - 1.0f) * halfW * u[1] + (1.0f - 2.0f * t) * halfH * v[1] - w[1],
                       (2.0f * s - 1.0f) * halfW * u[2] + (1.0f - 2.0f * t) * halfH * v[2] - w[2]};
        // P must lie on the ray from eye: (P - eye) parallel to dir.
        const Vec3 pe = vsub(p, cam.eye);
        const Vec3 c = vcross(vnorm(pe), vnorm(dir));
        const float collinearity = std::sqrt(vdot(c, c)); // 0 when parallel
        CHECK(collinearity < 1e-3f);
    }

    // The cube center projects near the image center for a centered camera.
    const auto centerPx = iv::vk::projectToPixel(M, {0.5f, 0.5f, 0.5f}, W, H);
    CHECK(std::abs(centerPx[0] - static_cast<float>(W) * 0.5f) < 1.0f);
    CHECK(std::abs(centerPx[1] - static_cast<float>(H) * 0.5f) < 1.0f);
}
