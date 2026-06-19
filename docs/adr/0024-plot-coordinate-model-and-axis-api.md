# ADR-0024: Plot Coordinate Model, Declarative Axis/Label API & Tick Generation

- **Status:** Accepted
- **Date:** 2026-06-19
- **Supersedes:** none

## Context
M7 turns the render into a *spatially labeled plot*. Callers must be able to map the
volume's `[0,1]³` grid (ADR-0012: field axis `(x,y,z)` → world `(X,Y,Z)`, +Y up) to
**physical coordinates** and to **label** the plot, **declaratively** (maintainer
decision: a declarative axis model, not caller-supplied tick lists). Requirements
fixed with the maintainer:
- The library **auto-generates "nice" major + minor tick marks**, **labels only the
  major ticks**, and lets a caller optionally **set the tick counts per axis**.
- **Every drawn element is optional** (bounding box, box ticks, tick labels, axis
  labels, title) — each independently toggleable.
- Axes/ticks may be drawn **on the bounding box** and/or as **reference axes through
  the volume** at caller-chosen locations. **Through-volume axis locations are given
  in the caller's data units**, never world `[0,1]`.
- Box tick marks default to an **outer-only** appearance (drawn on the box silhouette,
  not all faces) for clarity; an **all-faces** style remains available.

This ADR fixes the **model + public API + tick-generation algorithm** only; it is
**pure host code** (no Vulkan, no HarfBuzz), so it is unit-testable in isolation. The
view-dependent *rendering/placement* (which silhouette edge carries labels, the
projection, the glyphs) is ADR-0026; glyph rendering is ADR-0023/0025. Cites ADR-0012
(the `[0,1]³` frame and axis convention), ADR-0003 (no exceptions; values over
errors), the M7 milestone. LaTeX in labels is still deferred; label strings are plain
Unicode.

## Decision
**A declarative `iv::PlotAxes` model (ranges + labels + optional elements + axis
placement) plus a pure nice-number tick generator.**

- **Dimension** `enum class iv::Dim { X, Y, Z }` — selects a world axis (ADR-0012:
  field/world axis correspondence).
- **Per-axis model** `iv::Axis`:
  - `double min{0.0}, max{1.0}` — the physical coordinate at world `0` and world `1`
    on that axis (world `w ∈ [0,1]` ↦ physical `p = min + w·(max−min)`; default
    `[0,1]`, an unconfigured unit axis).
  - `std::string label` — the axis name (e.g. `"x"`); `std::string unit` — optional.
    The **rendered axis label** is `label` when `unit` is empty, else
    `label + " (" + unit + ")"`.
  - `std::optional<int> majorCount, minorCount` — caller overrides for the **target**
    number of major ticks and minor subdivisions per major interval; unset ⇒ defaults
    (`kDefaultMajor = 5`, `kDefaultMinorPerMajor = 5`).
- **Box tick style** `enum class iv::BoxTickStyle { Outer, AllFaces }` — `Outer`
  (default): box tick marks on the silhouette/outer edges only (ADR-0026 selects them
  per view); `AllFaces`: on every box edge (all six faces).
- **Reference axis through the volume** `iv::ThroughAxis`:
  - `Dim direction` — the line runs parallel to this dim, spanning the box.
  - `std::array<double,3> through{0,0,0}` — a point the line passes through, in
    **data coordinates** (the `direction` component is ignored). E.g.
    `{Dim::X, {0,0,0}}` is the X axis through the data point `(·,0,0)`.
  - `bool line{true}` (draw the line through the volume), `bool ticks{true}` (tick
    marks along it). **Numeric labels are not placed on through-axes** (they would
    overlap data); the box-edge labels (ADR-0026) serve all ticks.
- **Plot model** `iv::PlotAxes`:
  - `Axis x, y, z;  std::string title;`
  - **Visibility (all optional):** `bool boundingBox{true}` (12 cube edges);
    `bool boxTicks{true}` + `BoxTickStyle boxTickStyle{Outer}`; `bool tickLabels{true}`
    (numeric labels on major ticks); `bool axisLabels{true}`; `bool showTitle{true}`.
  - `std::vector<ThroughAxis> throughAxes{}` — optional reference axes (empty default).
  - An all-`false`, empty-`throughAxes` model draws nothing.
- **Mapping helpers:** `physical(const Axis&, double w)`, `world(const Axis&, double v)`
  as above; `std::array<double,3> dataCenter(const PlotAxes&)` (per-axis
  `(min+max)/2`) for the common "axis through the center."
- **Tick generation** (pure free functions; Heckbert "nice numbers"):
  `ticksFor(min, max, targetMajor, minorPerMajor) → AxisTicks` where
  `AxisTicks { std::vector<double> major, minor; double step; }`:
  - choose `step` as the `{1, 2, 5}·10^k` value giving ≈ `targetMajor` intervals over
    `|max−min|`; **major** ticks at every multiple of `step` within `[min,max]`
    (inclusive, tolerance `1e-9·range`); **minor** ticks subdividing each major
    interval into `minorPerMajor` parts, excluding major-coincident positions.
  - degenerate input (`max == min`, non-finite) ⇒ empty `major`/`minor`.
- **Value formatting** `formatTick(value, step) → std::string`: decimals =
  `max(0, −floor(log10(step)))` (a `0.5` step prints `"1.5"`, a `5` step prints
  `"10"`); locale-independent (`"."` decimal). Tick labels are the formatted **value
  only** (no unit — the unit lives on the axis label).

## Contract Specification
- New public host header `include/iv/plot_axes.hpp` (impl `src/plot_axes.cpp`),
  namespace `iv`. **No Vulkan/HarfBuzz** dependency; lives in core `iv`.
- `Dim`, `Axis`, `BoxTickStyle`, `ThroughAxis`, `PlotAxes`, `AxisTicks` as above;
  defaults: range `[0,1]`, empty label/unit, auto tick counts, all elements visible,
  `BoxTickStyle::Outer`, no through-axes.
- `world(axis, v) = (v − axis.min)/(axis.max − axis.min)`; `physical` is its inverse.
  Only ticks/axes with `world ∈ [0,1]` (± tolerance) lie inside the box (ADR-0026 may
  clip). A `ThroughAxis` maps to world via `world` on its two off-direction
  components; the `direction` component spans `[0,1]`.
- `ticksFor` is **deterministic** and pure; `major`/`minor` are sorted ascending,
  disjoint, within `[min,max]`; `step > 0` for non-degenerate input. `formatTick` is
  deterministic and locale-independent.
- Invariants (assertable): for `targetMajor ≥ 2` and a non-degenerate range,
  `major.size() ≥ 2` and consecutive majors differ by `step`; every `minor` lies
  strictly between two adjacent majors (or a major and a range end).
- **Through-axes carry no numeric labels** (a renderable predicate: the label set for
  a `ThroughAxis` is empty); their tick *positions* equal the corresponding `Axis`
  ticks.

## Consequences
- A small, declarative, dependency-free model the renderer (ADR-0026) and the future
  high-level API (M8) consume. Pure host ⇒ exhaustively unit-testable without a GPU.
- Callers express everything in their own units; world `[0,1]` never leaks into the
  API. Per-element optionality covers minimalist to fully-annotated plots.
- Fixes a tick *policy* (nice `{1,2,5}` numbers, value-only major labels). Log axes,
  custom formatters, or π-multiple ticks are out of scope — deferred rather than
  complicating M7.

## Alternatives Considered
- **Caller-supplied tick lists:** rejected by the maintainer — the declarative
  auto-nice-tick model is the goal; explicit ticks can be added later.
- **Axis locations in world `[0,1]`:** rejected by the maintainer — callers must work
  in their data units; the model converts.
- **Box ticks on all faces by default:** rejected (maintainer) — busy; `Outer` is the
  clearer default, `AllFaces` kept as an option.
- **Numeric labels on through-axes:** rejected — they pierce the data and would
  overlap it; labels live on the box silhouette (ADR-0026).
- **Wilkinson's extended tick algorithm / unit on every tick:** rejected for M7 —
  Heckbert `{1,2,5}` + unit-on-axis-label is simpler, standard, sufficient.

## Verification
- Unit tests (`[axes]`, pure host — no GPU): `ticksFor` on known ranges matches a
  recorded reference (`[0,1]→{0,0.2,…,1}`; `[0,100]→{0,20,…,100}`; `[−3,3]`,
  `[0.1,0.9]`), minor counts/positions, degenerate ranges → empty; `formatTick`
  precision cases; `world↔physical` round-trips; `dataCenter`; a `ThroughAxis` maps
  its `through` data point to the expected world line and exposes no labels.
- **Teeth:** perturb the nice-number rounding (drop the `2`/`5` candidates, or use a
  raw `range/n` step) → reference tick values diverge → red; perturb `formatTick`
  precision → labels diverge → red.
