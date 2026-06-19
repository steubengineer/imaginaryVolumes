#include "iv/text/bundled_font.hpp"
#include "iv/text/shaper.hpp"

#include "catch_amalgamated.hpp"

#include <array>
#include <cstddef>
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
