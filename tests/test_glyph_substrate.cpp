// Mixed-font glyph substrate (ADR-0032): multiple faces (roman / true italic / OpenType
// MATH) coexisting in ONE overlay via a single MERGED Slug atlas with rebased glyphLoc, so
// the renderer / pipeline / GlyphVertex stay unchanged. Teeth:
//   - FontSet loads three GENUINELY distinct faces (the italic 'f' outline differs from the
//     roman 'f' — not the same font bundled thrice).
//   - MixedGlyphs::finish concatenates the faces' atlases and REBASES each face's glyphLoc
//     (the italic glyph indexes the appended italic region, not roman's).
//   - Backward compat: a roman-only MixedGlyphs build is byte-identical to appendText (so the
//     M7/M8 single-face text path is unchanged — "mixed-font is additive").
//   - End-to-end: two faces in one merged-atlas overlay both render, validation-clean.

#include "iv/text/bundled_font.hpp"
#include "iv/text/font_set.hpp"
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
using iv::text::Shaper;

TEST_CASE("FontSet loads three distinct faces (roman/italic/math)", "[text]") {
    auto fonts = FontSet::create(48.0f);
    REQUIRE(fonts.has_value());

    Shaper& roman = fonts->shaper(Face::Roman);
    Shaper& italic = fonts->shaper(Face::Italic);
    Shaper& math = fonts->shaper(Face::Math);

    // Each face shapes a letter to a real (non-.notdef) glyph.
    const auto rf = roman.shape("f");
    const auto itf = italic.shape("f");
    const auto mx = math.shape("x");
    REQUIRE(rf.size() == 1);
    REQUIRE(itf.size() == 1);
    REQUIRE(mx.size() == 1);
    CHECK(rf[0].glyphId != 0u);
    CHECK(itf[0].glyphId != 0u);
    CHECK(mx[0].glyphId != 0u);

    // The roman and italic 'f' have DIFFERENT outlines => different encoded atlas bytes.
    // teeth: if bundledFontItalic() returned the roman bytes (same font loaded twice), the
    // two atlases would be byte-equal and this fails.
    REQUIRE_FALSE(roman.encodeGlyph(rf[0].glyphId).blank);
    REQUIRE_FALSE(italic.encodeGlyph(itf[0].glyphId).blank);
    const auto ra = roman.glyphAtlas();
    const auto ia = italic.glyphAtlas();
    bool differ = ra.size() != ia.size();
    for (std::size_t i = 0; !differ && i < ra.size(); ++i) {
        differ = ra[i] != ia[i];
    }
    CHECK(differ);
}

TEST_CASE("MixedGlyphs::finish merges atlases and rebases glyphLoc (ADR-0032)", "[text]") {
    auto fonts = FontSet::create(48.0f);
    REQUIRE(fonts.has_value());

    const std::uint32_t W = 256;
    const std::uint32_t H = 256;
    MixedGlyphs mg(*fonts);
    mg.appendRun(Face::Roman, "A", 20.0f, 140.0f, W, H, {{1.0f, 1.0f, 1.0f, 1.0f}});
    mg.appendRun(Face::Italic, "A", 60.0f, 140.0f, W, H, {{1.0f, 1.0f, 1.0f, 1.0f}});
    REQUIRE_FALSE(mg.empty());

    iv::vk::Overlay ov;
    mg.finish(ov);
    REQUIRE(mg.empty()); // finish() resets the builder

    // Roman 'A' (6 verts) then italic 'A' (6 verts), each a single non-blank glyph.
    REQUIRE(ov.glyphs.size() == 12);

    const auto romanBytes = fonts->shaper(Face::Roman).glyphAtlas().size();
    const auto italicBytes = fonts->shaper(Face::Italic).glyphAtlas().size();
    const auto romanTexels = static_cast<std::uint32_t>(romanBytes / 4);

    // The merged atlas is roman's atlas followed by italic's (concatenation).
    CHECK(ov.glyphAtlas.size() == romanBytes + italicBytes);

    // Roman is face 0 at base 0; the italic glyph is rebased past roman's region.
    CHECK(ov.glyphs[0].glyphLoc < romanTexels);
    // teeth: without the per-face rebase the italic glyphLoc would be its FACE-LOCAL 0
    // (< romanTexels); the merge adds base == romanTexels (italic 'A' is the italic shaper's
    // first encoded glyph, local offset 0).
    CHECK(ov.glyphs[6].glyphLoc >= romanTexels);
    CHECK(ov.glyphs[6].glyphLoc == romanTexels);
}

TEST_CASE("Roman-only MixedGlyphs is byte-identical to appendText (backward compat)", "[text]") {
    auto fonts = FontSet::create(48.0f);
    REQUIRE(fonts.has_value());
    auto solo = Shaper::create(iv::text::bundledFont(), 48.0f); // the pre-ADR single-face path
    REQUIRE(solo.has_value());

    const std::uint32_t W = 256;
    const std::uint32_t H = 256;
    const std::string_view s = "Vol |f| arg(f)";
    const std::array<float, 4> white{1.0f, 1.0f, 1.0f, 1.0f};

    iv::vk::Overlay a;
    iv::text::appendText(a, *solo, s, 20.0f, 140.0f, W, H, {white});

    iv::vk::Overlay b;
    MixedGlyphs mg(*fonts);
    mg.appendRun(Face::Roman, s, 20.0f, 140.0f, W, H, {white});
    mg.finish(b);

    // Same shaped roman glyphs, same quad geometry, same single-face atlas (base 0).
    // teeth: any divergence in the mixed-font roman path (scale, origin, atlas) breaks this,
    // i.e. the single-face contract (ADR-0032) is no longer additive.
    REQUIRE(a.glyphs.size() == b.glyphs.size());
    REQUIRE_FALSE(a.glyphs.empty());
    for (std::size_t i = 0; i < a.glyphs.size(); ++i) {
        CHECK(a.glyphs[i].pos == b.glyphs[i].pos);
        CHECK(a.glyphs[i].texcoord == b.glyphs[i].texcoord);
        CHECK(a.glyphs[i].glyphLoc == b.glyphs[i].glyphLoc);
        CHECK(a.glyphs[i].color == b.glyphs[i].color);
    }
    REQUIRE(a.glyphAtlas.size() == b.glyphAtlas.size());
    CHECK(a.glyphAtlas == b.glyphAtlas);
}

// End-to-end: two faces in ONE merged-atlas overlay both render through the unchanged
// renderer, validation-clean. Roman text on the left half, italic on the right half; both
// halves must paint ink. teeth: if a face contributed no atlas / the merge dropped a face,
// that half stays dark.
TEST_CASE("Two faces render from one merged overlay (ADR-0032)", "[vk][text]") {
    using iv::GridDims;
    using iv::vk::Context;
    using iv::vk::Renderer;
    using iv::vk::Volume;

    auto ctx = Context::create();
    REQUIRE(ctx.has_value());
    auto rend = Renderer::create(*ctx);
    REQUIRE(rend.has_value());
    auto fonts = FontSet::create(64.0f);
    REQUIRE(fonts.has_value());

    const GridDims d{8, 8, 8};
    auto vol = Volume::create(*ctx, std::vector<std::complex<float>>(d.count(), {0.0f, 0.0f}), d);
    REQUIRE(vol.has_value()); // empty field -> background, so only the glyphs paint

    const std::uint32_t W = 256;
    const std::uint32_t H = 128;
    iv::vk::Overlay ov;
    MixedGlyphs mg(*fonts);
    mg.appendRun(Face::Roman, "Re", 16.0f, 90.0f, W, H, {{1.0f, 1.0f, 1.0f, 1.0f}});  // left
    mg.appendRun(Face::Italic, "z", 168.0f, 90.0f, W, H, {{1.0f, 1.0f, 1.0f, 1.0f}}); // right
    mg.finish(ov);
    REQUIRE_FALSE(ov.glyphs.empty());

    iv::vk::RenderParams p;
    p.background = {0.0f, 0.0f, 0.0f, 1.0f};
    auto img = rend->render(*vol, W, H, p, &ov);
    REQUIRE(img.has_value());

    const auto brightCount = [&](std::uint32_t x0, std::uint32_t x1) {
        int n = 0;
        for (std::uint32_t y = 0; y < H; ++y) {
            for (std::uint32_t x = x0; x < x1; ++x) {
                if (img->at(x, y).g > 120) {
                    ++n;
                }
            }
        }
        return n;
    };
    const int left = brightCount(0, W / 2);
    const int right = brightCount(W / 2, W);
    INFO("left(roman)=" << left << " right(italic)=" << right);
    CHECK(left > 30);  // roman "Re" painted
    CHECK(right > 15); // italic "z" painted (its glyphs index the rebased atlas region)
    CHECK(ctx->validationClean());
}
