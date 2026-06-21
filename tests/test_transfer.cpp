// Host transfer-function evaluators (ADR-0028): the legend draws ONLY through these, so
// they must mirror the shader's arg->color (ADR-0014) and abs->opacity (ADR-0013/0027)
// exactly. Teeth: the colormap anchors tie to the committed LUT bytes / analytic HSV, and
// the opacity anchors pin every branch (linear / log-full / decade-window / degenerate).

#include "iv/transfer.hpp"

#include "catch_amalgamated.hpp"

#include <array>
#include <cmath>

using iv::MagnitudeRange;

namespace {

constexpr float kPi = 3.14159265358979323846f;

bool near3(const std::array<float, 3>& got, const std::array<float, 3>& want, float tol = 1e-4f) {
    return std::abs(got[0] - want[0]) <= tol && std::abs(got[1] - want[1]) <= tol &&
           std::abs(got[2] - want[2]) <= tol;
}

} // namespace

TEST_CASE("phaseColor cyclic seam (-pi == +pi)", "[transfer]") {
    // The seam is the defining cyclic property (ADR-0014). A non-wrapping normalization
    // would split it; this is the teeth for the repeat/wrap.
    REQUIRE(near3(iv::phaseColor(-kPi, 0), iv::phaseColor(kPi, 0)));
    REQUIRE(near3(iv::phaseColor(-kPi, 1), iv::phaseColor(kPi, 1)));
    // ... and full periodicity: theta and theta + 2pi agree.
    REQUIRE(near3(iv::phaseColor(0.3f, 0), iv::phaseColor(0.3f + 2.0f * kPi, 0), 2e-3f));
}

TEST_CASE("phaseColor mode 0 ties to the committed twilight LUT", "[transfer]") {
    // theta = 0 => t = 0.5 exactly => x = 127.5 => lerp(LUT[127], LUT[128], 0.5). The two
    // committed entries are (48,20,55) and (47,20,54), so the midpoint is (47.5,20,54.5)/255.
    // Teeth: a wrong texel offset / seam shift moves this off the committed bytes.
    const std::array<float, 3> want{47.5f / 255.0f, 20.0f / 255.0f, 54.5f / 255.0f};
    REQUIRE(near3(iv::phaseColor(0.0f, 0), want));
}

TEST_CASE("phaseColor mode 1 HSV anchors", "[transfer]") {
    // Analytic HSV (ADR-0014): theta=0 -> t=0.5 -> cyan; -pi/2 -> t=0.25; +pi/2 -> t=0.75.
    REQUIRE(near3(iv::phaseColor(0.0f, 1), {0.0f, 1.0f, 1.0f}));          // cyan
    REQUIRE(near3(iv::phaseColor(-0.5f * kPi, 1), {0.5f, 1.0f, 0.0f}));   // hue 90
    REQUIRE(near3(iv::phaseColor(0.5f * kPi, 1), {0.5f, 0.0f, 1.0f}));    // hue 270
}

TEST_CASE("transferNormalized linear branch", "[transfer]") {
    const MagnitudeRange r{0.01f, 1.0f};
    REQUIRE(iv::transferNormalized(0.0f, r, 0, 0.0f) == Catch::Approx(0.0f));
    REQUIRE(iv::transferNormalized(0.5f, r, 0, 0.0f) == Catch::Approx(0.5f));
    REQUIRE(iv::transferNormalized(1.0f, r, 0, 0.0f) == Catch::Approx(1.0f));
    REQUIRE(iv::transferNormalized(2.0f, r, 0, 0.0f) == Catch::Approx(1.0f)); // clamped
}

TEST_CASE("transferNormalized log full-range branch", "[transfer]") {
    const MagnitudeRange r{0.01f, 1.0f}; // 2 decades; m=0.1 is the geometric midpoint
    REQUIRE(iv::transferNormalized(1.0f, r, 1, 0.0f) == Catch::Approx(1.0f));
    REQUIRE(iv::transferNormalized(0.1f, r, 1, 0.0f) == Catch::Approx(0.5f).margin(1e-5));
    REQUIRE(iv::transferNormalized(0.01f, r, 1, 0.0f) == Catch::Approx(0.0f));
    REQUIRE(iv::transferNormalized(0.001f, r, 1, 0.0f) == Catch::Approx(0.0f)); // below floor
    // m=0 must not evaluate log(0): finite 0, no NaN.
    REQUIRE(std::isfinite(iv::transferNormalized(0.0f, r, 1, 0.0f)));
    REQUIRE(iv::transferNormalized(0.0f, r, 1, 0.0f) == Catch::Approx(0.0f));
}

TEST_CASE("transferNormalized decade window branch (ADR-0027)", "[transfer]") {
    const MagnitudeRange r{0.0001f, 1.0f}; // wide range; window only the top 2 decades
    REQUIRE(iv::transferNormalized(1.0f, r, 1, 2.0f) == Catch::Approx(1.0f));     // at max
    REQUIRE(iv::transferNormalized(0.1f, r, 1, 2.0f) == Catch::Approx(0.5f).margin(1e-5)); // 1 dec
    REQUIRE(iv::transferNormalized(0.01f, r, 1, 2.0f) == Catch::Approx(0.0f));    // 2 dec (floor)
    REQUIRE(iv::transferNormalized(0.001f, r, 1, 2.0f) == Catch::Approx(0.0f));   // clamped
}

TEST_CASE("transferNormalized degenerate range -> 0", "[transfer]") {
    const MagnitudeRange z{0.0f, 0.0f};
    REQUIRE(iv::transferNormalized(5.0f, z, 0, 0.0f) == Catch::Approx(0.0f));
    REQUIRE(iv::transferNormalized(5.0f, z, 1, 0.0f) == Catch::Approx(0.0f));
    REQUIRE(iv::transferNormalized(5.0f, z, 1, 2.0f) == Catch::Approx(0.0f));
}

TEST_CASE("transferOpacity applies densityScale then clamps", "[transfer]") {
    const MagnitudeRange r{0.01f, 1.0f};
    REQUIRE(iv::transferOpacity(0.5f, r, 0, 0.5f, 0.0f) == Catch::Approx(0.25f)); // 0.5*0.5
    REQUIRE(iv::transferOpacity(0.5f, r, 0, 2.0f, 0.0f) == Catch::Approx(1.0f));  // 0.5*2 clamped
    REQUIRE(iv::transferOpacity(0.5f, r, 0, 0.0f, 0.0f) == Catch::Approx(0.0f));  // density 0
}
