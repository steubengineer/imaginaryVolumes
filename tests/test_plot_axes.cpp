#include "iv/plot_axes.hpp"

#include "catch_amalgamated.hpp"

#include <cmath>
#include <limits>
#include <vector>

using iv::Axis;
using iv::AxisTicks;
using iv::BoxTickStyle;
using iv::Dim;
using iv::PlotAxes;

namespace {

bool approxVec(const std::vector<double>& got, const std::vector<double>& want, double tol = 1e-9) {
    if (got.size() != want.size()) {
        return false;
    }
    for (std::size_t i = 0; i < got.size(); ++i) {
        if (std::abs(got[i] - want[i]) > tol) {
            return false;
        }
    }
    return true;
}

bool sortedStrictAscending(const std::vector<double>& v) {
    for (std::size_t i = 1; i < v.size(); ++i) {
        if (!(v[i] > v[i - 1])) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("PlotAxes defaults are a unit-range, fully-visible box", "[axes]") {
    PlotAxes p;
    CHECK(p.x.min == 0.0);
    CHECK(p.x.max == 1.0);
    CHECK(p.boundingBox);
    CHECK(p.boxTicks);
    CHECK(p.boxTickStyle == BoxTickStyle::Outer);
    CHECK(p.tickLabels);
    CHECK(p.axisLabels);
    CHECK(p.showTitle);
    CHECK(p.throughAxes.empty());
}

// Recorded reference (ADR-0024): nice-number major ticks for canonical ranges.
// teeth: a non-nice step (e.g. raw range/n, or dropping the {2,5} candidates) makes
// these diverge — [0,1] would become {0,1} (step 1) instead of fifths.
TEST_CASE("ticksFor produces nice major ticks", "[axes]") {
    SECTION("[0,1] target 5 -> fifths") {
        const AxisTicks t = iv::ticksFor(0.0, 1.0, 5, 5);
        CHECK(t.step == Catch::Approx(0.2));
        CHECK(approxVec(t.major, {0.0, 0.2, 0.4, 0.6, 0.8, 1.0}));
        CHECK(t.minor.size() == 20); // 4 minors per major interval, 5 intervals
        CHECK(sortedStrictAscending(t.minor));
    }
    SECTION("[0,100] target 5 -> twenties") {
        const AxisTicks t = iv::ticksFor(0.0, 100.0, 5, 5);
        CHECK(t.step == Catch::Approx(20.0));
        CHECK(approxVec(t.major, {0.0, 20.0, 40.0, 60.0, 80.0, 100.0}));
        CHECK(t.minor.size() == 20);
    }
    SECTION("minors never coincide with majors and stay in range") {
        const AxisTicks t = iv::ticksFor(0.0, 1.0, 5, 5);
        for (const double m : t.minor) {
            CHECK(m > 0.0);
            CHECK(m < 1.0);
            for (const double M : t.major) {
                CHECK(std::abs(m - M) > 1e-9);
            }
        }
    }
}

TEST_CASE("ticksFor handles ranges, counts, and degenerate input", "[axes]") {
    SECTION("major-only when minorPerMajor <= 1") {
        const AxisTicks t = iv::ticksFor(0.0, 1.0, 5, 1);
        CHECK(t.minor.empty());
        CHECK_FALSE(t.major.empty());
    }
    SECTION("reversed range generates the same data-value ticks") {
        const AxisTicks t = iv::ticksFor(1.0, 0.0, 5, 5); // min>max
        CHECK(approxVec(t.major, {0.0, 0.2, 0.4, 0.6, 0.8, 1.0}));
    }
    SECTION("degenerate / non-finite range -> empty") {
        CHECK(iv::ticksFor(5.0, 5.0, 5, 5).major.empty());
        CHECK(iv::ticksFor(0.0, std::nan(""), 5, 5).major.empty());
        CHECK(iv::ticksFor(0.0, std::numeric_limits<double>::infinity(), 5, 5).major.empty());
    }
    SECTION("axis convenience uses the axis range + optional counts") {
        Axis a{0.0, 10.0, "x", "mm", {}, {}};
        const AxisTicks t = iv::ticksFor(a);
        CHECK(t.step == Catch::Approx(2.0));
        CHECK(approxVec(t.major, {0.0, 2.0, 4.0, 6.0, 8.0, 10.0}));
    }
}

// teeth: wrong precision (e.g. a fixed %f, or decimals off by one) diverges.
TEST_CASE("formatTick precision tracks the step", "[axes]") {
    CHECK(iv::formatTick(0.2, 0.2) == "0.2");
    CHECK(iv::formatTick(1.0, 0.2) == "1.0");
    CHECK(iv::formatTick(20.0, 20.0) == "20");
    CHECK(iv::formatTick(0.05, 0.05) == "0.05");
    CHECK(iv::formatTick(-0.0, 0.2) == "0.0");     // negative zero normalized
    CHECK(iv::formatTick(-2.5, 0.5) == "-2.5");
}

TEST_CASE("coordinate mapping and labels", "[axes]") {
    SECTION("world<->physical round-trips") {
        const Axis a{10.0, 20.0, "", "", {}, {}};
        CHECK(iv::world(a, 15.0) == Catch::Approx(0.5));
        CHECK(iv::physical(a, 0.5) == Catch::Approx(15.0));
        CHECK(iv::physical(a, iv::world(a, 17.3)) == Catch::Approx(17.3));
    }
    SECTION("reversed axis maps inversely") {
        const Axis a{1.0, 0.0, "", "", {}, {}};
        CHECK(iv::world(a, 0.2) == Catch::Approx(0.8));
    }
    SECTION("degenerate axis maps to 0") {
        const Axis a{3.0, 3.0, "", "", {}, {}};
        CHECK(iv::world(a, 3.0) == 0.0);
    }
    SECTION("dataCenter and axisFor") {
        PlotAxes p;
        p.x = Axis{0.0, 10.0, "", "", {}, {}};
        p.y = Axis{-2.0, 2.0, "", "", {}, {}};
        p.z = Axis{100.0, 200.0, "", "", {}, {}};
        const auto c = iv::dataCenter(p);
        CHECK(c[0] == Catch::Approx(5.0));
        CHECK(c[1] == Catch::Approx(0.0));
        CHECK(c[2] == Catch::Approx(150.0));
        CHECK(iv::axisFor(p, Dim::Z).max == 200.0);
    }
    SECTION("axis label text appends the unit") {
        CHECK(iv::axisLabelText(Axis{0, 1, "x", "", {}, {}}) == "x");
        CHECK(iv::axisLabelText(Axis{0, 1, "x", "mm", {}, {}}) == "x (mm)");
    }
}
