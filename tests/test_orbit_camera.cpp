#include "iv/orbit_camera.hpp"

#include "catch_amalgamated.hpp"

#include <array>
#include <cmath>

using iv::OrbitCamera;

namespace {

bool approxEq(float a, float b, float tol = 1e-5f) {
    return std::abs(a - b) <= tol;
}

float distanceToTarget(const OrbitCamera& c) {
    const auto e = c.eye();
    const auto t = c.target();
    const float dx = e[0] - t[0];
    const float dy = e[1] - t[1];
    const float dz = e[2] - t[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

constexpr float kPi = 3.14159265358979323846f;

} // namespace

// teeth: the eye() closed form (ADR-0018). yaw=0,pitch=0 -> +X of target;
// yaw=pi/2 -> +Z. Perturbing the formula (wrong axis / sign) diverges.
TEST_CASE("OrbitCamera eye() matches the closed form", "[camera]") {
    OrbitCamera c;
    c.setTarget({0.0f, 0.0f, 0.0f});
    c.setDistance(2.0f);
    c.setYaw(0.0f);
    c.setPitch(0.0f);

    auto e = c.eye();
    CHECK(approxEq(e[0], 2.0f));
    CHECK(approxEq(e[1], 0.0f));
    CHECK(approxEq(e[2], 0.0f));

    c.setYaw(kPi / 2.0f); // look from +Z
    e = c.eye();
    CHECK(approxEq(e[0], 0.0f));
    CHECK(approxEq(e[1], 0.0f));
    CHECK(approxEq(e[2], 2.0f));

    c.setYaw(0.0f);
    c.setPitch(kPi / 2.0f); // straight above (clamped just under, but ~+Y)
    e = c.eye();
    CHECK(e[1] > 1.9f); // mostly +Y
}

// teeth: pitch and distance clamps (ADR-0018). Removing a clamp lets these exceed
// their limits.
TEST_CASE("OrbitCamera clamps pitch and distance", "[camera]") {
    OrbitCamera c;

    c.setPitch(10.0f);
    CHECK(c.pitch() <= 1.5534f);
    CHECK(c.pitch() >= 1.5532f); // clamped at ~+89 deg
    c.setPitch(-10.0f);
    CHECK(c.pitch() <= -1.5532f);
    c.orbit(0.0f, 100.0f); // still clamped after orbit
    CHECK(c.pitch() <= 1.5534f);

    c.setDistance(1000.0f);
    CHECK(approxEq(c.distance(), 50.0f)); // maxDistance
    c.setDistance(0.0f);
    CHECK(approxEq(c.distance(), 0.2f)); // minDistance
    c.dolly(0.0f); // distance * 0 -> clamps back to min
    CHECK(approxEq(c.distance(), 0.2f));
    c.setDistance(4.0f);
    c.dolly(0.5f);
    CHECK(approxEq(c.distance(), 2.0f));
}

// teeth: orbit moves the eye but preserves the distance to the target (it's a
// rotation about the target).
TEST_CASE("OrbitCamera orbit preserves the target distance", "[camera]") {
    OrbitCamera c;
    const float d0 = distanceToTarget(c);
    const auto e0 = c.eye();
    c.orbit(0.5f, 0.2f);
    const auto e1 = c.eye();
    CHECK((!approxEq(e0[0], e1[0]) || !approxEq(e0[2], e1[2]))); // the eye moved
    CHECK(approxEq(distanceToTarget(c), d0, 1e-4f));             // distance preserved
    CHECK(approxEq(distanceToTarget(c), c.distance(), 1e-4f));
}
