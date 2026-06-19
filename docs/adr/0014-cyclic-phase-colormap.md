# ADR-0014: Cyclic Phase Colormap (arg → RGB)

- **Status:** Accepted
- **Date:** 2026-06-19
- **Supersedes:** none

## Context
The ray-marcher (ADR-0012) needs a per-sample `rgb` from a voxel's phase. The
mapping must be **cyclic** (phase wraps at ±π) and, per **D-0007**, default to a
**perceptually-uniform cyclic** map with the classic **HSV** hue wheel selectable.
Cites **ADR-0009** (phase stored as `arg(z) ∈ [−π, π]`, G channel), **ADR-0012**
(consumes `rgb`). Opacity is ADR-0013; this ADR is **color only**.

## Decision
Normalize phase `θ ∈ [−π, π]` to `t = (θ + π) / (2π) ∈ [0, 1]`, **cyclic**: `t = 0`
and `t = 1` coincide, and the seam is at `θ = ±π` (the negative real axis). A
`colormapMode` uniform selects the map:

- **`colormapMode == 0` — perceptually-uniform cyclic (default).** A **256-entry
  baked RGB LUT** of a twilight-style cyclic colormap, sampled with **linear
  interpolation and cyclic wrap** at the seam (entry `255` interpolates back to
  entry `0`). The LUT is committed as generated data (under `shaders/` or an
  embedded header) with a regeneration script in `tools/` and its source
  documented (reproducible, like `tools/regenerate_adr_index.py`).
- **`colormapMode == 1` — HSV hue wheel.** Analytic: `hue = t` (full circle),
  `S = 1`, `V = 1`, mapped to RGB by the standard HSV→RGB formula. (E.g. `θ = 0 ⇒
  t = 0.5 ⇒ hue = 180° ⇒ cyan`.) Data-free and exactly predictable.

The result is **non-premultiplied** `rgb`; ADR-0012 multiplies by `α` during
compositing.

## Contract Specification
- `t = (θ + π)/(2π)`, cyclic; `θ` and `θ ± 2π` yield the **same** color; `θ = −π`
  and `θ = +π` yield the same color (seam).
- `colormapMode` selects: `0` = 256-entry twilight-style LUT (linear-interpolated,
  wrapped); `1` = analytic HSV (`hue = t`, `S = V = 1`, standard HSV→RGB).
- The default LUT is exactly 256 RGB entries, committed and reproducible from a
  documented source via a `tools/` script.
- Output is `rgb ∈ [0,1]³`, non-premultiplied.

## Consequences
- Cyclic mapping makes phase wrap seamlessly (only the defined seam is a
  boundary); a perceptually-uniform default avoids HSV's brightness banding
  (D-0007), while HSV remains for domain-coloring familiarity.
- The LUT is small committed data; regeneration is offline and optional.
- Color and opacity are independent (this ADR vs ADR-0013), so phase and magnitude
  are visualized orthogonally.

## Alternatives Considered
- **HSV-only:** rejected — perceptual brightness banding misleads (D-0007).
- **Analytic-only "perceptually-uniform" palette** (e.g. cosine palettes):
  rejected as the default — true perceptual uniformity needs a fitted LUT; such a
  palette could be an additional selectable map later.
- **More colormaps / caller-supplied LUTs:** deferred — Backlog **B-0005**.

## Verification
- **HSV anchors (exact):** the host replicates HSV→RGB and asserts known phases —
  `θ = 0 → cyan`, `θ = −π/2 → t=0.25 → hue 90°`, `θ = +π/2 → t=0.75 → hue 270°` —
  bit-reproducibly (analytic).
- **Phase-offset teeth:** perturbing `t` by a constant `δ` (a wrong seam/offset)
  shifts a known-phase pixel's hue → red.
- **Cyclic teeth:** `θ = −π` and `θ = +π` map to the same color; a wrong
  (non-wrapping) normalization breaks this → red.
- **Selector / default LUT:** `colormapMode 0` and `1` differ for a known phase
  (selector works); LUT anchors are asserted against the committed table; a
  uniform-phase field render shows the expected hue (ties to ADR-0012).
