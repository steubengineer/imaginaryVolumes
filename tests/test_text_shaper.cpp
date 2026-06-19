#include "iv/text/bundled_font.hpp"
#include "iv/text/shaper.hpp"
#include "iv/text/text_layout.hpp"
#include "iv/vk/context.hpp"
#include "iv/vk/renderer.hpp"
#include "iv/vk/volume.hpp"
#include "iv/volume.hpp"

#include "catch_amalgamated.hpp"

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

using iv::text::bundledFont;
using iv::text::Shaper;
using iv::text::ShapedGlyph;

namespace {

float totalAdvance(const std::vector<ShapedGlyph>& glyphs) {
    return std::accumulate(glyphs.begin(), glyphs.end(), 0.0f,
                           [](float a, const ShapedGlyph& g) { return a + g.xAdvance; });
}

} // namespace

// The embedded default face is present and parseable (the embed step worked).
TEST_CASE("bundled New Computer Modern face loads", "[text]") {
    const auto font = bundledFont();
    REQUIRE(font.size() > 100000); // ~0.68 MB OTF

    auto shaper = Shaper::create(font, 64.0f);
    REQUIRE(shaper);
    CHECK(shaper->pixelSize() == 64.0f);
    CHECK(shaper->unitsPerEm() == 1000u); // NCM is a 1000-upem OpenType face
}

// Recorded reference (ADR-0022 verification): shaping a known string with the
// bundled font yields a known glyph count and positive, monotonic-cluster layout.
// teeth: the absolute advance pins the pixel-size scale math (value/64); dropping
// the /64 conversion gives ~3000 px and the window fails.
TEST_CASE("shaping a known string matches the recorded layout", "[text]") {
    auto shaper = Shaper::create(bundledFont(), 64.0f);
    REQUIRE(shaper);

    const auto glyphs = shaper->shape("Volume");
    REQUIRE(glyphs.size() == 6); // no ligatures in "Volume": one glyph per letter

    // All glyphs resolved (none is .notdef) and clusters are non-decreasing,
    // mapping back to the six input byte positions in order.
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        CHECK(glyphs[i].glyphId != 0u);
        CHECK(glyphs[i].cluster == static_cast<std::uint32_t>(i));
        CHECK(glyphs[i].xAdvance > 0.0f);
    }

    // Absolute layout window: six letters at a 64 px em land in a plausible pixel
    // range (each letter averages well under one em wide). A broken scale falls
    // far outside this.
    const float total = totalAdvance(glyphs);
    CHECK(total > 120.0f);
    CHECK(total < 260.0f);
}

// teeth on the scale contract: advances are linear in pixel size. Shaping the
// same text at 2x the size doubles the total advance (within rounding). A wrong
// scale or a missing /64 conversion breaks the ratio.
TEST_CASE("advances scale linearly with pixel size", "[text]") {
    auto small = Shaper::create(bundledFont(), 50.0f);
    auto large = Shaper::create(bundledFont(), 100.0f);
    REQUIRE(small);
    REQUIRE(large);

    const float a = totalAdvance(small->shape("imaginary"));
    const float b = totalAdvance(large->shape("imaginary"));
    REQUIRE(a > 0.0f);
    CHECK(b / a == Catch::Approx(2.0f).epsilon(0.01)); // 26.6 rounding tolerance
}

// teeth on OpenType shaping itself (GSUB): the serif face ligates "ffi" into a
// single glyph. A naive 1:1 codepoint->glyph map (no shaping) would emit three
// glyphs. This is the load-bearing proof that we run a real shaper, not a table
// lookup; perturbing it (disable shaping / wrong feature handling) goes red.
TEST_CASE("OpenType ligatures are applied (real shaping)", "[text]") {
    auto shaper = Shaper::create(bundledFont(), 48.0f);
    REQUIRE(shaper);

    const auto ffi = shaper->shape("ffi");
    const auto f = shaper->shape("f");
    const auto i = shaper->shape("i");
    REQUIRE(f.size() == 1);
    REQUIRE(i.size() == 1);

    // "ffi" ligates to fewer glyphs than its three component glyphs.
    CHECK(ffi.size() < 3);
    // The ligature still advances roughly like the three separate letters (it is
    // a substitution, not a deletion): well over a single 'i'.
    CHECK(totalAdvance(ffi) > totalAdvance(i));
}

TEST_CASE("empty input shapes to no glyphs", "[text]") {
    auto shaper = Shaper::create(bundledFont(), 32.0f);
    REQUIRE(shaper);
    CHECK(shaper->shape("").empty());
}

// ADR-0023: encoding a glyph outline into the Slug atlas via libharfbuzz-gpu. This
// exercises the experimental encode path on the CPU (no GPU): a real glyph yields
// a non-empty, ivec4-aligned, font-unit-extent encoding; whitespace stays blank;
// the atlas grows contiguously and caches per glyph id.
TEST_CASE("glyph outlines encode into a Slug atlas", "[text][glyph]") {
    auto shaper = Shaper::create(bundledFont(), 64.0f);
    REQUIRE(shaper);
    REQUIRE(shaper->glyphAtlas().empty()); // nothing encoded yet

    const auto hs = shaper->shape("H");
    REQUIRE(hs.size() == 1);
    const auto& e = shaper->encodeGlyph(hs[0].glyphId);

    INFO("H extents (font units): " << e.extents.minX << "," << e.extents.minY << " .. "
                                    << e.extents.maxX << "," << e.extents.maxY);
    CHECK_FALSE(e.blank); // 'H' has an outline
    REQUIRE_FALSE(e.extents.empty());
    // Extents are in FONT units (upem 1000), not pixels: a capital H is hundreds of
    // units wide/tall. (At the 64 px shaping scale it would be only ~48 — the teeth
    // that the encode uses font units.)
    CHECK(e.extents.maxX - e.extents.minX > 200.0f);
    CHECK(e.extents.maxY - e.extents.minY > 200.0f);
    CHECK(e.extents.maxX < 1200.0f);
    CHECK(e.extents.maxY < 1200.0f);

    const auto atlas0 = shaper->glyphAtlas();
    CHECK(atlas0.size() >= 4);        // at least the header texel
    CHECK(atlas0.size() % 4 == 0);    // whole ivec4 texels
    CHECK(e.atlasOffset == 0u);       // first glyph starts at texel 0

    // Caching: re-encoding the same glyph returns the same entry, atlas unchanged.
    const std::size_t n0 = atlas0.size();
    const auto& eAgain = shaper->encodeGlyph(hs[0].glyphId);
    CHECK(eAgain.atlasOffset == e.atlasOffset);
    CHECK(shaper->glyphAtlas().size() == n0);

    // A distinct glyph appends after the first (contiguous, texel-aligned offset).
    const auto os = shaper->shape("o");
    REQUIRE(os.size() == 1);
    const auto& eo = shaper->encodeGlyph(os[0].glyphId);
    CHECK_FALSE(eo.blank);
    CHECK(eo.atlasOffset == n0 / 4);
    CHECK(shaper->glyphAtlas().size() > n0);

    // Whitespace has no outline: it encodes to nothing and stays blank.
    const auto sp = shaper->shape(" ");
    REQUIRE(sp.size() == 1);
    CHECK(shaper->encodeGlyph(sp[0].glyphId).blank);
}

namespace {

// A magnitude-zero field: the volume is fully transparent, so the render is just
// the background — a clean canvas to read back glyph coverage on.
std::vector<std::complex<float>> emptyField(iv::GridDims d) {
    return std::vector<std::complex<float>>(d.count(), std::complex<float>{0.0f, 0.0f});
}

// Count bright (text ink) pixels in a readback, and on one row.
struct InkCount {
    int total{0};
    int onRow{0};
};
InkCount countInk(const iv::vk::ImageReadback& img, std::uint32_t w, std::uint32_t h,
                  std::uint32_t row) {
    InkCount c;
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const auto px = img.at(x, y);
            if (px.r > 150 && px.g > 150 && px.b > 150) {
                ++c.total;
                if (y == row) {
                    ++c.onRow;
                }
            }
        }
    }
    return c;
}

} // namespace

// ADR-0023 verification: a shaped+encoded glyph renders into the overlay via the
// Slug GPU path (headless readback). A white 'H' on a black background paints
// substantial ink (but not the whole frame), its mid-height row crosses the stems
// and crossbar, and a point well outside the glyph stays background.
// teeth: with no glyph encoded the atlas/quads are empty and `ink` is 0 (the
// >0 lower bound fails) — demonstrated by skipping the Slug encode.
TEST_CASE("Slug glyphs render into the overlay with correct coverage", "[glyph][vk]") {
    using iv::GridDims;
    using iv::vk::Context;
    using iv::vk::Renderer;
    using iv::vk::Volume;

    auto ctx = Context::create();
    REQUIRE(ctx.has_value());
    auto rend = Renderer::create(*ctx);
    REQUIRE(rend.has_value());
    const GridDims d{8, 8, 8};
    auto vol = Volume::create(*ctx, emptyField(d), d); // transparent -> background only
    REQUIRE(vol.has_value());

    iv::vk::RenderParams p;
    p.background = {0.0f, 0.0f, 0.0f, 1.0f}; // black

    const std::uint32_t W = 128;
    const std::uint32_t H = 128;
    auto shaper = Shaper::create(bundledFont(), 96.0f);
    REQUIRE(shaper);

    iv::vk::Overlay ov;
    // Baseline at (28, 97) px places the ~66 px cap-height 'H' roughly centered.
    iv::text::appendText(ov, *shaper, "H", 28.0f, 97.0f, W, H, {{1.0f, 1.0f, 1.0f, 1.0f}});
    REQUIRE_FALSE(ov.glyphs.empty());
    REQUIRE_FALSE(ov.glyphAtlas.empty());

    auto img = rend->render(*vol, W, H, p, &ov);
    REQUIRE(img.has_value());

    const InkCount ink = countInk(*img, W, H, 64);
    INFO("ink total=" << ink.total << " row64=" << ink.onRow);
    CHECK(ink.total > 200);      // the 'H' covers real area (teeth: no glyph -> 0)
    CHECK(ink.total < 8000);     // but not the whole 16384-pixel frame
    CHECK(ink.onRow >= 3);       // mid-height row crosses the two stems + crossbar

    const auto outside = img->at(118, 64); // right of the glyph -> background
    CHECK(outside.r < 24);
    CHECK(outside.g < 24);
    CHECK(outside.b < 24);

    CHECK(ctx->validationClean());
}

// ADR-0023 resolution independence: the SAME glyph rendered at two pixel sizes
// covers area in proportion to size^2 (the Slug coverage is analytic, so it scales
// cleanly rather than pixelating from a fixed atlas). Rendering 'H' at 48 px then
// 96 px (2x) quadruples the ink area (within tolerance).
TEST_CASE("Slug glyph coverage scales with size (resolution independence)", "[glyph][vk]") {
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

    iv::vk::RenderParams p;
    p.background = {0.0f, 0.0f, 0.0f, 1.0f};

    const std::uint32_t W = 256;
    const std::uint32_t H = 256;
    auto renderInk = [&](float pixelSize) -> int {
        auto shaper = Shaper::create(bundledFont(), pixelSize);
        REQUIRE(shaper);
        iv::vk::Overlay ov;
        iv::text::appendText(ov, *shaper, "H", 90.0f, 160.0f, W, H, {{1.0f, 1.0f, 1.0f, 1.0f}});
        auto img = rend->render(*vol, W, H, p, &ov);
        REQUIRE(img.has_value());
        return countInk(*img, W, H, 0).total;
    };

    const int inkSmall = renderInk(48.0f);
    const int inkLarge = renderInk(96.0f);
    INFO("ink small(48px)=" << inkSmall << " large(96px)=" << inkLarge);
    REQUIRE(inkSmall > 50);
    // Area ~ size^2 => 2x size -> ~4x ink. Wide tolerance for edge/rounding effects.
    CHECK(static_cast<double>(inkLarge) > 3.0 * inkSmall);
    CHECK(static_cast<double>(inkLarge) < 5.0 * inkSmall);
    CHECK(ctx->validationClean());
}

// Invalid inputs are rejected by value (ADR-0003), not by crashing in HarfBuzz.
TEST_CASE("Shaper::create rejects bad inputs", "[text]") {
    SECTION("non-positive pixel size") {
        CHECK_FALSE(Shaper::create(bundledFont(), 0.0f));
        CHECK_FALSE(Shaper::create(bundledFont(), -10.0f));
    }
    SECTION("empty font data") {
        CHECK_FALSE(Shaper::create(std::span<const std::byte>{}, 16.0f));
    }
    SECTION("garbage that is not a font") {
        const std::array<std::byte, 8> junk{}; // zeros: not a valid sfnt
        CHECK_FALSE(Shaper::create(std::span<const std::byte>(junk), 16.0f));
    }
}
