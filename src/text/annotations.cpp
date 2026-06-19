#include "iv/text/annotations.hpp"

#include "iv/text/text_layout.hpp"
#include "iv/vk/view_projection.hpp"

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
constexpr float kTickLabelMargin = 12.0f;
constexpr float kAxisLabelMargin = 40.0f;

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

float textWidth(Shaper& sh, std::string_view s) {
    float w = 0.0f;
    for (const auto& g : sh.shape(s)) {
        w += g.xAdvance;
    }
    return w;
}

// Place `s` centered (horizontally) at pixel (cx, cy), vertically centered on cy.
void addCenteredLabel(Overlay& ov, Shaper& sh, std::string_view s, float cx, float cy,
                      std::uint32_t fbW, std::uint32_t fbH) {
    const float penX = cx - 0.5f * textWidth(sh, s);
    const float penY = cy + 0.35f * sh.pixelSize(); // approx vertical centering of caps
    iv::text::appendText(ov, sh, s, penX, penY, fbW, fbH, {kLabelColor});
}

} // namespace

void buildAnnotations(Overlay& ov, const PlotAxes& axes, const iv::vk::RenderParams& camera,
                      std::uint32_t fbW, std::uint32_t fbH, Shaper& sh) {
    ov.lines.clear();
    ov.triangles.clear();
    ov.glyphs.clear();
    ov.glyphAtlas.clear();

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
                addCenteredLabel(ov, sh, iv::formatTick(v, ticks.step),
                                 px[0] + bestOut[0] * kTickLabelMargin,
                                 px[1] + bestOut[1] * kTickLabelMargin, fbW, fbH);
            }
        }
        if (axes.axisLabels) {
            const std::string lbl = iv::axisLabelText(ax);
            if (!lbl.empty()) {
                const Vec3 mid =
                    worldPoint(a, 0.5f, static_cast<float>(bestB), static_cast<float>(bestC));
                const auto px = iv::vk::projectToPixel(M, mid, fbW, fbH);
                if (px[2] > 0.0f) {
                    addCenteredLabel(ov, sh, lbl, px[0] + bestOut[0] * kAxisLabelMargin,
                                     px[1] + bestOut[1] * kAxisLabelMargin, fbW, fbH);
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

    // --- Title: screen top-center ---
    if (axes.showTitle && !axes.title.empty()) {
        addCenteredLabel(ov, sh, axes.title, static_cast<float>(fbW) * 0.5f, sh.pixelSize() * 1.3f,
                         fbW, fbH);
    }
}

} // namespace iv::text
