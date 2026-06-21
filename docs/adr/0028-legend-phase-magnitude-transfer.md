# ADR-0028: Legend for the Phase–Magnitude Transfer Function

- **Status:** Accepted
- **Date:** 2026-06-20
- **Supersedes:** none

## Context
M8 completes the "usable scientific plotting library": the rendered volume needs a
**legend** so a reader can decode what a given hue + opacity means. The two mappings
are fixed contracts — **arg(z) → color** (ADR-0014: cyclic twilight LUT default, HSV
selectable) and **abs(z) → opacity** (ADR-0013 linear/log, ADR-0027 decade window).
The ADR-0020 dt-correction is a *compositing/path-length* effect, **not** part of the
per-sample transfer a legend depicts, so the legend mirrors the per-sample α only.

The legend MUST stay consistent with whatever transfer state is active — the live
`RenderParams::{colormapMode, opacityMode, densityScale, logDecades}` plus the Volume's
`MagnitudeRange` — and update as the user toggles them (ADR-0018 keys).

Read: ADR-0013/0014/0020/0027 (the mappings the legend mirrors); ADR-0021 (the overlay
substrate — colored line/triangle lists + glyph quads composited over the volume in one
pass, transformed by a single `Overlay::transform`); ADR-0024 (`ticksFor` nice numbers,
`formatTick`); ADR-0025/0026 (`buildAnnotations` fills an `Overlay`; labels are
screen-space glyphs; the *same* Overlay draws identically headless `render()` and in the
viewer `recordFrame()`). The committed twilight LUT (`iv::vk::kTwilightLut`, ADR-0014) is
**data shared with the GPU**.

Maintainer decision (2026-06-20): the legend is a single **rectangular 2-D gradient
swatch** placed to the **right of the volume** — **color varying horizontally with phase**
(−π … +π) and **opacity varying vertically with magnitude** — with **phase ticks
(−π, 0, +π) on the bottom edge** and **nice-number magnitude ticks on the right edge**.

## Decision
Three pieces: host transfer evaluators (the consistency anchor), a screen-space overlay
channel (extends ADR-0021), and a legend spec + builder.

### (1) Host transfer evaluators — the single source of truth
Pure-host functions in **core `iv`** (no Vulkan, no HarfBuzz; header `iv/transfer.hpp`)
that mirror the shader's two mappings, so any host depiction of the transfer function is
consistent with the GPU by construction:

- `std::array<float,3> iv::phaseColor(float phaseRadians, std::uint32_t colormapMode) noexcept;`
  Mirrors `ray_march.comp::sampleColor`. `t = (phaseRadians + π)/(2π)`, cyclic.
  - `colormapMode == 0`: sample the committed 256-entry `kTwilightLut` (ADR-0014) with
    **linear interpolation + repeat wrap**, entries at texel centers `(i+0.5)/256` — the
    *same data and sampling rule the GPU sampler uses*, so host == GPU for mode 0.
  - `colormapMode == 1`: analytic HSV, the exact `hsv2rgb(t)` of the shader
    (`abs(fract(t + {1, 2/3, 1/3})·6 − 3) − 1`, clamped to [0,1]).
- `float iv::transferNormalized(float magnitude, MagnitudeRange range,
  std::uint32_t opacityMode, float logDecades) noexcept;`
  The magnitude → normalized position `mn ∈ [0,1]` (the abscissa of the opacity ramp,
  **without** `densityScale`): linear `clamp(m/max,0,1)`; log full-range
  `clamp((ln m − ln minP)/(ln max − ln minP),0,1)`; log decade-window (`logDecades>0`)
  `clamp(1 + (ln m − ln max)/(logDecades·ln10),0,1)`; degenerate ranges → 0. `log` is
  never evaluated at ≤ 0 (the guards precede it, per ADR-0013).
- `float iv::transferOpacity(float magnitude, MagnitudeRange range,
  std::uint32_t opacityMode, float densityScale, float logDecades) noexcept;`
  `= clamp(transferNormalized(...) · densityScale, 0, 1)` — the shader's per-sample α
  (`sampleOpacity`, pre-dt-correction).

`transferOpacity` is defined in terms of `transferNormalized`. The legend uses
`transferNormalized` for vertical *positions* (magnitude axis + ticks) and
`transferOpacity` for cell *alpha* (what the eye sees, density included).

### (2) Screen-space overlay channels — extends ADR-0021
`iv::vk::Overlay` gains two members of the existing `OverlayVertex` type:

```cpp
std::vector<OverlayVertex> screenLines;     // clip-space (NDC) line list; identity transform
std::vector<OverlayVertex> screenTriangles; // clip-space (NDC) triangle list; identity transform
```

These are **clip-space (NDC)** and drawn with the **identity** transform, independent of
`Overlay::transform` (which keeps carrying the world view-projection for the 3-D box/axes,
ADR-0026). Glyphs are already clip-space. The renderer draws the screen-space geometry in
the *same* overlay pass with an identity push-constant, so one `Overlay` holds both the
world-space annotations and the 2-D legend, drawn identically in `render()` and
`recordFrame()`. `Overlay::empty()` accounts for the new lists.

### (3) Legend spec + builder
A pure-host `iv::LegendSpec` (core `iv`, `iv/legend.hpp`) and a builder in `iv_text`
(it needs a `Shaper` for the numeric labels):

```cpp
namespace iv {
struct LegendSpec {
    MagnitudeRange range{};            // magnitude bounds (the Volume's effective range)
    std::uint32_t  colormapMode{0};    // ADR-0014 (must match the render)
    std::uint32_t  opacityMode{0};     // ADR-0013
    float          densityScale{1.0f};
    float          logDecades{0.0f};   // ADR-0027
    std::string    magnitudeLabel{"|z|"};
    std::string    phaseLabel{"arg z"};
    // Screen rect in NDC, default a panel on the right (x in ~[0.74,0.84], y centered).
    std::array<float,4> rectNdc{0.74f, -0.45f, 0.84f, 0.45f}; // {x0,y0,x1,y1}, y up
    bool show{true};
};
}

namespace iv::text {
void buildLegend(iv::vk::Overlay& overlay, const iv::LegendSpec& spec,
                 std::uint32_t fbWidth, std::uint32_t fbHeight, Shaper& shaper);
}
```

`buildLegend` **appends** (never clears) to the overlay's screen-space channels + glyphs,
so a caller may call `buildAnnotations` first (box/axes, world transform + cleared) then
`buildLegend` (legend, screen-space) into the same `Overlay`, sharing one `Shaper`/atlas.
It emits:
- the swatch: a grid mesh into `screenTriangles`, per-vertex **color =
  `phaseColor(θ_col, colormapMode)`** (θ from −π at the left edge to +π at the right) and
  per-vertex **alpha = `transferOpacity(m_row, …)`** (magnitude from the ramp's low bound
  at the bottom to `max` at the top, positioned by `transferNormalized`); the grid is fine
  enough that GPU interpolation between vertices is visually faithful. The swatch is
  composited over an **opaque backing quad** (drawn first, into `screenTriangles`) so
  partial opacity reads against a known tone;
- a border + ticks into `screenLines`: **bottom edge** phase ticks at θ = −π, 0, +π;
  **right edge** magnitude ticks at nice numbers (`ticksFor` over the active magnitude
  domain), each at vertical position `transferNormalized(value, …)`;
- labels (glyphs, `appendText`, the shared `shaper`): "−π", "0", "π" under the bottom
  ticks; `formatTick` values beside the right ticks; the `phaseLabel` / `magnitudeLabel`
  captions.

The ramp's low magnitude bound is `0` (linear), `minPositive` (log full-range), or
`max·10^−logDecades` (log decade-window) — matching `transferNormalized`'s domain.

## Contract Specification
- **Signatures/layout:** `iv/transfer.hpp` (`phaseColor`, `transferNormalized`,
  `transferOpacity` — all `noexcept`, pure, core `iv`); `iv/legend.hpp` (`LegendSpec`);
  `iv/text/legend_builder.hpp` (`iv::text::buildLegend`, `iv_text`). New `Overlay`
  members `screenLines`, `screenTriangles` (type `OverlayVertex`).
- **Consistency invariant (the binding property):** for the same arguments, `phaseColor`
  equals the shader's `sampleColor` (mode 0: identical because the *same* `kTwilightLut`
  + linear/repeat; mode 1: identical analytic HSV) and `transferOpacity` equals the
  shader's `sampleOpacity` (pre-dt-correction), within fp32 tolerance. The legend's swatch
  colors/alphas are produced **only** through these functions, so the legend cannot drift
  from the render.
- **Cyclic/degenerate:** `phaseColor(−π,·)` == `phaseColor(+π,·)` (seam). All-zero / degenerate
  range → `transferNormalized` = 0 → fully transparent swatch; no NaN/Inf (guards precede
  every `log`). `densityScale ≤ 0` → α = 0.
- **Coordinates:** screen-space overlay geometry is clip-space NDC, y-up at the vertex
  (the overlay vertex shader convention, ADR-0021); `rectNdc` is {x0,y0,x1,y1} with y up.
- **Ownership/threading:** unchanged from ADR-0021/0026 — `buildLegend` only mutates the
  passed `Overlay` and reads the `Shaper`; not thread-safe (ADR-0007). No GPU resource
  ownership changes; the renderer draws the new channels with the existing line/triangle
  pipelines (no new pipeline, descriptor, or device feature).
- **Error semantics:** total functions (no failure path); out-of-domain inputs clamp per
  the formulas above.

## Consequences
- The legend is *provably* consistent with the active transfer function/colormap — the
  same host code, and for the default colormap the same LUT bytes as the GPU.
- Screen-space overlay channels generalize ADR-0021 cleanly (one Overlay = 3-D scene +
  2-D HUD) and cost nothing when unused; they are reusable for any future 2-D overlay.
- `phaseColor` / `transferNormalized` / `transferOpacity` are independently useful public
  host utilities (e.g. CPU previews, tests, future exporters).
- No new pipeline/feature keeps the ADR-0019 perf contract and the pixel-exact renderer
  tests intact.
- A 2-D swatch shows the *combined* (phase × magnitude) transfer in one figure, denser
  than separate wheel + bar; the trade-off is it is less conventional than a discrete
  color wheel (accepted by the maintainer).

## Alternatives Considered
- **Discrete color wheel + separate opacity bar** (my original options A–C): rejected by
  the maintainer in favor of the unified 2-D swatch, which shows the joint transfer.
- **A textured-quad legend sampling the GPU LUT in a new shader:** rejected — heavier
  (new pipeline/descriptor) and it would *not* exercise the host evaluators that give the
  consistency teeth; per-vertex color/alpha through the existing pipelines is simpler.
- **A second Overlay (identity transform) drawn after the first:** rejected — would change
  the `render()`/`recordFrame()` single-`Overlay*` signature (ADR-0021/0026); screen-space
  channels on the one Overlay are more localized.
- **Folding `screenLines/Triangles` into a superseding ADR-0021:** rejected — append-only;
  this ADR *extends* ADR-0021 (new members + draw rule), leaving it immutable.
- **Legend reflecting the dt-corrected α:** rejected — dt-correction is a ray-integration
  effect, not the per-sample transfer; the legend depicts the authored opacity curve.

## Verification
- **Colormap consistency (mode 0):** `phaseColor(θ,0)` equals a host reference that samples
  `kTwilightLut` with linear+repeat at `t=(θ+π)/2π`. Teeth: perturb the seam/offset (a
  wrong `t`) or drop the wrap → a known-phase color diverges → red. (Same data as the GPU,
  so this also pins host↔GPU agreement for the default map.)
- **Colormap consistency (mode 1) & opacity formulas:** analytic anchors — `phaseColor(0,1)`
  = cyan, ±π/2 → 90°/270°; `transferOpacity` at `m=max` (=`clamp(densityScale)`), `m≤0`
  (=0), the log floor, and a decade-window point — asserted against hand-computed values.
  Teeth: flip linear/log or drop the decade branch → red.
- **GPU cross-check (host evaluator ↔ shader):** render a *uniform* field of known
  `(m, θ)` (the M4 uniform-field harness) and assert the composited pixel matches a host
  prediction built from `phaseColor` + `transferOpacity` within tolerance. Teeth: perturb
  either host function → the prediction no longer matches the GPU render → red. This is the
  binding "legend matches the transfer function" test.
- **Legend render (both paths):** a headless `render()` with a `LegendSpec` shows the swatch
  (nonzero coverage in `rectNdc`, the expected hue at the right column, decreasing alpha
  toward the bottom) and the tick labels; the same Overlay in the viewer (`recordFrame`)
  is validation-clean. Teeth: skip the swatch draw → blank rect → red; perturb the bottom
  tick positions → −π/0/π labels misplaced vs. reference → red.
- **Sanitizers:** ASan+UBSan clean over the host evaluators and the builder.
