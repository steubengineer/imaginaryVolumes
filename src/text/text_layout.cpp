#include "iv/text/text_layout.hpp"

namespace iv::text {

float appendText(iv::vk::Overlay& overlay, Shaper& shaper, std::string_view utf8, float penXpx,
                 float penYpx, std::uint32_t fbWidth, std::uint32_t fbHeight,
                 const TextStyle& style) {
    const auto glyphs = shaper.shape(utf8);

    // The atlas is size-independent (font-unit outlines); the render size comes from
    // the quad/advance scaling, so one Shaper renders any size (ADR-0023). advScale
    // converts the shaper's pixel-size advances/offsets to the requested size.
    const float baseSize = shaper.pixelSize();
    const float size = style.pixelSize > 0.0f ? style.pixelSize : baseSize;
    const float advScale = size / baseSize;
    const float scale = size / static_cast<float>(shaper.unitsPerEm()); // em units -> px
    const float halfW = static_cast<float>(fbWidth) * 0.5f;
    const float halfH = static_cast<float>(fbHeight) * 0.5f;
    // Pixel (top-left origin) -> Vulkan clip space (NDC): (0,0) top-left -> (-1,-1).
    const auto ndcX = [&](float px) { return px / halfW - 1.0f; };
    const auto ndcY = [&](float px) { return px / halfH - 1.0f; };
    // Pad the quad slightly beyond the ink box so antialiased edge pixels are not
    // clipped (the Slug shader returns zero coverage outside the outline).
    const float pad = static_cast<float>(shaper.unitsPerEm()) * 0.05f; // em units

    float penX = penXpx;
    float penY = penYpx;
    for (const auto& g : glyphs) {
        const EncodedGlyph& e = shaper.encodeGlyph(g.glyphId);
        if (!e.blank) {
            const float ox = penX + g.xOffset * advScale;
            const float oy = penY - g.yOffset * advScale; // em y-up -> screen y-down baseline
            const float x0 = e.extents.minX - pad;
            const float x1 = e.extents.maxX + pad;
            const float y0 = e.extents.minY - pad;
            const float y1 = e.extents.maxY + pad;

            // Build a quad corner: em coord -> screen px -> NDC; texcoord = em coord
            // (the Slug renderCoord), glyphLoc = the glyph's atlas texel offset.
            const auto corner = [&](float emX, float emY) {
                iv::vk::GlyphVertex v;
                v.pos = {ndcX(ox + emX * scale), ndcY(oy - emY * scale)};
                v.texcoord = {emX, emY};
                v.glyphLoc = e.atlasOffset;
                v.color = style.color;
                return v;
            };
            const iv::vk::GlyphVertex bl = corner(x0, y0);
            const iv::vk::GlyphVertex br = corner(x1, y0);
            const iv::vk::GlyphVertex tr = corner(x1, y1);
            const iv::vk::GlyphVertex tl = corner(x0, y1);
            overlay.glyphs.push_back(bl);
            overlay.glyphs.push_back(br);
            overlay.glyphs.push_back(tr);
            overlay.glyphs.push_back(bl);
            overlay.glyphs.push_back(tr);
            overlay.glyphs.push_back(tl);
        }
        penX += g.xAdvance * advScale;
        penY += g.yAdvance * advScale;
    }

    // The renderer uploads this as the Slug atlas texel buffer (ADR-0023). It is the
    // shaper's whole (cumulative) atlas, so glyphLoc offsets above stay valid.
    const auto atlas = shaper.glyphAtlas();
    overlay.glyphAtlas.assign(atlas.begin(), atlas.end());

    return penX - penXpx;
}

} // namespace iv::text
