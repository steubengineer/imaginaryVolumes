# ADR-0036: Additive Cyclic Colormaps (`infinity`, `grayscale`)

- **Status:** Accepted
- **Date:** 2026-07-03
- **Supersedes:** none

> Amends **ADR-0014** (adds two selectable cyclic phase colormaps beyond the twilight LUT / analytic
> HSV; supersedes neither — twilight stays `colormapMode 0`, HSV stays `1`). Resolves **B-0017** (the
> maintainer's own maps) and partially addresses **B-0005** (more colormaps). Builds on the ADR-0014
> normalization `t = (θ + π)/(2π)`, cyclic, linear + REPEAT.

## Context
ADR-0014 defines exactly two phase maps: `colormapMode 0` (a committed 256-entry twilight LUT) and
`colormapMode 1` (analytic HSV). The maintainer has authored two additional **cyclic** phase maps he
wants selectable, delivered as committed 256-row RGBA CSVs in `tools/colormaps/`:
- **`infinity`** — `infinityRGBAcolormap.csv` (a purple-anchored cyclic map).
- **`grayscale`** — `grayscaleRGBAcolormap.csv` (black → white → black, cyclic; colourless/print- and
  CVD-friendly phase rendering).

Both are 256 rows of `r,g,b,a` floats in `[0,1]`, sampled inclusively over one cycle so the first and
last rows are identical (the seam colour). They must be **additive** (twilight remains the default) and
obey the ADR-0014 cyclic contract (seam at `θ = ±π`). The current GPU path binds a single `sampler1D
uColormap` and selects `LUT vs HSV` on `modes.x`; the host mirror `iv::phaseColor` samples the one
committed `kTwilightLut`. Supporting more than one LUT is therefore a contract change on both sides.

## Decision
1. **Two new modes, additive.** `colormapMode` gains:
   - `2` — **infinity** (256-entry baked LUT),
   - `3` — **grayscale** (256-entry baked LUT).
   `0` (twilight) and `1` (HSV) are unchanged; `0` stays the default. All LUT modes (`0/2/3`) use the
   ADR-0014 rule: `t = (θ+π)/(2π)`, linear interpolation, REPEAT wrap; `1` stays analytic HSV.
   A named count `kColormapModeCount = 4` is the single source of truth for "how many modes".

2. **GPU: a layered LUT.** Replace the single `sampler1D uColormap` with a **`sampler1DArray`** whose
   layers are the baked LUTs in mode order *skipping HSV* — layer 0 = twilight, layer 1 = infinity,
   layer 2 = grayscale. The shader keeps HSV analytic and samples the array for LUT modes:
   `P.modes.x == 1u ? hsv2rgb(t) : texture(uColormap, vec2(t, layerOf(P.modes.x))).rgb`, where
   `layerOf` maps `mode → layer` (`0→0, 2→1, 3→2`; any other → 0). REPEAT applies to the 1-D coord
   per layer (no cross-layer blend — the layer index is integral). The renderer uploads all LUT layers
   from the committed data in one image (array of 1-D layers), one staging copy.

3. **Committed data + generator.** `include/iv/vk/colormap_lut.hpp` becomes a small **table**: a 2-D
   array `kColormapLuts[kColormapLutCount][256*4]` (RGBA8) + `kColormapLutNames[]` +
   `kColormapLutCount` (= 3: twilight, infinity, grayscale — HSV is analytic, not stored).
   `tools/gen_colormap.py` is extended to bake, in order: twilight from matplotlib (unchanged source),
   then each `tools/colormaps/<name>RGBAcolormap.csv`. **Sampling convention:** each map is baked to the
   *same* texel-center + REPEAT rule as twilight — the CSV is treated as a cyclic piecewise-linear map
   over `t ∈ [0,1]` (its duplicated endpoint row folded to the single seam) and resampled at
   `(i+0.5)/256`, so every map shares one seam-uniform sampling rule. The generator gains a `--check`
   mode (diff regenerated header vs committed, like `tools/regenerate_adr_index.py --check`). Do not
   hand-edit the header.

4. **Host mirror.** `iv::phaseColor(θ, mode)` selects the LUT layer for modes `0/2/3` (HSV for `1`),
   reusing the existing `sampleLut` against the matching `kColormapLuts` row — so host and shader stay
   bit-for-bit consistent by construction (the invariant ADR-0014/ADR-0028 already rely on). The legend
   (which draws only through `phaseColor`) tracks the selected map automatically.

5. **Selection surface.** `PlotOptions::colormapMode` / `RenderParams::colormapMode` are unchanged in
   type; they now accept `0..3`. The viewer's `C` key **cycles** `colormapMode = (colormapMode + 1) %
   kColormapModeCount` (was `^= 1`). Out-of-range modes clamp to `0` (twilight) on both host and GPU.

## Contract Specification
- `colormapMode ∈ {0 twilight, 1 HSV, 2 infinity, 3 grayscale}`. `0/2/3` are 256-entry cyclic LUTs
  (linear, REPEAT, texel-center sampling); `1` is analytic HSV. Out-of-range ⇒ mode `0`.
- Every LUT map is cyclic: `phaseColor(−π) == phaseColor(+π)`; entry 255 wraps to entry 0.
- Output is `rgb ∈ [0,1]³`, non-premultiplied (ADR-0012 multiplies by `α`).
- The committed LUTs are reproducible from documented sources: matplotlib `twilight`; the committed
  `tools/colormaps/*.csv`. `gen_colormap.py --check` must pass against the committed header.
- Host `iv::phaseColor` and the GPU `uColormap` sample **identical** committed data for every LUT mode.

## Consequences
- Adds two publication/CVD-useful cyclic maps without disturbing the default or the HSV escape hatch.
- One extra GPU image layer per map (tiny), one binding change (`sampler1D` → `sampler1DArray`), and a
  pipeline/descriptor update; `GlyphVertex`/overlay paths untouched.
- The `kColormapLuts` table centralizes committed map data; adding a further CSV later is generator +
  data only (the mode/layer plumbing then already generalizes) — a smooth path for the rest of B-0005.
- `C` now round-robins modes; any code assuming a 0/1 toggle must use `kColormapModeCount`.

## Alternatives Considered
- **Multiple separate `sampler1D` bindings** (one per map): rejected — doesn't scale, more descriptors,
  branchy shader.
- **One wide concatenated `sampler1D`** (N·256 texels, offset per mode): rejected — REPEAT would wrap
  across map boundaries and linear filtering would bleed between maps at the joins.
- **2-D texture (256 × N) with normalized layer coord:** rejected — needs CLAMP + nearest on V to avoid
  inter-map blending; `sampler1DArray` expresses "integral layer, REPEAT on t" directly.
- **Bake HSV into the table too** (uniform treatment): rejected — analytic HSV is exact and data-free;
  keep it as mode 1.
- **Bake the CSV rows verbatim** (256 rows → 256 entries, duplicated seam): rejected as the default —
  the duplicated endpoint wastes an entry and makes a one-texel flat spot at the seam; resampling to
  texel centers keeps the map seam-uniform and consistent with twilight. (Trivial to switch if the
  verbatim rows are preferred.)
- **Caller-supplied runtime LUTs** (generic B-0005): deferred — this ADR lands the maintainer's specific
  committed maps; a runtime-LUT API is a separate, larger surface.

## Verification (teeth)
- **Host↔GPU cross-check extended:** the existing `[vk][renderer]` "host `phaseColor` matches the GPU
  colormap" test runs for **each** LUT mode `0/2/3` (render/sample a known phase; compare to host
  `phaseColor`) — a per-mode red if either side drifts. (Fault-inject: perturb one LUT layer → red.)
- **Selector:** modes `0/1/2/3` yield different colours for a fixed known phase (the selector routes).
- **Cyclic/seam:** for each new map, `phaseColor(−π) == phaseColor(+π)`; a non-wrapping normalization
  breaks it → red.
- **LUT anchors:** committed-table anchors — e.g. grayscale mid (`θ = 0 ⇒ t = 0.5`) ≈ white and its
  ends ≈ black; infinity's seam colour matches the committed first/last row — asserted against the
  header (guards a bad regeneration).
- **Generator `--check`:** regenerate the header from the sources and diff against the committed file
  (CI-style), so the committed LUTs can't silently drift from `tools/colormaps/*.csv`.
- **End-to-end:** a uniform-phase field rendered in each new mode shows that map's expected colour at
  `θ = 0` (ties to ADR-0012), and the legend swatch shows the same map across its phase axis.

## Open questions for the maintainer (redline before Accept)
- **Mode order / cycle:** `0 twilight → 1 HSV → 2 infinity → 3 grayscale` (the `C` cycle order). OK, or
  reorder (e.g. group the LUTs, HSV last)?
- **`grayscale` as a phase map:** confirm it's intended for the cyclic *phase* axis (unusual but valid —
  a colourless/CVD-safe phase rendering), not the magnitude axis.
- **Seam sampling:** resample each CSV to texel centers (proposed, seam-uniform) vs. bake the 256 rows
  verbatim (matches the CSV exactly, one-texel seam flat). Either is a one-line generator choice.
