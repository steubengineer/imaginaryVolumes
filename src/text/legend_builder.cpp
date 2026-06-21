#include "iv/text/legend_builder.hpp"

#include "iv/plot_axes.hpp"        // ticksFor, formatTick, kDefaultMajor (ADR-0024)
#include "iv/text/text_layout.hpp" // appendText
#include "iv/transfer.hpp"         // phaseColor, transferNormalized (ADR-0028)

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace iv::text {

namespace {

using iv::vk::Overlay;
using iv::vk::OverlayVertex;
using Color = std::array<float, 4>;

constexpr Color kBorderColor{0.85f, 0.86f, 0.92f, 1.0f};
constexpr Color kBackingColor{0.12f, 0.12f, 0.15f, 1.0f}; // opaque panel so the swatch alpha reads
constexpr Color kLabelColor{1.0f, 1.0f, 1.0f, 1.0f};
constexpr float kPi = 3.14159265358979323846f;
constexpr int kCols = 64;            // swatch phase resolution (color interpolates between)
constexpr int kRows = 48;            // swatch magnitude resolution (alpha interpolates between)
constexpr float kTickLenNdc = 0.018f;
constexpr float kPhaseLabelPadPx = 6.0f;
constexpr float kMagLabelPadPx = 8.0f;

void sv(std::vector<OverlayVertex>& out, float x, float y, const Color& c) {
    out.push_back(OverlayVertex{{x, y, 0.0f}, c});
}
void sline(Overlay& ov, float x0, float y0, float x1, float y1, const Color& c) {
    sv(ov.screenLines, x0, y0, c);
    sv(ov.screenLines, x1, y1, c);
}
// A screen-space quad (two triangles) with per-corner colors at (xL,yA),(xR,yA),(xR,yB),(xL,yB).
void squad(Overlay& ov, float xL, float yA, float xR, float yB, const Color& cLA, const Color& cRA,
           const Color& cRB, const Color& cLB) {
    sv(ov.screenTriangles, xL, yA, cLA);
    sv(ov.screenTriangles, xR, yA, cRA);
    sv(ov.screenTriangles, xR, yB, cRB);
    sv(ov.screenTriangles, xL, yA, cLA);
    sv(ov.screenTriangles, xR, yB, cRB);
    sv(ov.screenTriangles, xL, yB, cLB);
}

float textWidth(Shaper& sh, std::string_view s) {
    float w = 0.0f;
    for (const auto& g : sh.shape(s)) {
        w += g.xAdvance;
    }
    return w;
}
// Horizontally centered at pixel (cx), baseline at pixel (by), at the shaper's size.
void centeredLabel(Overlay& ov, Shaper& sh, std::string_view s, float cx, float by,
                   std::uint32_t fbW, std::uint32_t fbH) {
    iv::text::appendText(ov, sh, s, cx - 0.5f * textWidth(sh, s), by, fbW, fbH,
                         {kLabelColor, sh.pixelSize()});
}
// Left-aligned at pixel x, vertically centered on pixel (cy).
void leftLabel(Overlay& ov, Shaper& sh, std::string_view s, float x, float cy, std::uint32_t fbW,
               std::uint32_t fbH) {
    iv::text::appendText(ov, sh, s, x, cy + 0.34f * sh.pixelSize(), fbW, fbH,
                         {kLabelColor, sh.pixelSize()});
}

} // namespace

void buildLegend(Overlay& ov, const iv::LegendSpec& spec, std::uint32_t fbW, std::uint32_t fbH,
                 Shaper& sh) {
    if (!spec.show) {
        return;
    }
    const float xL = spec.rectNdc[0];
    const float yTop = spec.rectNdc[1];
    const float xR = spec.rectNdc[2];
    const float yBot = spec.rectNdc[3];

    const float halfW = static_cast<float>(fbW) * 0.5f;
    const float halfH = static_cast<float>(fbH) * 0.5f;
    const auto pxX = [&](float ndc) { return (ndc + 1.0f) * halfW; }; // NDC -> pixel (top-left)
    const auto pxY = [&](float ndc) { return (ndc + 1.0f) * halfH; };

    // (1) Opaque backing panel so the swatch's partial opacity reads against a known tone.
    squad(ov, xL, yTop, xR, yBot, kBackingColor, kBackingColor, kBackingColor, kBackingColor);

    // (2) The 2-D swatch. Width = phase (-pi .. +pi, color = phaseColor); height = normalized
    //     magnitude v in [0,1] (low at the bottom, max at the top), alpha = clamp(v * density)
    //     = transferOpacity at the magnitude whose position is v (ADR-0028). This gradient is
    //     mode-independent; the mode only sets where magnitude VALUES sit (the right ticks).
    for (int j = 0; j < kCols; ++j) {
        const float u0 = static_cast<float>(j) / static_cast<float>(kCols);
        const float u1 = static_cast<float>(j + 1) / static_cast<float>(kCols);
        const float x0 = xL + (xR - xL) * u0;
        const float x1 = xL + (xR - xL) * u1;
        const auto rgb0 = iv::phaseColor(-kPi + u0 * 2.0f * kPi, spec.colormapMode);
        const auto rgb1 = iv::phaseColor(-kPi + u1 * 2.0f * kPi, spec.colormapMode);
        for (int i = 0; i < kRows; ++i) {
            const float v0 = static_cast<float>(i) / static_cast<float>(kRows);
            const float v1 = static_cast<float>(i + 1) / static_cast<float>(kRows);
            const float y0 = yBot + (yTop - yBot) * v0; // y-down: v=0 -> bottom, v=1 -> top
            const float y1 = yBot + (yTop - yBot) * v1;
            const float a0 = std::clamp(v0 * spec.densityScale, 0.0f, 1.0f);
            const float a1 = std::clamp(v1 * spec.densityScale, 0.0f, 1.0f);
            squad(ov, x0, y0, x1, y1, {rgb0[0], rgb0[1], rgb0[2], a0}, {rgb1[0], rgb1[1], rgb1[2], a0},
                  {rgb1[0], rgb1[1], rgb1[2], a1}, {rgb0[0], rgb0[1], rgb0[2], a1});
        }
    }

    // (3) Border.
    sline(ov, xL, yTop, xR, yTop, kBorderColor);
    sline(ov, xR, yTop, xR, yBot, kBorderColor);
    sline(ov, xR, yBot, xL, yBot, kBorderColor);
    sline(ov, xL, yBot, xL, yTop, kBorderColor);

    // (4) Phase ticks + labels on the bottom edge: -pi (left), 0 (center), +pi (right).
    const float us[3] = {0.0f, 0.5f, 1.0f};
    const char* plbl[3] = {"-\xCF\x80", "0", "\xCF\x80"}; // "-pi", "0", "pi" (U+03C0)
    for (int k = 0; k < 3; ++k) {
        const float x = xL + (xR - xL) * us[k];
        sline(ov, x, yBot, x, yBot + kTickLenNdc, kBorderColor); // y-down: +len draws downward
        centeredLabel(ov, sh, plbl[k], pxX(x),
                      pxY(yBot + kTickLenNdc) + kPhaseLabelPadPx + sh.pixelSize(), fbW, fbH);
    }
    if (!spec.phaseLabel.empty()) {
        centeredLabel(ov, sh, spec.phaseLabel, pxX(0.5f * (xL + xR)),
                      pxY(yBot + kTickLenNdc) + kPhaseLabelPadPx + 2.4f * sh.pixelSize(), fbW, fbH);
    }

    // (5) Magnitude ticks + value labels on the right edge: nice numbers (ADR-0024 ticksFor)
    //     over the active domain, each placed at its normalized ramp height transferNormalized.
    float loBound = 0.0f;
    if (spec.opacityMode != 0u) {
        loBound = (spec.logDecades > 0.0f) ? spec.range.max * std::pow(10.0f, -spec.logDecades)
                                           : spec.range.minPositive;
    }
    const iv::AxisTicks mt = iv::ticksFor(static_cast<double>(loBound),
                                          static_cast<double>(spec.range.max), iv::kDefaultMajor, 1);
    for (const double mv : mt.major) {
        const float pos =
            iv::transferNormalized(static_cast<float>(mv), spec.range, spec.opacityMode,
                                   spec.logDecades);
        if (pos < -0.001f || pos > 1.001f) {
            continue;
        }
        const float y = yBot + (yTop - yBot) * pos;
        sline(ov, xR, y, xR + kTickLenNdc, y, kBorderColor);
        leftLabel(ov, sh, iv::formatTick(mv, mt.step), pxX(xR + kTickLenNdc) + kMagLabelPadPx, pxY(y),
                  fbW, fbH);
    }
    if (!spec.magnitudeLabel.empty()) {
        centeredLabel(ov, sh, spec.magnitudeLabel, pxX(0.5f * (xL + xR)),
                      pxY(yTop) - kPhaseLabelPadPx - 0.4f * sh.pixelSize(), fbW, fbH);
    }
}

} // namespace iv::text
