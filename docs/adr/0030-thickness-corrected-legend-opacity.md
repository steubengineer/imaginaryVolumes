# ADR-0030: Thickness-Corrected Legend Opacity

- **Status:** Accepted
- **Date:** 2026-06-20
- **Supersedes:** none

## Context
The legend (ADR-0028) plots the **per-sample** authored opacity `a = transferOpacity(m)` — the
opacity a single ray sample of magnitude `m` contributes. The volume render does not show that:
a ray **accumulates** opacity along its path (ADR-0012 front-to-back compositing) with the
ADR-0020 step-spacing correction `a_corr = 1 − (1−a)^(dt·kReferenceSteps)`, `kReferenceSteps =
256`. Through a uniform region of path length `ℓ` (in unit-cube `[0,1]³` units) the accumulated
opacity is therefore

```
A(m, ℓ) = 1 − (1 − transferOpacity(m))^(256·ℓ).
```

So the legend (per-step `a`) reads far more transparent than the rendered volume (accumulated
`A`) — they do not match 1:1. The maintainer asked to correct the legend for this "thickness"
effect (2026-06-20). The catch: `ℓ` is not constant — a center ray crosses ≈1 unit, corners
more, and real features are thinner than the whole cube — so no single thickness matches every
ray. Hence a **reference thickness** `ℓ_ref` is chosen and made tunable; and because the shown
opacity is then no longer the analytic transfer value, the legend must **display `ℓ_ref`** so a
reader can still recover `a` analytically (`a = 1 − (1−A)^(1/(256·ℓ_ref))`).

Read: ADR-0013 (`a`), ADR-0020 (the `kReferenceSteps = 256` correction the volume applies),
ADR-0012 (compositing), ADR-0028 (the legend draws through the host evaluators; the consistency
principle), ADR-0029 (`PlotOptions` single-source fan-out; the viewer's per-frame closure reads
display state from `RenderParams`).

Maintainer decision (2026-06-20): **tunable reference thickness, soft default** (a readable
gradient, not the full-cube true-1:1 that saturates), with a live viewer hotkey, plus a text
label of the thickness value.

## Decision
**(1) Host evaluator.** Add `float iv::accumulatedOpacity(float perSampleAlpha, float thickness)
noexcept` to `iv/transfer.hpp` (core `iv`): returns `1 − (1−a)^(kReferenceSteps·thickness)` with
`kReferenceSteps = 256` — the *same* accumulation the shader applies — for `thickness > 0`, and
`perSampleAlpha` unchanged for `thickness ≤ 0` (the escape hatch: no correction = the ADR-0028
per-sample legend). `kReferenceSteps` is exposed as `inline constexpr float iv::kReferenceSteps =
256.0f` and MUST equal the shader's constant.

**(2) Legend opacity.** The swatch alpha at normalized position `v` becomes
`accumulatedOpacity(transferOpacity(m), ℓ_ref)` — i.e. the opacity a uniform slab of magnitude
`m` and thickness `ℓ_ref` would render. Color (phase) and the magnitude axis/ticks are unchanged.

**(3) Reference-thickness knob (new public fields, default 0.1).**
- `iv::LegendSpec::referenceThickness` (float, default `0.1`) — the `ℓ_ref` the legend uses.
- `iv::PlotOptions::legendThickness` (float, default `0.1`) — the facade input, fanned to both
  the legend and the live channel.
- `iv::vk::RenderParams::legendThickness` (float, default `0.1`) — the **live display channel**:
  the viewer mutates it and the ADR-0029 closure copies `cam.legendThickness →
  LegendSpec::referenceThickness` each frame (exactly as it already does for colormap / opacity /
  density / decades). **It does not affect the ray-march** (the renderer ignores it; not packed
  into the UBO) — it is a legend display parameter carried here only so the viewer can drive it
  live.

**(4) Thickness label.** `buildLegend` draws a small text label of `ℓ_ref` near the swatch
(e.g. under the phase caption), formatted to ~2 significant figures (e.g. `ℓ = 0.10`); when
`ℓ_ref ≤ 0` it reads as the uncorrected per-sample legend. This keeps the legend analytically
interpretable once the displayed opacity is thickness-corrected.

**(5) Viewer hotkey.** `[` / `]` decrease / increase `RenderParams::legendThickness` by an
additive step (`0.02`), clamped to `[0, 2]` (`0` = uncorrected). Live; the label updates.

## Contract Specification
- **Signatures:** `iv::accumulatedOpacity(float, float) noexcept` and `inline constexpr float
  iv::kReferenceSteps` in `iv/transfer.hpp`. New fields as in (3), all defaulting to `0.1`.
- **Formula (binding):** legend swatch alpha `= accumulatedOpacity(transferOpacity(m, …),
  ℓ_ref)`; `accumulatedOpacity(a, ℓ) = (ℓ ≤ 0) ? a : 1 − (1−a)^(256·ℓ)`. Clamped to `[0,1]`;
  `a ≤ 0 ⇒ 0`, `a ≥ 1 ⇒ 1`, finite for all inputs (`thickness ≥ 0`, no `pow` of a negative base
  since `1−a ∈ [0,1]`).
- **Consistency invariant:** for a uniform field of magnitude `m`, the volume's accumulated
  opacity over a ray of path length `ℓ` equals `accumulatedOpacity(transferOpacity(m), ℓ)` within
  fp tolerance — the same relation the legend uses, so legend and render agree for a slab of
  thickness `ℓ_ref` (ADR-0028's "legend matches the render", now including thickness).
- **`RenderParams::legendThickness` is render-inert:** `fillUbo` does not read it; the ray-march
  output is bit-identical regardless of its value (an assertable invariant — a render test pins
  that two renders differing only in `legendThickness` are equal).
- **Defaults & back-compat:** all three fields default to `0.1`; a caller wanting the exact
  ADR-0028 per-sample legend sets the thickness to `0`. No existing signature changes.
- **Threading/ownership:** unchanged (ADR-0007); `accumulatedOpacity` is a pure function.

## Consequences
- The legend's opacity reflects the volume's thickness/accumulation, so a low-magnitude field
  whose rays accumulate to visible opacity no longer shows a near-black legend (the screenshot
  case). The soft default keeps a readable gradient; the hotkey dials it to the data.
- `accumulatedOpacity` reuses the ADR-0020 model, so legend↔render consistency extends to
  thickness; the thickness label keeps the legend quantitatively invertible.
- A legend-only field lives in `RenderParams` (the one live channel the closure sees). This is
  consistent with the colormap/opacity/density/decades already read there, but it is the first
  member that does not affect the render — documented and guarded by the render-inert test.
- No shader / pipeline / perf change; the ADR-0019 contract and pixel-exact renderer tests are
  untouched.

## Alternatives Considered
- **`ℓ_ref = 1` (true full-cube 1:1) default:** rejected by the maintainer — the exponent 256
  saturates the swatch, collapsing the opacity gradient. Available by setting the thickness to 1.
- **Fixed thickness, no knob:** rejected — no single `ℓ` fits all data/views; tunability + the
  label are needed for an honest, readable legend.
- **A separate live-parameter channel on the Viewer (avoid `RenderParams`):** rejected for now —
  the ADR-0026 `FrameCallback` passes `RenderParams`; adding a parallel channel is more surface
  than one documented render-inert field.
- **Per-ray / data-derived thickness:** rejected — view- and data-dependent, not a stable legend
  scale; the reference thickness + label is the analytic, reproducible choice.
- **Showing the per-sample `a` axis instead of `ℓ`:** the label shows `ℓ_ref` (one scalar) since
  `a` varies along the bar; with `ℓ_ref` and the magnitude axis the reader recovers `a`.

## Verification
- **`accumulatedOpacity` (host, `[transfer]`):** `accumulatedOpacity(a,0) == a`;
  `accumulatedOpacity(1,ℓ)==1`, `(0,ℓ)==0`; a mid value matches `1−(1−a)^(256ℓ)` for a chosen
  `(a,ℓ)`; monotone increasing in both `a` and `ℓ`. **Teeth:** dropping the `256` factor (or the
  `ℓ≤0` guard) breaks the anchors.
- **Legend uses it (`[legend]`):** with `referenceThickness > 0` a low per-sample swatch is
  markedly more opaque than at `0` (same field); the thickness label is present. **Teeth:**
  forcing the uncorrected `a` leaves the swatch dark → the "more opaque" check reddens.
- **Render-inert field (`[vk][renderer]`):** two `render()`s differing only in
  `RenderParams::legendThickness` are pixel-identical. **Teeth:** packing it into the UBO / shader
  would diverge.
- **Legend ↔ render thickness consistency (`[vk]`):** a uniform field rendered over black to full
  saturation, vs `accumulatedOpacity(transferOpacity(m), L)` for the center-ray path length — the
  legend's corrected opacity tracks the volume's accumulation (within tolerance).
- **Sanitizers:** ASan+UBSan clean over `accumulatedOpacity` and the legend/viewer paths.
