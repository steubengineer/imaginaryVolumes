#include "iv/vk/view_projection.hpp"

#include <cmath>

namespace iv::vk {

namespace {

using Vec3 = std::array<float, 3>;

Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec3 normalize(const Vec3& a) {
    const float n = std::sqrt(dot(a, a));
    const float inv = n > 0.0f ? 1.0f / n : 0.0f;
    return {a[0] * inv, a[1] * inv, a[2] * inv};
}

} // namespace

std::array<float, 16> viewProjection(const RenderParams& camera, float aspect) {
    // ADR-0012 camera basis: w points from target to eye (backward); u right; v up.
    const Vec3 eye = camera.eye;
    const Vec3 w = normalize(sub(eye, camera.target));
    const Vec3 u = normalize(cross(camera.up, w));
    const Vec3 v = cross(w, u);

    const float halfH = std::tan(camera.vfovRadians * 0.5f);
    const float halfW = aspect * halfH;
    const float invHW = halfW != 0.0f ? 1.0f / halfW : 0.0f;
    const float invHH = halfH != 0.0f ? 1.0f / halfH : 0.0f;
    const float eu = dot(eye, u);
    const float ev = dot(eye, v);
    const float ew = dot(eye, w);

    // Derived from the ADR-0012 ray (origin=eye, dir = (2s-1)halfW*u + (1-2t)halfH*v
    // - w): for d = P - eye = a*u + b*v + c*w, the pixel is ndc_x = -a/(c*halfW),
    // ndc_y = b/(c*halfH) (y-down), with perspective w = -c. So, with clip = M*(P,1):
    //   clip.x =  dot(d,u)/halfW,  clip.y = -dot(d,v)/halfH,  clip.w = -dot(d,w),
    //   clip.z = 0.5*clip.w  (mid-range; the overlay pass has no depth buffer).
    // Row r (acting on (Px,Py,Pz,1)); stored column-major below.
    const std::array<float, 4> row3{-w[0], -w[1], -w[2], ew};
    // ADR-0035 horizontal image shift: clip.x' = clip.x + shift*clip.w => ndc_x' = ndc_x + shift.
    // Add shift*row3 (the clip.w row) to row0 (the clip.x row) — matches the fillUbo ray shift.
    const float sh = camera.imageShiftNdcX;
    const std::array<float, 4> row0{u[0] * invHW + sh * row3[0], u[1] * invHW + sh * row3[1],
                                    u[2] * invHW + sh * row3[2], -eu * invHW + sh * row3[3]};
    const std::array<float, 4> row1{-v[0] * invHH, -v[1] * invHH, -v[2] * invHH, ev * invHH};
    const std::array<float, 4> row2{0.5f * row3[0], 0.5f * row3[1], 0.5f * row3[2], 0.5f * row3[3]};

    // Column-major: out[col*4 + row] = M[row][col].
    return {row0[0], row1[0], row2[0], row3[0], row0[1], row1[1], row2[1], row3[1],
            row0[2], row1[2], row2[2], row3[2], row0[3], row1[3], row2[3], row3[3]};
}

std::array<float, 3> projectToPixel(const std::array<float, 16>& m, const Vec3& p,
                                    std::uint32_t width, std::uint32_t height) {
    // clip = m * (p, 1); m is column-major so clip[r] = sum_c m[c*4+r] * P[c].
    const std::array<float, 4> hp{p[0], p[1], p[2], 1.0f};
    std::array<float, 4> clip{0.0f, 0.0f, 0.0f, 0.0f};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            clip[static_cast<std::size_t>(r)] +=
                m[static_cast<std::size_t>(c * 4 + r)] * hp[static_cast<std::size_t>(c)];
        }
    }
    const float wclip = clip[3];
    const float invW = wclip != 0.0f ? 1.0f / wclip : 0.0f;
    const float ndcX = clip[0] * invW;
    const float ndcY = clip[1] * invW;
    // NDC [-1,1] -> pixel (top-left origin; ndcY is already y-down).
    const float px = (ndcX * 0.5f + 0.5f) * static_cast<float>(width);
    const float py = (ndcY * 0.5f + 0.5f) * static_cast<float>(height);
    return {px, py, wclip};
}

} // namespace iv::vk
