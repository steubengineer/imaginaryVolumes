#ifndef IV_TEXT_TEXT_LAYOUT_HPP
#define IV_TEXT_TEXT_LAYOUT_HPP

// Text layout for the overlay (ADR-0023): shape a string, encode its glyphs into
// the Shaper's Slug atlas, and append the resulting glyph quads (+ the atlas) to an
// iv::vk::Overlay for the renderer to draw. This is the bridge between the text
// layer (iv::text, HarfBuzz behind it) and the renderer's overlay geometry; it adds
// no HarfBuzz type to the renderer (the Overlay carries plain quads + an int atlas).

#include "iv/text/shaper.hpp"
#include "iv/vk/renderer.hpp" // iv::vk::Overlay, GlyphVertex

#include <array>
#include <cstdint>
#include <string_view>

namespace iv::text {

struct TextStyle {
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f}; // RGBA, straight alpha
    // Render size in pixels; 0 = use the Shaper's configured size. The Slug atlas is
    // size-independent (font-unit outlines), so one Shaper renders any size by scaling
    // the quads + shaped advances (ADR-0023).
    float pixelSize{0.0f};
};

// Shape `utf8` with `shaper`, encode its glyphs into the shaper's atlas, and append
// the resulting Slug glyph quads to `overlay.glyphs`, replacing `overlay.glyphAtlas`
// with the shaper's (cumulative) atlas. The pen starts at the text baseline origin
// (penXpx, penYpx) in pixels, in a top-left-origin framebuffer of (fbWidth,
// fbHeight) pixels; quad positions are converted to clip space (NDC). Returns the
// total horizontal pen advance in pixels.
//
// One Shaper backs one Overlay's glyph atlas: all appended runs must use the same
// `shaper` (their glyphLoc offsets index its single atlas).
float appendText(iv::vk::Overlay& overlay, Shaper& shaper, std::string_view utf8, float penXpx,
                 float penYpx, std::uint32_t fbWidth, std::uint32_t fbHeight,
                 const TextStyle& style = {});

} // namespace iv::text

#endif // IV_TEXT_TEXT_LAYOUT_HPP
