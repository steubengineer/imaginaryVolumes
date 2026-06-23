#include "iv/text/text_layout.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace iv::text {

namespace {

// Push one glyph's 6-vertex quad (two triangles) into `out` (ADR-0023). `ox,oy` is the
// glyph origin in px (top-left-origin framebuffer); `scaleEmToPx` converts the glyph's
// em (font-unit) coordinates to px; `padEm` pads the ink box so antialiased edge pixels
// are not clipped (the Slug shader returns zero coverage outside the outline). `glyphLoc`
// is written verbatim (face-local during a mixed-font build; the merge rebases it).
// halfW/halfH = fb/2 for the px->NDC map. This is the single source of glyph quad geometry
// (appendText and MixedGlyphs both call it, so their output stays in lock-step).
void pushGlyphQuad(std::vector<iv::vk::GlyphVertex>& out, const EncodedGlyph& e, float ox,
                   float oy, float scaleEmToPx, float padEm, std::uint32_t glyphLoc, float halfW,
                   float halfH, const std::array<float, 4>& color) {
    const float x0 = e.extents.minX - padEm;
    const float x1 = e.extents.maxX + padEm;
    const float y0 = e.extents.minY - padEm;
    const float y1 = e.extents.maxY + padEm;
    // em coord -> screen px -> NDC (Vulkan clip, y-down: (0,0) top-left -> (-1,-1)); texcoord
    // = em coord (the Slug renderCoord). y is em-up, so screen y subtracts.
    const auto corner = [&](float emX, float emY) {
        iv::vk::GlyphVertex v;
        v.pos = {(ox + emX * scaleEmToPx) / halfW - 1.0f, (oy - emY * scaleEmToPx) / halfH - 1.0f};
        v.texcoord = {emX, emY};
        v.glyphLoc = glyphLoc;
        v.color = color;
        return v;
    };
    const iv::vk::GlyphVertex bl = corner(x0, y0);
    const iv::vk::GlyphVertex br = corner(x1, y0);
    const iv::vk::GlyphVertex tr = corner(x1, y1);
    const iv::vk::GlyphVertex tl = corner(x0, y1);
    out.push_back(bl);
    out.push_back(br);
    out.push_back(tr);
    out.push_back(bl);
    out.push_back(tr);
    out.push_back(tl);
}

// Shape `utf8` with `shaper` and append its glyph quads (face-local glyphLoc) to `out`,
// starting at baseline pen (penXpx, penYpx). Returns the horizontal advance (px). Shared by
// appendText (single face) and MixedGlyphs::appendRun (one face of a FontSet).
float layoutRun(std::vector<iv::vk::GlyphVertex>& out, Shaper& shaper, std::string_view utf8,
                float penXpx, float penYpx, std::uint32_t fbWidth, std::uint32_t fbHeight,
                const TextStyle& style) {
    const auto glyphs = shaper.shape(utf8);

    // The atlas is size-independent (font-unit outlines); the render size comes from the
    // quad/advance scaling, so one Shaper renders any size (ADR-0023).
    const float baseSize = shaper.pixelSize();
    const float size = style.pixelSize > 0.0f ? style.pixelSize : baseSize;
    const float advScale = size / baseSize;
    const float scale = size / static_cast<float>(shaper.unitsPerEm()); // em units -> px
    const float halfW = static_cast<float>(fbWidth) * 0.5f;
    const float halfH = static_cast<float>(fbHeight) * 0.5f;
    const float pad = static_cast<float>(shaper.unitsPerEm()) * 0.05f; // em units

    float penX = penXpx;
    float penY = penYpx;
    for (const auto& g : glyphs) {
        const EncodedGlyph& e = shaper.encodeGlyph(g.glyphId);
        if (!e.blank) {
            const float ox = penX + g.xOffset * advScale;
            const float oy = penY - g.yOffset * advScale; // em y-up -> screen y-down baseline
            pushGlyphQuad(out, e, ox, oy, scale, pad, e.atlasOffset, halfW, halfH, style.color);
        }
        penX += g.xAdvance * advScale;
        penY += g.yAdvance * advScale;
    }
    return penX - penXpx;
}

} // namespace

float appendText(iv::vk::Overlay& overlay, Shaper& shaper, std::string_view utf8, float penXpx,
                 float penYpx, std::uint32_t fbWidth, std::uint32_t fbHeight,
                 const TextStyle& style) {
    const float advance =
        layoutRun(overlay.glyphs, shaper, utf8, penXpx, penYpx, fbWidth, fbHeight, style);
    // The renderer uploads this as the Slug atlas texel buffer (ADR-0023). It is the shaper's
    // whole (cumulative) atlas, so the glyphLoc offsets above stay valid (single-face: base 0).
    const auto atlas = shaper.glyphAtlas();
    overlay.glyphAtlas.assign(atlas.begin(), atlas.end());
    return advance;
}

float MixedGlyphs::appendRun(Face face, std::string_view utf8, float penXpx, float penYpx,
                             std::uint32_t fbWidth, std::uint32_t fbHeight,
                             const TextStyle& style) {
    auto& out = perFace_[static_cast<std::size_t>(face)];
    return layoutRun(out, fonts_->shaper(face), utf8, penXpx, penYpx, fbWidth, fbHeight, style);
}

void MixedGlyphs::appendGlyph(Face face, std::uint32_t glyphId, float penXpx, float penYpx,
                              float pixelSize, std::uint32_t fbWidth, std::uint32_t fbHeight,
                              const std::array<float, 4>& color) {
    Shaper& sh = fonts_->shaper(face);
    const EncodedGlyph& e = sh.encodeGlyph(glyphId);
    if (e.blank) {
        return; // outline-less glyph (whitespace / .notdef): nothing to draw
    }
    const float scale = pixelSize / static_cast<float>(sh.unitsPerEm()); // em -> px
    const float pad = static_cast<float>(sh.unitsPerEm()) * 0.05f;
    pushGlyphQuad(perFace_[static_cast<std::size_t>(face)], e, penXpx, penYpx, scale, pad,
                  e.atlasOffset, static_cast<float>(fbWidth) * 0.5f,
                  static_cast<float>(fbHeight) * 0.5f, color);
}

MixedGlyphs::Marker MixedGlyphs::marker() const noexcept {
    Marker m;
    for (std::size_t f = 0; f < kFaceCount; ++f) {
        m.n[f] = perFace_[f].size();
    }
    return m;
}

void MixedGlyphs::rotateSince(const Marker& from, float angleRad, float pivotXpx, float pivotYpx,
                              std::uint32_t fbWidth, std::uint32_t fbHeight) {
    const float c = std::cos(angleRad);
    const float s = std::sin(angleRad);
    const float halfW = static_cast<float>(fbWidth) * 0.5f;
    const float halfH = static_cast<float>(fbHeight) * 0.5f;
    for (std::size_t f = 0; f < kFaceCount; ++f) {
        auto& quads = perFace_[f];
        for (std::size_t i = from.n[f]; i < quads.size(); ++i) {
            auto& pos = quads[i].pos;
            const float px = (pos[0] + 1.0f) * halfW; // NDC -> pixel (top-left origin)
            const float py = (pos[1] + 1.0f) * halfH;
            const float dx = px - pivotXpx;
            const float dy = py - pivotYpx;
            const float rx = c * dx - s * dy; // R(angleRad) in the y-down pixel frame
            const float ry = s * dx + c * dy;
            pos[0] = (pivotXpx + rx) / halfW - 1.0f;
            pos[1] = (pivotYpx + ry) / halfH - 1.0f;
        }
    }
}

void MixedGlyphs::finish(iv::vk::Overlay& overlay) {
    // Merge contributing faces in enum order (Roman, Italic, Math). Each used face's
    // cumulative Slug atlas is concatenated onto overlay.glyphAtlas at a running texel base,
    // and that face's accumulated quads are appended with glyphLoc rebased by the base — so
    // every glyph indexes its own face's region of the one merged atlas (ADR-0032). A face
    // with no quads adds no atlas bytes. Bases account for any atlas already on the overlay.
    for (std::size_t f = 0; f < kFaceCount; ++f) {
        auto& quads = perFace_[f];
        if (quads.empty()) {
            continue;
        }
        const auto atlas = fonts_->shaper(static_cast<Face>(f)).glyphAtlas();
        const auto base = static_cast<std::uint32_t>(overlay.glyphAtlas.size() / 4);
        overlay.glyphAtlas.insert(overlay.glyphAtlas.end(), atlas.begin(), atlas.end());
        for (iv::vk::GlyphVertex v : quads) {
            v.glyphLoc += base;
            overlay.glyphs.push_back(v);
        }
        quads.clear();
    }
}

bool MixedGlyphs::empty() const noexcept {
    for (const auto& quads : perFace_) {
        if (!quads.empty()) {
            return false;
        }
    }
    return true;
}

} // namespace iv::text
