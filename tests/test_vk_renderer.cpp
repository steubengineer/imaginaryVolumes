#include "iv/vk/colormap_lut.hpp"
#include "iv/vk/context.hpp"
#include "iv/vk/renderer.hpp"
#include "iv/vk/volume.hpp"

#include "catch_amalgamated.hpp"

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
