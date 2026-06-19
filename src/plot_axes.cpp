#include "iv/plot_axes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace iv {

namespace {

// A "nice" number near x: (1,2,5)*10^k. round=true picks the nearest nice value;
// round=false the smallest nice value >= x. (Heckbert, "Nice Numbers for Graph
// Labels".) Requires x > 0.
double niceNum(double x, bool round) {
    const double expo = std::floor(std::log10(x));
    const double f = x / std::pow(10.0, expo); // mantissa in [1, 10)
    double nf = 0.0;
    if (round) {
        nf = (f < 1.5) ? 1.0 : (f < 3.0) ? 2.0 : (f < 7.0) ? 5.0 : 10.0;
    } else {
        nf = (f <= 1.0) ? 1.0 : (f <= 2.0) ? 2.0 : (f <= 5.0) ? 5.0 : 10.0;
    }
    return nf * std::pow(10.0, expo);
}

} // namespace

double physical(const Axis& a, double w) noexcept { return a.min + w * (a.max - a.min); }

double world(const Axis& a, double v) noexcept {
    const double range = a.max - a.min;
    return range == 0.0 ? 0.0 : (v - a.min) / range;
}

std::array<double, 3> dataCenter(const PlotAxes& p) noexcept {
    return {0.5 * (p.x.min + p.x.max), 0.5 * (p.y.min + p.y.max), 0.5 * (p.z.min + p.z.max)};
}

const Axis& axisFor(const PlotAxes& p, Dim d) noexcept {
    switch (d) {
    case Dim::X:
        return p.x;
    case Dim::Y:
        return p.y;
    case Dim::Z:
        return p.z;
    }
    return p.x; // unreachable; all enumerators handled above
}

AxisTicks ticksFor(double min, double max, int targetMajor, int minorPerMajor) {
    AxisTicks out;
    const double lo = std::min(min, max);
    const double hi = std::max(min, max);
    const double span = hi - lo;
    if (!std::isfinite(lo) || !std::isfinite(hi) || span <= 0.0) {
        return out; // degenerate range -> no ticks
    }

    const int intervals = std::max(targetMajor, 2) - 1; // >= 1 interval
    const double range = niceNum(span, false);
    const double step = niceNum(range / static_cast<double>(intervals), true);
    out.step = step;

    const double tol = 1e-9 * span;
    // Major ticks at integer multiples of `step` within [lo, hi] (k*step avoids the
    // drift of repeated addition).
    const long k0 = static_cast<long>(std::ceil(lo / step - tol));
    const long k1 = static_cast<long>(std::floor(hi / step + tol));
    for (long k = k0; k <= k1; ++k) {
        out.major.push_back(static_cast<double>(k) * step);
    }

    // Minor ticks at multiples of step/minorPerMajor, dropping major-coincident ones.
    if (minorPerMajor > 1) {
        const double mstep = step / static_cast<double>(minorPerMajor);
        const long j0 = static_cast<long>(std::ceil(lo / mstep - tol));
        const long j1 = static_cast<long>(std::floor(hi / mstep + tol));
        for (long j = j0; j <= j1; ++j) {
            if (j % minorPerMajor == 0) {
                continue; // coincides with a major tick
            }
            out.minor.push_back(static_cast<double>(j) * mstep);
        }
    }
    return out;
}

AxisTicks ticksFor(const Axis& a) {
    return ticksFor(a.min, a.max, a.majorCount.value_or(kDefaultMajor),
                    a.minorCount.value_or(kDefaultMinorPerMajor));
}

std::string axisLabelText(const Axis& a) {
    if (a.unit.empty()) {
        return a.label;
    }
    return a.label + " (" + a.unit + ")";
}

std::string formatTick(double value, double step) {
    int decimals = 0;
    if (step > 0.0 && std::isfinite(step)) {
        decimals = std::max(0, static_cast<int>(-std::floor(std::log10(step))));
        decimals = std::min(decimals, 12);
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    std::string s(buf);
    // Normalize a negative zero ("-0", "-0.00") to a plain zero.
    bool allZero = true;
    for (const char c : s) {
        if (c != '-' && c != '0' && c != '.') {
            allZero = false;
            break;
        }
    }
    if (allZero && !s.empty() && s.front() == '-') {
        s.erase(s.begin());
    }
    return s;
}

} // namespace iv
