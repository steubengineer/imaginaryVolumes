#ifndef IV_ORBIT_CAMERA_HPP
#define IV_ORBIT_CAMERA_HPP

// Orbit/zoom camera (ADR-0018): pure host code — no Vulkan, no GLFW — so it is
// usable both by the windowed viewer and by headless callers (e.g. scripting
// turntable views). It produces an eye position around a target for the ADR-0012
// camera; a caller maps eye()/target()/up() onto RenderParams. Right-handed,
// +Y up (matches ADR-0012). To stay dependency-free it does not reference
// RenderParams directly.

#include <algorithm>
#include <array>
#include <cmath>

namespace iv {

class OrbitCamera {
public:
    using Vec3 = std::array<float, 3>;

    [[nodiscard]] Vec3 target() const noexcept { return target_; }
    [[nodiscard]] float distance() const noexcept { return distance_; }
    [[nodiscard]] float yaw() const noexcept { return yaw_; }
    [[nodiscard]] float pitch() const noexcept { return pitch_; }

    void setTarget(Vec3 t) noexcept { target_ = t; }
    void setYaw(float y) noexcept { yaw_ = y; }
    void setPitch(float p) noexcept { pitch_ = std::clamp(p, -kPitchLimit, kPitchLimit); }
    void setDistance(float d) noexcept { distance_ = std::clamp(d, minDistance_, maxDistance_); }

    // Add to yaw/pitch (radians); pitch is clamped to avoid the pole flip.
    void orbit(float dYaw, float dPitch) noexcept {
        yaw_ += dYaw;
        pitch_ = std::clamp(pitch_ + dPitch, -kPitchLimit, kPitchLimit);
    }
    // Multiply the orbit distance (e.g. 0.9 to move closer); clamped.
    void dolly(float factor) noexcept {
        distance_ = std::clamp(distance_ * factor, minDistance_, maxDistance_);
    }
    // Restore the default home view.
    void reset() noexcept { *this = OrbitCamera{}; }

    // Eye position: target + distance·(cos p cos y, sin p, cos p sin y) — RH, +Y up.
    [[nodiscard]] Vec3 eye() const noexcept {
        const float cp = std::cos(pitch_);
        return {target_[0] + distance_ * cp * std::cos(yaw_),
                target_[1] + distance_ * std::sin(pitch_),
                target_[2] + distance_ * cp * std::sin(yaw_)};
    }
    [[nodiscard]] Vec3 up() const noexcept { return {0.0f, 1.0f, 0.0f}; }

private:
    static constexpr float kPitchLimit = 1.55334f; // ~89° — avoids the gimbal flip

    Vec3 target_{0.5f, 0.5f, 0.5f}; // unit-cube centre (ADR-0012)
    float distance_{3.0f};
    float yaw_{0.7f};
    float pitch_{0.5f};
    float minDistance_{0.2f};
    float maxDistance_{50.0f};
};

} // namespace iv

#endif // IV_ORBIT_CAMERA_HPP
