#ifndef IV_PLOT_AXES_HPP
#define IV_PLOT_AXES_HPP

// Declarative plot coordinate model + nice-number tick generation (ADR-0024). Pure
// host code — no Vulkan, no HarfBuzz — so it is unit-testable in isolation and lives
// in core `iv` (like orbit_camera). It maps the volume's [0,1]^3 grid (ADR-0012:
// field axis (x,y,z) -> world (X,Y,Z), +Y up) to caller-specified physical
// coordinates, in the caller's own data units, and describes which annotations to
// draw. The view-dependent placement/rendering is ADR-0026.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace iv {

// A world/data axis (ADR-0012 correspondence).
enum class Dim { X, Y, Z };

// Default tick targets when an axis does not override them.
inline constexpr int kDefaultMajor = 5;          // target number of major ticks
inline constexpr int kDefaultMinorPerMajor = 5;  // minor subdivisions per major interval

// One axis of the plot. The data coordinate is `min` at world 0 and `max` at world 1
// (a reversed axis with min > max is allowed). `unit` is optional; the rendered axis
// label is `label` or, with a unit, `label + " (" + unit + ")"` (axisLabelText()).
struct Axis {
    double min{0.0};
    double max{1.0};
    std::string label;
    std::string unit;
    std::optional<int> majorCount; // target # major ticks (default kDefaultMajor)
    std::optional<int> minorCount; // minor subdivisions per major (default kDefaultMinorPerMajor)
};

// Box tick appearance (ADR-0024). Outer (default): marks only on the box silhouette
// edges (ADR-0026 selects them per view); AllFaces: on every box edge.
enum class BoxTickStyle { Outer, AllFaces };

// A reference axis drawn through the volume, parallel to `direction`, passing through
// the data-coordinate point `through` (its `direction` component is ignored). E.g.
// {Dim::X, {0,0,0}} is the X axis through the data point (·,0,0). Carries tick marks
// but NO numeric labels (they would overlap the data; ADR-0024/0026).
struct ThroughAxis {
    Dim direction{Dim::X};
    std::array<double, 3> through{0.0, 0.0, 0.0}; // data coords
    bool line{true};
    bool ticks{true};
};

// The declarative plot annotation model (ADR-0024). Every drawn element is optional;
// an all-false, empty-throughAxes model draws nothing.
struct PlotAxes {
    Axis x{};
    Axis y{};
    Axis z{};
    std::string title;

    bool boundingBox{true};                      // the 12 cube edges
    bool boxTicks{true};                         // tick marks on the box
    BoxTickStyle boxTickStyle{BoxTickStyle::Outer};
    bool tickLabels{true};                       // numeric labels on major ticks
    bool axisLabels{true};                       // per-axis name/unit labels
    bool showTitle{true};

    std::vector<ThroughAxis> throughAxes{};      // optional reference axes (empty default)
};

// Generated ticks for one axis: data-coordinate tick values, sorted ascending and
// within the axis range; `step` is the major spacing (0 for a degenerate range).
struct AxisTicks {
    std::vector<double> major;
    std::vector<double> minor;
    double step{0.0};
};

// --- Coordinate mapping (ADR-0024) ---

// Physical (data) coordinate at world position w in [0,1]: min + w*(max-min).
[[nodiscard]] double physical(const Axis& a, double w) noexcept;
// World position in [0,1] for data value v: (v-min)/(max-min) (0 if the range is
// degenerate). Only values with world in [0,1] lie inside the box.
[[nodiscard]] double world(const Axis& a, double v) noexcept;
// The per-axis center in data coordinates ((min+max)/2 each) — e.g. for an axis
// through the volume center.
[[nodiscard]] std::array<double, 3> dataCenter(const PlotAxes& p) noexcept;
// The axis for a dimension.
[[nodiscard]] const Axis& axisFor(const PlotAxes& p, Dim d) noexcept;

// --- Tick generation & formatting (ADR-0024) ---

// "Nice number" major+minor ticks within [min,max] for ~targetMajor major ticks and
// `minorPerMajor` minor subdivisions per major interval (Heckbert {1,2,5}). Empty for
// a degenerate or non-finite range. Deterministic.
[[nodiscard]] AxisTicks ticksFor(double min, double max, int targetMajor, int minorPerMajor);
// As above, using the axis's range and its (optional) tick counts.
[[nodiscard]] AxisTicks ticksFor(const Axis& a);

// The rendered axis label: `label`, or `label + " (" + unit + ")"` when unit is set.
[[nodiscard]] std::string axisLabelText(const Axis& a);
// Format a tick value with decimal precision derived from the tick `step`
// (locale-independent; "." decimal). Major tick labels use this (value only).
[[nodiscard]] std::string formatTick(double value, double step);

} // namespace iv

#endif // IV_PLOT_AXES_HPP
