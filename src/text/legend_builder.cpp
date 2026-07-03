#include "iv/text/legend_builder.hpp"

#include "iv/plot_axes.hpp"          // ticksFor, formatTick, kDefaultMajor (ADR-0024)
#include "iv/text/math_layout.hpp"   // appendLabel(Rotated) / measureLabel (math labels, ADR-0033)
#include "iv/text/text_layout.hpp"   // MixedGlyphs
#include "iv/transfer.hpp"           // phaseColor, transferNormalized (ADR-0028)
#include "iv/vk/view_projection.hpp" // viewProjection / projectToPixel (ADR-0012/0026; ADR-0034)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace iv::text {

namespace {

using iv::vk::Overlay;
using iv::vk::OverlayVertex;
using Color = std::array<float, 4>;

constexpr Color kBorderColor{0.85f, 0.86f, 0.92f, 1.0f};
constexpr Color kLabelColor{1.0f, 1.0f, 1.0f, 1.0f};
constexpr float kPi = 3.14159265358979323846f;
constexpr float kHalfPi = 1.57079632679489661923f;
constexpr int kCols = 64;            // swatch phase resolution (color interpolates between)
constexpr int kRows = 48;            // swatch magnitude resolution (alpha interpolates between)
constexpr float kTickLenNdc = 0.018f;
constexpr float kPhaseLabelPadPx = 6.0f;
constexpr float kMagLabelPadPx = 8.0f;
constexpr float kMagCaptionPadPx = 10.0f; // gap between the rotated |f| caption and the swatch left
constexpr float kLegendLabelScale = 1.3f; // legend labels relative to the tick size — ~the main
                                          // plot's axis-label scale (annotations.cpp kAxisLabelScale)

// placeLegendRight tunables (px; converted to NDC via halfW so gaps are aspect-consistent, ADR-0034).
constexpr float kBoxGapPx = 18.0f;      // gap between the box's right extent and the caption
constexpr float kCapAllowPx = 30.0f;    // horizontal room for the rotated caption left of the swatch
constexpr float kTickReservePx = 10.0f; // tick stub + a hair of margin, added to the measured label
                                        // width for the value-label reserve (B-0016). The fixed
                                        // fallback reserve is placeLegendRight's default arg (46 px).
constexpr float kRightEdgeNdc = 0.98f;  // keep the whole legend left of this NDC-x

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

// Math-aware labels (ADR-0033): captions may carry inline `$…$` math (the field name is `$f$`).
// Horizontally centered at pixel (cx), baseline at pixel (by), at `size` px.
void centeredLabel(Overlay& ov, MixedGlyphs& g, std::string_view s, float cx, float by,
                   std::uint32_t fbW, std::uint32_t fbH, float size) {
    const float w = iv::text::math::measureLabel(g.fonts(), s, size);
    iv::text::math::appendLabel(g, ov, s, cx - 0.5f * w, by, fbW, fbH, size, kLabelColor);
}
// Left-aligned at pixel x, vertically centered on pixel (cy), at `size` px.
void leftLabel(Overlay& ov, MixedGlyphs& g, std::string_view s, float x, float cy,
               std::uint32_t fbW, std::uint32_t fbH, float size) {
    iv::text::math::appendLabel(g, ov, s, x, cy + 0.34f * size, fbW, fbH, size, kLabelColor);
}

// Whether a linear magnitude axis with maximum `axisMax` should use scientific (mantissa×10^exp)
// tick labels: only very small or very large ranges, so ordinary ranges keep plain decimals and we
// never render "1.2×10⁰". Threshold per B-0016 (maintainer 2026-07-03).
bool linearUsesSci(double axisMax) {
    const double a = std::abs(axisMax);
    return a > 0.0 && (a < 1e-2 || a >= 1e4);
}

// The magnitude-axis ticks buildLegend draws for `spec`: (value, label) in draw order. Linear mode
// -> nice numbers over [0, max] (ADR-0024 ticksFor), scientific for extreme ranges (B-0016); log
// mode -> decade ticks over the active window (ADR-0027). Shared by buildLegend (emission) and
// magnitudeValueReservePx (measuring the labels to reserve their horizontal room). decadeTickLabel /
// linearTickLabel are the public formatters declared in the header.
std::vector<std::pair<double, std::string>> magnitudeTicks(const iv::LegendSpec& spec) {
    std::vector<std::pair<double, std::string>> out;
    const double maxM = static_cast<double>(spec.range.max);
    if (spec.opacityMode == 0u) { // linear: nice numbers over [0, max]
        const iv::AxisTicks mt = iv::ticksFor(0.0, maxM, iv::kDefaultMajor, 1);
        for (const double mv : mt.major) {
            out.emplace_back(mv, linearTickLabel(mv, mt.step, maxM));
        }
    } else { // log: decade ticks over the active window [lo, max]
        const double lo = (spec.logDecades > 0.0f)
                              ? maxM * std::pow(10.0, -static_cast<double>(spec.logDecades))
                              : static_cast<double>(spec.range.minPositive);
        if (lo > 0.0 && maxM > lo) {
            const int e0 = static_cast<int>(std::ceil(std::log10(lo)));
            const int e1 = static_cast<int>(std::floor(std::log10(maxM)));
            const int de = std::max(1, (e1 - e0) / 10); // thin to <= ~11 ticks for huge ranges
            for (int e = e0; e <= e1; e += de) {
                out.emplace_back(std::pow(10.0, e), decadeTickLabel(e));
            }
        }
    }
    return out;
}

} // namespace

// --- B-0016 magnitude tick-label formatting (see legend_builder.hpp) -------------------------

std::string decadeTickLabel(int exponent) {
    return exponent == 0 ? std::string("1") : "$10^{" + std::to_string(exponent) + "}$";
}

std::string linearTickLabel(double value, double step, double axisMax) {
    if (!linearUsesSci(axisMax)) {
        return iv::formatTick(value, step); // ordinary range: plain decimal
    }
    if (value == 0.0) {
        return std::string("0"); // zero has no exponent
    }
    // Shared exponent across the whole axis (derived from axisMax, identical for every tick), so the
    // mantissas read against one power of ten (4×10⁻³, 2×10⁻³) rather than each tick floating its own.
    const int exp = static_cast<int>(std::floor(std::log10(std::abs(axisMax))));
    const double scale = std::pow(10.0, exp);
    const std::string mant = iv::formatTick(value / scale, step / scale);
    return "$" + mant + "\\times10^{" + std::to_string(exp) + "}$";
}

float magnitudeValueReservePx(const iv::LegendSpec& spec, MixedGlyphs& glyphs) {
    // Value labels draw at the base tick size (buildLegend: emitMagTick uses baseSize). Reserve the
    // widest actual label + the tick stub + the swatch->label gap, so placeLegendRight keeps the
    // whole label on screen — the sci-notation labels (B-0016) are wider than the old decimals, so a
    // fixed guess under-reserved and clipped the exponent.
    const float size = glyphs.fonts().pixelSize();
    float widest = 0.0f;
    for (const auto& [mv, label] : magnitudeTicks(spec)) {
        (void)mv;
        widest = std::max(widest, iv::text::math::measureLabel(glyphs.fonts(), label, size));
    }
    return widest + kMagLabelPadPx + kTickReservePx;
}

void buildLegend(Overlay& ov, MixedGlyphs& g, const iv::LegendSpec& spec, std::uint32_t fbW,
                 std::uint32_t fbH) {
    if (!spec.show) {
        return;
    }
    const float baseSize = g.fonts().pixelSize();
    const float labelSize = baseSize * kLegendLabelScale; // legend text ~= the plot's axis labels
    const float xL = spec.rectNdc[0];
    const float yTop = spec.rectNdc[1];
    const float xR = spec.rectNdc[2];
    const float yBot = spec.rectNdc[3];

    const float halfW = static_cast<float>(fbW) * 0.5f;
    const float halfH = static_cast<float>(fbH) * 0.5f;
    const auto pxX = [&](float ndc) { return (ndc + 1.0f) * halfW; }; // NDC -> pixel (top-left)
    const auto pxY = [&](float ndc) { return (ndc + 1.0f) * halfH; };

    // The 2-D swatch. Width = phase (-pi .. +pi, color = phaseColor); height = normalized
    // magnitude v in [0,1] (low at the bottom, max at the top). The per-sample alpha at v is
    // transferOpacity = clamp(v * density), then corrected for the volume's accumulation over the
    // reference thickness (ADR-0030: accumulatedOpacity), so the swatch matches what a slab of
    // that magnitude renders. There is NO opaque backing: the swatch composites straight over the
    // scene with its real alpha, so it truly reflects transparency and matches the plot's
    // saturation. The gradient is opacity-mode-independent; the mode only sets where magnitude
    // VALUES sit (the right ticks).
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
            const float a0 = iv::accumulatedOpacity(std::clamp(v0 * spec.densityScale, 0.0f, 1.0f),
                                                    spec.referenceThickness);
            const float a1 = iv::accumulatedOpacity(std::clamp(v1 * spec.densityScale, 0.0f, 1.0f),
                                                    spec.referenceThickness);
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
        centeredLabel(ov, g, plbl[k], pxX(x),
                      pxY(yBot + kTickLenNdc) + kPhaseLabelPadPx + labelSize, fbW, fbH, labelSize);
    }
    if (const std::string ph = spec.phaseCaption(); !ph.empty()) {
        centeredLabel(ov, g, ph, pxX(0.5f * (xL + xR)),
                      pxY(yBot + kTickLenNdc) + kPhaseLabelPadPx + 2.4f * labelSize, fbW, fbH,
                      labelSize);
    }

    // (5) Magnitude ticks + value labels on the right edge, each placed at its normalized ramp
    //     height transferNormalized. Linear mode: nice numbers over [0, max] (ADR-0024 ticksFor).
    //     Log mode: DECADE ticks (powers of 10) over the active window [lo, max] — they are
    //     evenly spaced on the log bar and TRACK the decade window, so changing it visibly
    //     relabels the axis (ADR-0027; resolves B-0011), unlike linear ticks which barely move
    //     for high-dynamic-range data.
    const auto emitMagTick = [&](double mv, const std::string& label) {
        const float pos = iv::transferNormalized(static_cast<float>(mv), spec.range,
                                                 spec.opacityMode, spec.logDecades);
        if (pos < -0.001f || pos > 1.001f) {
            return;
        }
        const float y = yBot + (yTop - yBot) * pos;
        sline(ov, xR, y, xR + kTickLenNdc, y, kBorderColor);
        // The magnitude value ticks read at the volume's TICK size (baseSize), not the enlarged
        // legend labelSize — matching the plot's axis tick values. The -pi/0/pi phase labels and the
        // captions/L stay at labelSize.
        leftLabel(ov, g, label, pxX(xR + kTickLenNdc) + kMagLabelPadPx, pxY(y), fbW, fbH, baseSize);
    };
    for (const auto& [mv, label] : magnitudeTicks(spec)) { // labels are inline-math sci form (B-0016)
        emitMagTick(mv, label);
    }
    // Magnitude caption: rotated a quarter-turn CCW (reads bottom-to-top), vertically centered just
    // left of the swatch — compact, the conventional colorbar-title place (ADR-0034). The pivot is
    // the rotated baseline origin: x just left of the swatch (the baseline; ink extends further
    // left), y at the swatch mid + half the caption width (so the upward advance centers it).
    if (const std::string mg = spec.magnitudeCaption(); !mg.empty()) {
        const float w = iv::text::math::measureLabel(g.fonts(), mg, labelSize);
        const float pivotX = pxX(xL) - kMagCaptionPadPx - 0.30f * labelSize;
        const float pivotY = pxY(0.5f * (yTop + yBot)) + 0.5f * w;
        iv::text::math::appendLabelRotated(g, ov, mg, pivotX, pivotY, fbW, fbH, labelSize, kLabelColor,
                                           -kHalfPi);
    }

    // (6) Reference-thickness label (ADR-0030): the swatch opacity is accumulatedOpacity(a, L),
    //     so displaying L (the thickness) keeps the legend analytically invertible to the
    //     per-sample alpha. Uses plain "L" because the bundled NCM-Book face lacks U+2113 (the
    //     script small l) — see test_legend.
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "L = %.2f", static_cast<double>(spec.referenceThickness));
        centeredLabel(ov, g, buf, pxX(0.5f * (xL + xR)),
                      pxY(yBot + kTickLenNdc) + kPhaseLabelPadPx + 3.8f * labelSize, fbW, fbH,
                      labelSize);
    }
}

void placeLegendRight(iv::LegendSpec& spec, const iv::vk::RenderParams& camera, std::uint32_t fbW,
                      std::uint32_t fbH, float valueReservePx) {
    if (fbW == 0u || fbH == 0u) {
        return;
    }
    const float aspect = static_cast<float>(fbW) / static_cast<float>(fbH);
    const auto M = iv::vk::viewProjection(camera, aspect);
    const float halfW = static_cast<float>(fbW) * 0.5f;

    // Rightmost NDC-x of the projected unit-cube corners that are in front of the camera.
    bool any = false;
    float boxRight = -2.0f;
    for (int i = 0; i < 8; ++i) {
        const std::array<float, 3> corner{static_cast<float>(i & 1), static_cast<float>((i >> 1) & 1),
                                          static_cast<float>((i >> 2) & 1)};
        const auto px = iv::vk::projectToPixel(M, corner, fbW, fbH);
        if (px[2] <= 0.0f) {
            continue; // behind the camera
        }
        boxRight = std::max(boxRight, px[0] / halfW - 1.0f); // pixel-x -> NDC-x (matches pxX)
        any = true;
    }
    if (!any) {
        return; // degenerate projection: keep the default rect
    }

    const float width = spec.rectNdc[2] - spec.rectNdc[0]; // preserve the swatch width
    const float defaultLeft = spec.rectNdc[0];
    const float gap = kBoxGapPx / halfW;       // px -> NDC (aspect-consistent)
    const float capAllow = kCapAllowPx / halfW;
    const float valAllow = valueReservePx / halfW; // measured room for the right-edge value labels

    // `boxClear` is the leftmost swatch position that clears the box (+ the rotated caption to its
    // left); `hi` is the rightmost that keeps the widest value label on screen (measured, B-0016).
    // When the labels fit at/right-of the default (hi >= default) keep the established behavior: sit
    // at the default, or push right to clear the box but never so far that a value label leaves the
    // screen (labels win over full box clearance — partial box overlap is accepted, as before). When
    // they do NOT fit at the default (hi < default: a narrow window or a wide sci label), slide left
    // to `hi` so the label stays on screen — but never left of `boxClear`, so the swatch (and its
    // caption) never moves INTO the box; if clearing the box and fitting the label conflict (the box
    // fills a portrait frame) clearing the box wins and the label clips at the right edge.
    const float boxClear = boxRight + gap + capAllow;
    const float hi = kRightEdgeNdc - valAllow - width;
    const float lo = defaultLeft;
    const float left = (hi >= lo) ? std::clamp(boxClear, lo, hi) : std::max(hi, boxClear);
    spec.rectNdc[0] = left;
    spec.rectNdc[2] = left + width;
}

} // namespace iv::text
