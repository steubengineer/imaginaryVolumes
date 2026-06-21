// OpenType-MATH box layout (ADR-0033 §3). Teeth pin the metrics-from-font invariant (the thing
// that makes this "not a TeX processor"): the math face carries a MATH table; the fraction rule
// sits at the font's math axis height; a superscript raises the box ascent using the font's
// shift; and end-to-end the fraction renders a real rule with the numerator above / denominator
// below. The parser is tested separately (test_math_parse); here the tree is fixed and we check
// geometry.

#include "iv/text/font_set.hpp"
#include "iv/text/math.hpp"
#include "iv/text/math_layout.hpp"
#include "iv/text/shaper.hpp"
#include "iv/text/text_layout.hpp"
#include "iv/vk/context.hpp"
#include "iv/vk/renderer.hpp"
#include "iv/vk/volume.hpp"

#include "catch_amalgamated.hpp"

#include <complex>
#include <cstdint>
#include <vector>

using iv::text::Face;
using iv::text::FontSet;
using iv::text::MixedGlyphs;
using MC = iv::text::Shaper::MathConstant;

namespace {
const std::array<float, 4> kWhite{1.0f, 1.0f, 1.0f, 1.0f};
}

TEST_CASE("Math face carries the OpenType MATH table with sane constants (ADR-0033)", "[math]") {
    auto fonts = FontSet::create(40.0f);
    REQUIRE(fonts.has_value());
    auto& m = fonts->shaper(Face::Math);
    // teeth: if the math face were not NewCMMath (or the MATH table were missing), there is no
    // way to draw metrics-correct math — hasMathTable() is the gate.
    REQUIRE(m.hasMathTable());
    CHECK(m.mathConstant(MC::axisHeight) > 0.0f);
    CHECK(m.mathConstant(MC::fractionRuleThickness) > 0.0f);
    CHECK(m.mathConstant(MC::superscriptShiftUp) > 0.0f);
    CHECK(m.scriptScaleDown(false) > 0.4f);
    CHECK(m.scriptScaleDown(false) < 1.0f);
    CHECK(m.scriptScaleDown(true) < m.scriptScaleDown(false));
    // (NCM ships a MATH table in its text faces too, so we don't assert the roman face lacks
    // one — only that the math face has the canonical constants the layout draws through.)
}

TEST_CASE("Layout: superscript raises ascent; a fraction draws a rule (ADR-0033)", "[math]") {
    auto fonts = FontSet::create(40.0f);
    REQUIRE(fonts.has_value());

    auto run = [&](const char* src) {
        MixedGlyphs g(*fonts);
        iv::vk::Overlay ov;
        const auto tree = iv::text::math::parse(src);
        const auto met = iv::text::math::layout(g, ov, *fonts, tree, 0.0f, 0.0f, 256u, 256u, 40.0f,
                                                kWhite);
        g.finish(ov);
        struct R {
            iv::text::math::Metrics m;
            std::size_t glyphs;
            std::size_t rules;
        };
        return R{met, ov.glyphs.size(), ov.screenTriangles.size()};
    };

    const auto x = run("x");
    const auto x2 = run("x^2");
    // teeth: if scripts were ignored (laid out inline at full size) the ascent would not grow.
    CHECK(x2.m.ascent > x.m.ascent + 1.0f);
    CHECK(x2.glyphs > x.glyphs);
    CHECK(x.rules == 0u); // a plain atom emits no rule

    const auto frac = run("\\frac{1}{2}");
    CHECK(frac.rules > 0u);        // the fraction bar is a screen-space rule (teeth: none -> 0)
    CHECK(frac.m.ascent > 0.0f);   // numerator sits above the baseline
    CHECK(frac.m.descent > 0.0f);  // denominator below
}

TEST_CASE("Layout: the fraction rule sits at the font's math axis (metrics-from-font)", "[math]") {
    auto fonts = FontSet::create(40.0f);
    REQUIRE(fonts.has_value());
    auto& m = fonts->shaper(Face::Math);
    const float sizePx = 40.0f;
    const float axisPx = m.mathConstant(MC::axisHeight) * sizePx / static_cast<float>(m.unitsPerEm());
    REQUIRE(axisPx > 1.0f);

    MixedGlyphs g(*fonts);
    iv::vk::Overlay ov;
    const float penX = 100.0f;
    const float penY = 100.0f;
    const std::uint32_t W = 256;
    const std::uint32_t H = 256;
    const auto tree = iv::text::math::parse("\\frac{1}{2}");
    iv::text::math::layout(g, ov, *fonts, tree, penX, penY, W, H, sizePx, kWhite);
    g.finish(ov);

    // The only screen geometry is the fraction rule (6 verts). Recover its center y (px) from the
    // NDC (y-down) vertices: py = (pos.y + 1) * halfH.
    REQUIRE(ov.screenTriangles.size() == 6u);
    const float halfH = static_cast<float>(H) * 0.5f;
    float ymin = 1e9f;
    float ymax = -1e9f;
    for (const auto& v : ov.screenTriangles) {
        const float py = (v.pos[1] + 1.0f) * halfH;
        ymin = std::min(ymin, py);
        ymax = std::max(ymax, py);
    }
    const float ruleCenter = 0.5f * (ymin + ymax);
    // The rule is axisPx ABOVE the baseline (smaller y, top-left origin): penY - axisPx.
    // teeth: a hardcoded axis (e.g. 0) puts the rule on the baseline (penY) — off by axisPx.
    CHECK(ruleCenter == Catch::Approx(penY - axisPx).margin(1.5f));
}

TEST_CASE("Layout: glyph variants stretch delimiters/radicals (ADR-0033 stage 3)", "[math]") {
    auto fonts = FontSet::create(40.0f);
    REQUIRE(fonts.has_value());
    auto& m = fonts->shaper(Face::Math);
    const std::uint32_t paren = m.glyphForCodepoint(U'(');
    REQUIRE(paren != 0u);

    // A large target height selects a taller variant glyph than the base paren.
    const std::uint32_t big = m.glyphVariant(paren, true, 5000.0f);
    // teeth: if variants were ignored (return the input), big == paren and there is no growth.
    REQUIRE(big != paren);
    const auto eBase = m.encodeGlyph(paren);
    const auto eBig = m.encodeGlyph(big);
    const float hBase = eBase.extents.maxY - eBase.extents.minY;
    const float hBig = eBig.extents.maxY - eBig.extents.minY;
    CHECK(hBig > hBase * 1.5f); // the chosen variant is substantially taller
}

TEST_CASE("Layout: radical/overline/accent emit rules + marks (ADR-0033 stage 3)", "[math]") {
    auto fonts = FontSet::create(40.0f);
    REQUIRE(fonts.has_value());
    auto run = [&](const char* src) {
        MixedGlyphs g(*fonts);
        iv::vk::Overlay ov;
        const auto tree = iv::text::math::parse(src);
        const auto met = iv::text::math::layout(g, ov, *fonts, tree, 0.0f, 0.0f, 256u, 256u, 40.0f,
                                                kWhite);
        g.finish(ov);
        struct R {
            iv::text::math::Metrics m;
            std::size_t glyphs;
            std::size_t rules;
        };
        return R{met, ov.glyphs.size(), ov.screenTriangles.size()};
    };

    const auto x = run("x");
    // \sqrt draws a vinculum rule (and the surd raises the box well above the radicand).
    const auto sqrtX = run("\\sqrt{x}");
    CHECK(sqrtX.rules > 0u); // teeth: no vinculum -> 0 rules
    CHECK(sqrtX.m.ascent > x.m.ascent + 2.0f);
    CHECK(sqrtX.glyphs > x.glyphs); // the surd glyph is added

    // \overline draws a rule above the base.
    const auto over = run("\\overline{z}");
    CHECK(over.rules > 0u);
    CHECK(over.m.ascent > run("z").m.ascent + 1.0f);

    // \hat adds an accent glyph above the base, raising the ascent (teeth: accent dropped -> equal).
    const auto hatX = run("\\hat{x}");
    CHECK(hatX.glyphs > x.glyphs);
    CHECK(hatX.m.ascent > x.m.ascent + 1.0f);

    // A stretchy \left(...\right) around a tall fraction is taller than around a small body.
    const auto smallDelim = run("\\left(x\\right)");
    const auto tallDelim = run("\\left(\\frac{a}{b}\\right)");
    CHECK(tallDelim.m.ascent + tallDelim.m.descent > smallDelim.m.ascent + smallDelim.m.descent + 4.0f);
}

// End-to-end: \frac{1}{2} renders a real horizontal rule with ink above (1) and below (2),
// validation-clean. teeth: drop the rule and the wide bright bar row vanishes.
TEST_CASE("Layout: a fraction renders a rule with numerator above / denominator below",
          "[vk][math]") {
    using iv::GridDims;
    using iv::vk::Context;
    using iv::vk::Renderer;
    using iv::vk::Volume;

    auto ctx = Context::create();
    REQUIRE(ctx.has_value());
    auto rend = Renderer::create(*ctx);
    REQUIRE(rend.has_value());
    auto fonts = FontSet::create(60.0f);
    REQUIRE(fonts.has_value());

    const GridDims d{8, 8, 8};
    auto vol = Volume::create(*ctx, std::vector<std::complex<float>>(d.count(), {0.0f, 0.0f}), d);
    REQUIRE(vol.has_value());

    const std::uint32_t W = 200;
    const std::uint32_t H = 200;
    iv::vk::Overlay ov;
    MixedGlyphs g(*fonts);
    // A two-digit fraction makes the bar much wider than any single digit stroke.
    const auto tree = iv::text::math::parse("\\frac{12}{34}");
    iv::text::math::layout(g, ov, *fonts, tree, 40.0f, 105.0f, W, H, 60.0f, kWhite);
    g.finish(ov);
    REQUIRE_FALSE(ov.glyphs.empty());
    REQUIRE_FALSE(ov.screenTriangles.empty());

    iv::vk::RenderParams p;
    p.background = {0.0f, 0.0f, 0.0f, 1.0f};
    auto img = rend->render(*vol, W, H, p, &ov);
    REQUIRE(img.has_value());

    // Per-row bright-pixel count; the fraction bar is the widest bright row.
    std::vector<int> rowBright(H, 0);
    for (std::uint32_t y = 0; y < H; ++y) {
        for (std::uint32_t x = 0; x < W; ++x) {
            if (img->at(x, y).g > 120) {
                ++rowBright[y];
            }
        }
    }
    std::uint32_t barRow = 0;
    int barMax = 0;
    for (std::uint32_t y = 0; y < H; ++y) {
        if (rowBright[y] > barMax) {
            barMax = rowBright[y];
            barRow = y;
        }
    }
    INFO("bar row=" << barRow << " width=" << barMax);
    CHECK(barMax > 35); // a wide rule across both digits, not a glyph stroke (teeth: no rule)

    auto inkBetween = [&](std::uint32_t y0, std::uint32_t y1) {
        int n = 0;
        for (std::uint32_t y = y0; y < y1; ++y) {
            n += rowBright[y];
        }
        return n;
    };
    CHECK(inkBetween(0, barRow > 6 ? barRow - 6 : 0) > 10);   // numerator above the bar
    CHECK(inkBetween(barRow + 6, H) > 10);                    // denominator below
    CHECK(ctx->validationClean());
}
