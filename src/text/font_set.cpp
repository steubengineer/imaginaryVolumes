#include "iv/text/font_set.hpp"

#include "iv/text/bundled_font.hpp"

#include <array>
#include <utility>

namespace iv::text {

Result<FontSet> FontSet::create(float pixelSizePx) {
    // Shaper::create validates pixelSizePx > 0 and the font bytes; forward its error with
    // the failing face named so a bad bundle is diagnosable.
    auto roman = Shaper::create(bundledFont(), pixelSizePx);
    if (!roman) {
        return std::unexpected(std::move(roman).error());
    }
    auto italic = Shaper::create(bundledFontItalic(), pixelSizePx);
    if (!italic) {
        return std::unexpected(std::move(italic).error());
    }
    auto math = Shaper::create(bundledFontMath(), pixelSizePx);
    if (!math) {
        return std::unexpected(std::move(math).error());
    }

    // Face order = enum order (Roman, Italic, Math); aggregate-init moves each Shaper in
    // (Shaper is move-only with no default ctor, so no element is default-constructed).
    std::array<Shaper, kFaceCount> shapers{
        {std::move(*roman), std::move(*italic), std::move(*math)}};
    return FontSet(std::move(shapers));
}

} // namespace iv::text
