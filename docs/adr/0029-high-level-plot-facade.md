# ADR-0029: High-Level Plot Facade (`makePlot` / `renderPlot`)

- **Status:** Accepted
- **Date:** 2026-06-20
- **Supersedes:** none

## Context
Today a fully labeled plot takes a hand-written multi-step dance (see
`examples/view_demo.cpp`): create a `Viewer`/`Context`, build a `Volume` from its context,
`setVolume`, set `RenderParams`, create a `Shaper`, build a `PlotAxes`, install a
`setOnFrame` that rebuilds annotations each frame, then `run()` — or, headless, stand up a
`Context` + `Renderer` + `Volume`, build an `Overlay`, `render()`, read back. M8's second
goal is a **one-call convenience API** over this, producing a fully labeled plot **both
headless and in the viewer** (MILESTONES M8 "Done when").

Read: ADR-0008/0015 (`Volume::create` from `span<complex<float|double>> + GridDims`);
ADR-0010 (`MagnitudeRange`); ADR-0012/0013/0014/0027 (`RenderParams`); ADR-0016/0017/0018
(`Viewer`: `create`, `context`, `setVolume`, `params`, `setOnFrame`, `run`); ADR-0024
(`PlotAxes`); ADR-0026 (`buildAnnotations`, `setOnFrame` per-frame rebuild); ADR-0028
(`LegendSpec`, `buildLegend`, the host transfer evaluators). Isolation gates (D-0001/D-0026,
D-0033/D-0035): core `iv` builds with `IV_BUILD_VIEWER=OFF` *and* `IV_BUILD_TEXT=OFF`; only
`iv_viewer` links GLFW; only `iv_text` links HarfBuzz.

Maintainer decision (2026-06-20): the interactive entry point **returns a configured (not
yet running) `Viewer`** the caller drives — so they can tweak `params()` or the loop —
alongside a **headless `renderPlot`** that returns a fully labeled image.

## Decision
Add a thin facade — **not** in core `iv` (it depends on the viewer/text layers) — with one
options aggregate and two entry points.

```cpp
namespace iv {

struct PlotOptions {
    PlotAxes axes{};                  // box / ticks / labels / title (ADR-0024)
    bool     showLegend{true};        // the phase–magnitude legend (ADR-0028)
    std::string magnitudeLabel{"|z|"};
    std::string phaseLabel{"arg z"};

    // Transfer state — the SINGLE source, propagated to both the RenderParams used to
    // render AND the LegendSpec, so the legend always matches the image (ADR-0028).
    std::uint32_t colormapMode{0};    // ADR-0014
    std::uint32_t opacityMode{0};     // ADR-0013
    float         densityScale{1.0f};
    float         logDecades{0.0f};   // ADR-0027
    std::array<float,4> background{0.05f, 0.05f, 0.07f, 1.0f};

    std::optional<MagnitudeRange> magnitudeRange{}; // else the Volume's auto range
    float labelPixelSize{16.0f};      // Shaper size (tick labels; ADR-0026 scales the rest)

    std::uint32_t width{1000};        // window (makePlot) / image (renderPlot) size
    std::uint32_t height{1000};
    const char*   windowTitle{"imaginaryVolumes"}; // window caption (axes.title is the plot title)
};

// Interactive: build the window/Context/Volume, set params from options, install a
// per-frame callback that rebuilds the box/axes (ADR-0026) and the legend (ADR-0028) from
// the LIVE RenderParams, and return the configured Viewer WITHOUT running it. The caller
// does v->run() (or runFrames), and may edit v->params()/camera() first. Needs GLFW+text.
[[nodiscard]] Result<iv::vk::Viewer> makePlot(std::span<const std::complex<float>>  field,
                                              GridDims dims, const PlotOptions& options = {});
[[nodiscard]] Result<iv::vk::Viewer> makePlot(std::span<const std::complex<double>> field,
                                              GridDims dims, const PlotOptions& options = {});

// Headless: render one fully labeled image (box/axes + legend) of width×height and read it
// back (ADR-0006 layout). No window. Needs text, not GLFW.
[[nodiscard]] Result<iv::vk::ImageReadback> renderPlot(std::span<const std::complex<float>>  field,
                                                       GridDims dims, std::uint32_t width,
                                                       std::uint32_t height,
                                                       const PlotOptions& options = {});
[[nodiscard]] Result<iv::vk::ImageReadback> renderPlot(std::span<const std::complex<double>> field,
                                                       GridDims dims, std::uint32_t width,
                                                       std::uint32_t height,
                                                       const PlotOptions& options = {});
}
```

**Live legend without coupling the Viewer to text.** `makePlot` installs a `setOnFrame`
closure that owns its rendering state — a `Shaper`, the `PlotAxes`, and a `LegendSpec` —
via a `shared_ptr` captured by value (so the closure stays copyable for
`std::function`). Each frame the closure calls `buildAnnotations(...)` then (if
`showLegend`) `buildLegend(...)` into the live `Overlay`, reading the frame's
camera-applied `RenderParams` so toggling colormap/opacity/density/decades updates the
legend live. The returned `Viewer` therefore needs no HarfBuzz type — the text dependency
lives inside the closure, compiled in the facade translation unit.

**Single source of transfer state.** The facade derives both the `RenderParams`
(`colormapMode/opacityMode/densityScale/logDecades/background`) and the `LegendSpec`
(same four + the effective `MagnitudeRange`) from `PlotOptions`, so they cannot disagree.

## Contract Specification
- **Placement & isolation:** the facade is **outside core `iv`**. `renderPlot` requires
  `IV_BUILD_TEXT` (Volume/Renderer are core vk; annotations/legend need a `Shaper`); it
  does **not** require GLFW. `makePlot` requires `IV_BUILD_VIEWER && IV_BUILD_TEXT`. Core
  `iv`, the tests, and `iv_bench` continue to build with both gates OFF (the existing
  isolation gates are unchanged — this ADR adds no symbol to core `iv`). Exact CMake target
  wiring (likely: `renderPlot` in the text library/target, `makePlot` in the viewer target)
  is journaled at implementation; the binding rule is the gate dependency above.
- **Signatures:** as declared (header e.g. `iv/plot.hpp`, behind the gates). Move-only
  `Viewer` / `ImageReadback` returned by value in `Result<>` (consistent with ADR-0017/0006).
- **Behavior:** `makePlot` does **not** run the loop (the maintainer's choice); it returns a
  Viewer with the Volume set, `params()` initialized from `PlotOptions`, and the per-frame
  callback installed. `renderPlot` builds one Overlay (annotations + legend), renders, and
  returns the readback; it owns its transient `Context`/`Renderer`/`Shaper`.
- **Validation/errors:** inputs are validated by the underlying `Volume::create`
  (dims/shape/range) and `Viewer::create` (surface/swapchain), whose `Errc` failures
  propagate unchanged; no new error codes. A non-finite `MagnitudeRange` override is
  rejected as in ADR-0010.
- **Defaults:** `PlotOptions{}` yields the project's standard look (box + nice ticks +
  labels + legend, twilight colormap, linear opacity, 1000×1000). Axis ranges/labels come
  from `options.axes`; if left default they are the unit-cube `[0,1]` extents.
- **Threading/lifetime:** unchanged (ADR-0007). The facade owns no global state; the
  per-frame closure's `shared_ptr` state lives as long as the returned `Viewer`.

## Consequences
- "Plot this field" is one call for both interactive and headless use; the by-hand dance in
  `view_demo.cpp` collapses to `makePlot(...)->run()`.
- Returning a *configured* Viewer keeps full control (edit params, drive `runFrames`, embed
  in a larger loop) — strictly more flexible than a blocking `plot()`, at one extra line.
- `renderPlot` gives CI/scripts a deterministic, fully labeled image without a display —
  the headless half of the M8 gate and a natural home for golden-image checks.
- The single-source transfer state structurally prevents the legend/render mismatch ADR-0028
  guards against.
- The facade stays out of core `iv`, preserving both isolation gates; the cost is that the
  one-call convenience is only available when text (and, for `makePlot`, the viewer) is built
  — acceptable, since a labeled plot inherently needs text.

## Alternatives Considered
- **Blocking `plot(field, dims, opts)` that opens the window and runs to close:** the other
  offered option; rejected by the maintainer in favor of returning a configured Viewer
  (more control). A caller wanting the blocking form writes `makePlot(...)->run()`.
- **Putting the facade in core `iv`:** rejected — it would drag GLFW/HarfBuzz into the core
  and break the isolation gates (D-0001/D-0033). The facade lives in the coupled layer.
- **A new `PlotOptions` that *embeds* a full `RenderParams`/`LegendSpec`:** rejected —
  duplicates the transfer knobs and invites inconsistency; `PlotOptions` holds the transfer
  state once and the facade fans it out.
- **Coupling `Viewer` to a `Shaper` member for the live legend:** rejected — would couple
  the viewer target to HarfBuzz; the self-owning per-frame closure keeps `Viewer`
  text-agnostic.

## Verification
- **Headless labeled image (teeth):** `renderPlot` on a known field yields an image with
  the volume *and* overlay present — assert nonzero overlay coverage (box/legend pixels in
  expected screen regions) over the bare-volume baseline. Teeth: disable the overlay build
  → the labeled regions vanish vs. baseline → red.
- **Legend ↔ render consistency (teeth):** in the `renderPlot` image, a legend swatch pixel
  at a chosen `(phase, magnitude)` matches a volume pixel of a uniform field with that same
  `(phase, magnitude)` (both flow through ADR-0028's host evaluators / the shader). Teeth:
  perturb `PlotOptions.colormapMode` for the render but not the legend (a deliberate
  desync) → the two diverge → red; with the facade's single-source wiring they always agree.
- **makePlot configuration (teeth):** after `makePlot`, the Viewer has a Volume and the
  callback installed; `runFrames(N)` is validation-clean and the frames carry the overlay
  (a present-path readback shows box/legend pixels). Teeth: omit the `setOnFrame` install →
  no overlay in the frame → red. Live update: change `params().colormapMode` between frames
  → the legend swatch hue changes accordingly.
- **Isolation (teeth):** core `iv` / tests / `iv_bench` still build and link with
  `-DIV_BUILD_VIEWER=OFF -DIV_BUILD_TEXT=OFF` (no `iv::makePlot`/`renderPlot` symbol leaks
  into core). Teeth: a stray facade include in a core TU breaks the text-free build → caught
  by the existing isolation gate.
- **Sanitizers:** ASan+UBSan clean over `renderPlot` (headless, on the test gate);
  `makePlot` exercised via the viewer's `runFrames` path.
