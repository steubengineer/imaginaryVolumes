#ifndef IV_TEXT_TEXT_LAYOUT_HPP
#define IV_TEXT_TEXT_LAYOUT_HPP

// Text layout for the overlay (ADR-0023): shape a string, encode its glyphs into
// the Shaper's Slug atlas, and append the resulting glyph quads (+ the atlas) to an
// iv::vk::Overlay for the renderer to draw. This is the bridge between the text
// layer (iv::text, HarfBuzz behind it) and the renderer's overlay geometry; it adds
// no HarfBuzz type to the renderer (the Overlay carries plain quads + an int atlas).

#include "iv/text/font_set.hpp"
#include "iv/text/shaper.hpp"
#include "iv/vk/renderer.hpp" // iv::vk::Overlay, GlyphVertex

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

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

// Mixed-font glyph builder (ADR-0032): accumulates positioned glyph quads from one or more
// FontSet faces, then merges their Slug atlases into a SINGLE Overlay glyph channel (one
// atlas, one draw — the renderer/pipeline/GlyphVertex are unchanged). Build with
// appendRun()/appendGlyph(), then finish() once. A face that contributes no glyphs
// contributes no atlas bytes; Roman is merged first (base 0), so roman-only output matches
// the single-face appendText path (the backward-compat invariant). For composing the math
// layout (ADR-0033) with plain text in one overlay.
class MixedGlyphs {
public:
    explicit MixedGlyphs(FontSet& fonts) noexcept : fonts_(&fonts) {}

    // Shape `utf8` in `face` and append its glyph quads at the baseline pen (penXpx,
    // penYpx), top-left origin, in an (fbWidth x fbHeight) framebuffer; returns the
    // horizontal advance in pixels. (Plain text spans and the ADR-0032 mixed-font test.)
    float appendRun(Face face, std::string_view utf8, float penXpx, float penYpx,
                    std::uint32_t fbWidth, std::uint32_t fbHeight, const TextStyle& style = {});

    // Append ONE positioned glyph (by glyphId in `face`) with its em origin at (penXpx,
    // penYpx) px, rendered at `pixelSize` px. For math layout (ADR-0033), which computes
    // each glyph's position itself. A blank (outline-less) glyph is a no-op.
    void appendGlyph(Face face, std::uint32_t glyphId, float penXpx, float penYpx,
                     float pixelSize, std::uint32_t fbWidth, std::uint32_t fbHeight,
                     const std::array<float, 4>& color);

    // Merge the contributing faces (Roman, then Italic, then Math) into `overlay`: append
    // each used face's atlas to overlay.glyphAtlas at a running base, append this builder's
    // accumulated quads to overlay.glyphs with glyphLoc rebased by that base, then reset the
    // builder. Appends to whatever the overlay already holds (so it composes after
    // appendText/buildAnnotations roman text, which is base 0).
    void finish(iv::vk::Overlay& overlay);

    // True if no glyph has been appended since construction/finish().
    [[nodiscard]] bool empty() const noexcept;

private:
    FontSet* fonts_;
    // Per-face accumulated quads; glyphLoc holds the FACE-LOCAL atlas offset until finish()
    // rebases it. Indexed by static_cast<size_t>(Face).
    std::array<std::vector<iv::vk::GlyphVertex>, kFaceCount> perFace_{};
};

} // namespace iv::text

#endif // IV_TEXT_TEXT_LAYOUT_HPP
