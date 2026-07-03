// Host transfer-function evaluators (ADR-0028): the legend draws ONLY through these, so
// they must mirror the shader's arg->color (ADR-0014) and abs->opacity (ADR-0013/0027)
// exactly. Teeth: the colormap anchors tie to the committed LUT bytes / analytic HSV, and
// the opacity anchors pin every branch (linear / log-full / decade-window / degenerate).

#include "iv/transfer.hpp"

#include "catch_amalgamated.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

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
    // would split it; this is the teeth for the repeat/wrap. Every map is cyclic (ADR-0036).
    for (std::uint32_t mode = 0u; mode < 4u; ++mode) {
        REQUIRE(near3(iv::phaseColor(-kPi, mode), iv::phaseColor(kPi, mode)));
    }
    // ... and full periodicity: theta and theta + 2pi agree.
    REQUIRE(near3(iv::phaseColor(0.3f, 0), iv::phaseColor(0.3f + 2.0f * kPi, 0), 2e-3f));
}

TEST_CASE("phaseColor mode 3 grayscale anchors (ADR-0036)", "[transfer]") {
    // grayscale is black -> white -> black, cyclic (r==g==b everywhere). theta=0 (t=0.5) is the
    // white peak; the seam (theta=+-pi) is black. Teeth: a wrong layer / a colour map here fails.
    const auto mid = iv::phaseColor(0.0f, 3);
    CHECK(mid[0] > 0.95f);
    CHECK(std::abs(mid[0] - mid[1]) < 1e-3f); // truly grey (no hue)
    CHECK(std::abs(mid[1] - mid[2]) < 1e-3f);
    const auto seam = iv::phaseColor(kPi, 3);
    CHECK(seam[0] < 0.03f);
    CHECK(seam[1] < 0.03f);
    CHECK(seam[2] < 0.03f);
}

TEST_CASE("phaseColor mode 2 infinity ties to its committed source seam (ADR-0036)", "[transfer]") {
    // infinity's seam colour (CSV row 0 == row 255) is this purple; the map is sampled at texel
    // centres so the seam value is within a texel of the source. Teeth: a wrong layer/source moves
    // it far off (e.g. twilight's seam is a different purple; HSV is saturated).
    REQUIRE(near3(iv::phaseColor(-kPi, 2), {0.34562389f, 0.02586483f, 0.44469337f}, 0.02f));
}

TEST_CASE("phaseColor selector: the four modes differ at a fixed phase (ADR-0036)", "[transfer]") {
    // Each colormapMode routes to a distinct map; at a generic phase no two coincide. A broken
    // selector (e.g. all modes -> twilight) would collapse these.
    const float th = 0.6f;
    const std::array<std::array<float, 3>, 4> c{iv::phaseColor(th, 0), iv::phaseColor(th, 1),
                                                iv::phaseColor(th, 2), iv::phaseColor(th, 3)};
    for (std::size_t i = 0; i < c.size(); ++i) {
        for (std::size_t j = i + 1; j < c.size(); ++j) {
            CHECK_FALSE(near3(c[i], c[j], 0.02f)); // distinct maps -> distinct colours
        }
    }
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

TEST_CASE("accumulatedOpacity mirrors the ADR-0020 accumulation over thickness", "[transfer]") {
    // thickness <= 0 -> uncorrected per-sample alpha (the escape hatch / ADR-0028 legend).
    REQUIRE(iv::accumulatedOpacity(0.3f, 0.0f) == Catch::Approx(0.3f));
    REQUIRE(iv::accumulatedOpacity(0.3f, -1.0f) == Catch::Approx(0.3f));
    // Endpoints stay put; a small per-sample alpha accumulates to much more over thickness.
    REQUIRE(iv::accumulatedOpacity(1.0f, 0.1f) == Catch::Approx(1.0f));
    REQUIRE(iv::accumulatedOpacity(0.0f, 0.1f) == Catch::Approx(0.0f));
    // 1 - (1-0.01)^(256*0.1) = 1 - 0.99^25.6 = 0.2269 (vs 0.01 per-sample).
    REQUIRE(iv::accumulatedOpacity(0.01f, 0.1f) == Catch::Approx(0.2269f).margin(1e-3));
    // Monotone increasing in both thickness and per-sample alpha.
    REQUIRE(iv::accumulatedOpacity(0.01f, 0.05f) < iv::accumulatedOpacity(0.01f, 0.2f));
    REQUIRE(iv::accumulatedOpacity(0.01f, 0.1f) < iv::accumulatedOpacity(0.05f, 0.1f));
    // The accumulated opacity exceeds the per-sample alpha (the "thickness" boost).
    REQUIRE(iv::accumulatedOpacity(0.02f, 0.1f) > 0.02f);
}
