#include "iv/text/annotations.hpp"

#include "iv/text/math_layout.hpp" // appendLabel / measureLabel (math-aware labels, ADR-0033)
#include "iv/text/text_layout.hpp"
#include "iv/vk/view_projection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

namespace iv::text {

namespace {

using iv::Axis;
using iv::AxisTicks;
using iv::Dim;
using iv::PlotAxes;
using iv::ThroughAxis;
using iv::vk::Overlay;
using iv::vk::OverlayVertex;
using Vec3 = std::array<float, 3>;
using Color = std::array<float, 4>;

// Annotation appearance (ADR-0026). Lengths are fractions of the unit cube; margins
// are pixels.
constexpr Color kLineColor{0.85f, 0.86f, 0.92f, 1.0f};
constexpr Color kLabelColor{1.0f, 1.0f, 1.0f, 1.0f};
constexpr float kTickMajorLen = 0.045f;
constexpr float kTickMinorLen = 0.024f;
constexpr float kTickLabelMargin = 30.0f; // px outward from the tick (clears the tick mark)
constexpr float kAxisLabelGap = 12.0f;    // px gap between the tick-label band and the axis label
constexpr float kAxisLabelScale = 1.3f;   // axis labels, relative to tick labels
constexpr float kTitleScale = 1.5f;       // plot title, relative to tick labels
constexpr float kTitleGapPx = 10.0f;      // gap between the title's underside and the box top
constexpr float kLabelCapHalf = 0.35f;    // half the cap height as a fraction of pixel size:
                                          // both the cap-centering nudge and a label's box height

Vec3 add(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3 scale(const Vec3& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }

std::size_t dimIndex(Dim d) {
    switch (d) {
    case Dim::X:
        return 0;
    case Dim::Y:
        return 1;
    case Dim::Z:
        return 2;
    }
    return 0;
}

Vec3 unitVec(Dim d) {
    Vec3 v{0.0f, 0.0f, 0.0f};
    v[dimIndex(d)] = 1.0f;
    return v;
}

std::array<Dim, 2> otherDims(Dim d) {
    switch (d) {
    case Dim::X:
        return {Dim::Y, Dim::Z};
    case Dim::Y:
        return {Dim::X, Dim::Z};
    case Dim::Z:
        return {Dim::X, Dim::Y};
    }
    return {Dim::Y, Dim::Z};
}

// World point on axis `a` at world coord wA, with the other two dims at world wB, wC
// (in otherDims(a) order).
Vec3 worldPoint(Dim a, float wA, float wB, float wC) {
    const auto od = otherDims(a);
    Vec3 p{0.0f, 0.0f, 0.0f};
    p[dimIndex(a)] = wA;
    p[dimIndex(od[0])] = wB;
    p[dimIndex(od[1])] = wC;
    return p;
}

Vec3 cubeCorner(int i) {
    return {static_cast<float>(i & 1), static_cast<float>((i >> 1) & 1),
            static_cast<float>((i >> 2) & 1)};
}

// Is the box face perpendicular to `fd` at coord `fc` (0 or 1) front-facing the eye?
bool frontFacing(Dim fd, int fc, const Vec3& eye) {
    Vec3 n = unitVec(fd);
    if (fc == 0) {
        n = scale(n, -1.0f); // outward normal
    }
    Vec3 center{0.5f, 0.5f, 0.5f};
    center[dimIndex(fd)] = static_cast<float>(fc);
    const Vec3 d{eye[0] - center[0], eye[1] - center[1], eye[2] - center[2]};
    return (n[0] * d[0] + n[1] * d[1] + n[2] * d[2]) > 0.0f;
}

// A box edge parallel to `a` at off-axis coords (b, c) is on the silhouette iff its
// two adjacent faces differ in facing (ADR-0026).
bool silhouetteEdge(Dim a, int b, int c, const Vec3& eye) {
    const auto od = otherDims(a);
    return frontFacing(od[0], b, eye) != frontFacing(od[1], c, eye);
}

void addLine(Overlay& ov, const Vec3& a, const Vec3& b, const Color& color) {
    ov.lines.push_back(OverlayVertex{{a[0], a[1], a[2]}, color});
    ov.lines.push_back(OverlayVertex{{b[0], b[1], b[2]}, color});
}

// Place `s` centered (horizontally) at pixel (cx, cy), vertically centered on cy, at
// `pixelSize` px. `s` is a math-aware label (inline `$…$` math, ADR-0033), appended to the
// shared mixed-font builder `g`.
void addCenteredLabel(Overlay& ov, MixedGlyphs& g, std::string_view s, float cx, float cy,
                      std::uint32_t fbW, std::uint32_t fbH, float pixelSize) {
    const float w = iv::text::math::measureLabel(g.fonts(), s, pixelSize);
    const float penX = cx - 0.5f * w;
    const float penY = cy + kLabelCapHalf * pixelSize; // approx vertical centering of caps
    iv::text::math::appendLabel(g, ov, s, penX, penY, fbW, fbH, pixelSize, kLabelColor);
}

} // namespace

float axisLabelOutwardPx(float nx, float ny, float tickMargin, float tickHalfW, float tickHalfH,
                         float axisHalfW, float axisHalfH, float gap) {
    // Outward offset (px), along the unit screen normal n=(nx,ny), from the box-edge midpoint
    // to the axis-label CENTER, so the axis label clears the tick-label band rather than
    // colliding with it (B-0013: "y (nm)" over "0.0"). Each label is an axis-aligned box; its
    // reach along n is the box support, |nx|·halfW + |ny|·halfH — so a vertical edge (n≈±x)
    // clears the tick *width*, a horizontal edge (n≈±y) the tick *height*. The axis label sits
    // one `gap` beyond the tick band's outer edge (tickMargin + tick reach), plus its own reach.
    // With no tick labels (tickHalf*≈0) it reduces to clearing the tick marks (tickMargin).
    const float tickReach = std::abs(nx) * tickHalfW + std::abs(ny) * tickHalfH;
    const float axisReach = std::abs(nx) * axisHalfW + std::abs(ny) * axisHalfH;
    return tickMargin + tickReach + gap + axisReach;
}

void buildAnnotations(Overlay& ov, MixedGlyphs& g, const PlotAxes& axes,
                      const iv::vk::RenderParams& camera, std::uint32_t fbW, std::uint32_t fbH) {
    ov.clear(); // reset ALL channels incl. the screen-space legend ones, so a reused overlay
                // (the viewer rebuilds one every frame) does not accumulate (ADR-0026/0028).
    const float baseSize = g.fonts().pixelSize();

    const float aspect = fbH > 0u ? static_cast<float>(fbW) / static_cast<float>(fbH) : 1.0f;
    ov.transform = iv::vk::viewProjection(camera, aspect);
    const auto& M = ov.transform;
    const Vec3 eye = camera.eye;

    // --- Bounding box: the 12 unit-cube edges ---
    if (axes.boundingBox) {
        for (int i = 0; i < 8; ++i) {
            for (int k = 0; k < 3; ++k) {
                const int j = i | (1 << k);
                if (j > i) { // each undirected edge once
                    addLine(ov, cubeCorner(i), cubeCorner(j), kLineColor);
                }
            }
        }
    }

    const std::array<Dim, 3> dims{Dim::X, Dim::Y, Dim::Z};
    const auto centerPx = iv::vk::projectToPixel(M, {0.5f, 0.5f, 0.5f}, fbW, fbH);

    for (const Dim a : dims) {
        const Axis& ax = iv::axisFor(axes, a);
        const AxisTicks ticks = iv::ticksFor(ax);
        if (ticks.major.empty()) {
            continue;
        }
        const auto od = otherDims(a);

        // --- Box ticks (outer silhouette edges, or all faces) ---
        if (axes.boxTicks) {
            for (int b = 0; b < 2; ++b) {
                for (int c = 0; c < 2; ++c) {
                    const bool draw = axes.boxTickStyle == iv::BoxTickStyle::AllFaces ||
                                      silhouetteEdge(a, b, c, eye);
                    if (!draw) {
                        continue;
                    }
                    const Vec3 outB = scale(unitVec(od[0]), b == 0 ? -1.0f : 1.0f);
                    const auto emitTicks = [&](const std::vector<double>& vals, float len) {
                        for (const double v : vals) {
                            const float wA = static_cast<float>(iv::world(ax, v));
                            if (wA < 0.0f || wA > 1.0f) {
                                continue;
                            }
                            const Vec3 p =
                                worldPoint(a, wA, static_cast<float>(b), static_cast<float>(c));
                            addLine(ov, p, add(p, scale(outB, len)), kLineColor);
                        }
                    };
                    emitTicks(ticks.major, kTickMajorLen);
                    emitTicks(ticks.minor, kTickMinorLen);
                }
            }
        }

        // --- Label edge: the silhouette edge whose outward screen normal points most
        //     down-and-out (so labels sit below/outside the projected box) ---
        if (!axes.tickLabels && !axes.axisLabels) {
            continue;
        }
        bool haveEdge = false;
        int bestB = 0;
        int bestC = 0;
        float bestScore = -1e30f;
        std::array<float, 2> bestOut{0.0f, 0.0f};
        for (int b = 0; b < 2; ++b) {
            for (int c = 0; c < 2; ++c) {
                if (!silhouetteEdge(a, b, c, eye)) {
                    continue;
                }
                const Vec3 mid =
                    worldPoint(a, 0.5f, static_cast<float>(b), static_cast<float>(c));
                const auto midPx = iv::vk::projectToPixel(M, mid, fbW, fbH);
                if (midPx[2] <= 0.0f) {
                    continue; // behind the camera
                }
                float ox = midPx[0] - centerPx[0];
                float oy = midPx[1] - centerPx[1];
                const float len = std::sqrt(ox * ox + oy * oy);
                if (len <= 1e-4f) {
                    continue;
                }
                ox /= len;
                oy /= len;
                const float score = oy; // prefer pointing down (screen +y)
                if (score > bestScore) {
                    bestScore = score;
                    bestB = b;
                    bestC = c;
                    bestOut = {ox, oy};
                    haveEdge = true;
                }
            }
        }
        if (!haveEdge) {
            continue;
        }

        float maxTickWidth = 0.0f; // widest drawn tick label (px) — sizes the axis-label offset
        if (axes.tickLabels) {
            for (const double v : ticks.major) {
                const float wA = static_cast<float>(iv::world(ax, v));
                if (wA < 0.0f || wA > 1.0f) {
                    continue;
                }
                const Vec3 p =
                    worldPoint(a, wA, static_cast<float>(bestB), static_cast<float>(bestC));
                const auto px = iv::vk::projectToPixel(M, p, fbW, fbH);
                if (px[2] <= 0.0f) {
                    continue;
                }
                const std::string t = iv::formatTick(v, ticks.step);
                maxTickWidth =
                    std::max(maxTickWidth, iv::text::math::measureLabel(g.fonts(), t, baseSize));
                addCenteredLabel(ov, g, t, px[0] + bestOut[0] * kTickLabelMargin,
                                 px[1] + bestOut[1] * kTickLabelMargin, fbW, fbH, baseSize);
            }
        }
        if (axes.axisLabels) {
            const std::string lbl = iv::axisLabelText(ax);
            if (!lbl.empty()) {
                const Vec3 mid =
                    worldPoint(a, 0.5f, static_cast<float>(bestB), static_cast<float>(bestC));
                const auto px = iv::vk::projectToPixel(M, mid, fbW, fbH);
                if (px[2] > 0.0f) {
                    const float axisSize = baseSize * kAxisLabelScale;
                    const float axisW = iv::text::math::measureLabel(g.fonts(), lbl, axisSize);
                    // Clear the tick-label band, adapting to the edge's screen orientation, so
                    // wide tick numbers no longer collide with the axis label (B-0013).
                    const float margin = axisLabelOutwardPx(
                        bestOut[0], bestOut[1], kTickLabelMargin, 0.5f * maxTickWidth,
                        kLabelCapHalf * baseSize, 0.5f * axisW, kLabelCapHalf * axisSize,
                        kAxisLabelGap);
                    addCenteredLabel(ov, g, lbl, px[0] + bestOut[0] * margin,
                                     px[1] + bestOut[1] * margin, fbW, fbH, axisSize);
                }
            }
        }
    }

    // --- Through-volume reference axes (data-unit locations; no labels) ---
    for (const ThroughAxis& ta : axes.throughAxes) {
        const Dim a = ta.direction;
        const auto od = otherDims(a);
        const float wB = static_cast<float>(
            iv::world(iv::axisFor(axes, od[0]), ta.through[dimIndex(od[0])]));
        const float wC = static_cast<float>(
            iv::world(iv::axisFor(axes, od[1]), ta.through[dimIndex(od[1])]));
        if (ta.line) {
            addLine(ov, worldPoint(a, 0.0f, wB, wC), worldPoint(a, 1.0f, wB, wC), kLineColor);
        }
        if (ta.ticks) {
            const Axis& ax = iv::axisFor(axes, a);
            const AxisTicks ticks = iv::ticksFor(ax);
            const Vec3 perp = unitVec(od[0]);
            const auto emitCross = [&](const std::vector<double>& vals, float len) {
                for (const double v : vals) {
                    const float wA = static_cast<float>(iv::world(ax, v));
                    if (wA < 0.0f || wA > 1.0f) {
                        continue;
                    }
                    const Vec3 p = worldPoint(a, wA, wB, wC);
                    addLine(ov, add(p, scale(perp, -0.5f * len)), add(p, scale(perp, 0.5f * len)),
                            kLineColor);
                }
            };
            emitCross(ticks.major, kTickMajorLen);
            emitCross(ticks.minor, kTickMinorLen);
        }
    }

    // --- Title: centered over the PLOT, tucked just above the box top. The title is not projected,
    //     so it carries the ADR-0035 image shift itself to stay centered over the (possibly
    //     left-shifted) plot; its vertical position tracks the box's projected top so it sits close
    //     to the bounding box rather than floating in the empty space below the frame top. ---
    if (axes.showTitle && !axes.title.empty()) {
        const float titleSize = baseSize * kTitleScale;
        const float titleCx = static_cast<float>(fbW) * 0.5f * (1.0f + camera.imageShiftNdcX);
        float boxTopPx = static_cast<float>(fbH);
        for (int i = 0; i < 8; ++i) {
            const auto cp = iv::vk::projectToPixel(M, cubeCorner(i), fbW, fbH);
            if (cp[2] > 0.0f) {
                boxTopPx = std::min(boxTopPx, cp[1]);
            }
        }
        // Center the title half its height + a gap above the box top; never above the frame margin.
        const float titleCy = std::max(boxTopPx - 0.5f * titleSize - kTitleGapPx, titleSize);
        addCenteredLabel(ov, g, axes.title, titleCx, titleCy, fbW, fbH, titleSize);
    }
}

} // namespace iv::text
