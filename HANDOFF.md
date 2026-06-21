# HANDOFF.md — imaginaryVolumes

**Last updated:** 2026-06-20 by the M8 session (Claude / Opus 4.8)
**Active milestone:** **M8 Complete & locked.** With it the founding milestone arc
**M1–M8 is complete** — the "usable scientific plotting library" goal is met. Next work is
a maintainer-driven **legend visual-polish pass** and the open Backlog; a new milestone (if
any) starts at ORIENT → CONTRACT.

## Current State
The library does the full job end to end: ingest a complex field (`std::complex<float|
double>` + `GridDims`, x-fastest) → a 3D RG32F texture → a compute ray-march (abs→opacity
ADR-0013/0020/0027, arg→colormap ADR-0014) → offscreen readback **and** an interactive GLFW
viewer, with a perf contract (15.3 ms / ~65 FPS @ 512³→720p on the RTX 4070). Over that:
crisp Unicode text (vendored HarfBuzz + Slug GPU glyphs, M6), a world-space bounding box +
ticked, labeled axes (M7), a **phase × magnitude legend** (M8), and a **one-call facade**
(M8). All M1–M8 complete & locked (MILESTONES.md; CHANGELOG.md).

**M8 (legend/colorbar & high-level plot API) — Complete & locked** (CHANGELOG § M8). Two ADRs:
- **ADR-0028** (84c5950) — **legend**. Pure-host transfer evaluators (`include/iv/transfer.hpp`:
  `phaseColor`, `transferNormalized`, `transferOpacity`) mirror the shader's arg→color /
  abs→opacity and are the single source the legend draws through (mode-0 colormap shares the
  committed `kTwilightLut` with the GPU). `Overlay` gained screen-space `screenLines/
  screenTriangles` (identity transform, Vulkan clip y-down — **D-0044**), drawn in both paths.
  `iv::LegendSpec` (core `iv`) + `iv::text::buildLegend` (`iv_text`) draw a **2-D swatch**
  (phase across, opacity up) + border + −π/0/π phase ticks + nice-number magnitude ticks +
  labels; it **appends** so it composes with `buildAnnotations`.
- **ADR-0029** (64e8c2d) — **facade**. `iv::PlotOptions` (single source of transfer state);
  `iv::renderPlot` (headless → labeled image; in `iv_text`) and `iv::makePlot` (returns a
  configured, not-yet-running `Viewer`; new **`iv_plot`** target, needs viewer + text). `iv_view`
  now builds its labeled plot via `makePlot`; the per-frame closure rebuilds box/axes + legend
  from the LIVE params so hotkeys update the legend. Decisions D-0042…D-0044.

**Post-M8 legend polish** (CHANGELOG § Post-M8): truly-transparent swatch (D-0045), per-frame
overlay reset (`Overlay::clear()` — was accumulating), log **decade ticks** (B-0011), and
**thickness-corrected opacity** (ADR-0030; `iv::accumulatedOpacity(a, L)` + the `[`/`]` thickness
knob with an on-legend `L = …` label). Default reference thickness `L = 0.1` (soft; tunable).

Decisions D-0001…D-0046; Backlog B-0005 (more colormaps), B-0006 (VMA), B-0009 (LICENSE) open;
B-0007/0008/0010/0011 resolved. ADR index current
(**ADR-0001…0030 Accepted**; 0009 superseded by 0015). Gates: full suite **768/79**;
ASan+UBSan `ctest` green; GLFW-free (renderPlot present, makePlot absent) **768/79**;
text-free (transfer core, no HarfBuzz) **580/59**; no HarfBuzz type in any public header;
`iv_view` (via makePlot) validation-CLEAN on the vortex and a 150³ dataset.

## Test data (gitignored)
`example_data/` (NOT tracked — `.gitignore`: `/example_data/`, `*.c64`): 11 heavy raw
**complex64** volumes from QM wavefunction-scattering models (`wf1.c64`…`wf11.c64`, 27 MB
each). Each is **150³**, x-fastest, so: `iv_view --input example_data/wf1.c64 --dims 150 150
150` (add `--decades`/`--density` to taste). Good real datasets for eyeballing the legend.

## In Flight (work started, not finished)
**Nothing in flight.** M8 is closed; the working tree is clean at the M8 commits.

## Next Action
The founding arc is done. The most immediate likely next step is the maintainer's deferred
**legend visual-polish pass** — appearance/placement of the 2-D swatch (the default panel is
`LegendSpec::rectNdc = {0.60,−0.45,0.84,0.45}`; tune sizes/margins). Other open work:
**B-0005** (more colormaps),
**B-0009** (declare a project LICENSE), and **LaTeX math** labels (deferred since M6). Any of
these touching a public contract starts at ORIENT → CONTRACT with a new ADR (Proposed →
Accepted) before code. The legend's appearance is pure presentation — eyeball it with
`DISPLAY=:1 ./build/debug/iv_view --input example_data/wf1.c64 --dims 150 150 150`.

## Known-Broken / Blocked
- **Nothing broken.** The tree builds and all gates pass.
- VMA still deferred (D-0017); B-0006 open — revisit if memory management grows.
- `clearAndReadback` (M2) still has its own inline submit/barriers rather than the shared
  `commands.hpp` helpers — fold in if touched (not urgent).
- A separate present queue is **not** supported (ADR-0016); deferred to Backlog.

## Landmines & Context
- ORIENT before writing (§3.0): this file → DEV_PROCESS → MILESTONES → ADR INDEX →
  DECISIONS. Restate governing ADRs before coding; ADRs are append-only and Accepted ones
  immutable — record deviations in DECISIONS.md + CHANGELOG (as D-0044 corrected ADR-0028's
  y-up→y-down coordinate note without superseding).
- **The legend draws ONLY through the host transfer evaluators** (`iv/transfer.hpp`,
  ADR-0028) — `phaseColor` (mode 0 = the committed `kTwilightLut` with linear+repeat, *same
  data as the GPU*; mode 1 = the analytic HSV) and `transferNormalized`/`transferOpacity`
  (mirror `ray_march.comp::sampleOpacity`, **pre** the ADR-0020 dt-correction). They must stay
  in lock-step with the shader: the `[vk][renderer]` "host phaseColor matches the GPU
  colormap" cross-check is the guard. Don't "optimize" one side without the other.
- **Screen-space overlay channels** (`Overlay::screenLines/screenTriangles`, ADR-0028) are
  **Vulkan clip space, y-down** (y=−1 top), drawn with the identity transform after the
  world-space `lines/triangles` (which use `Overlay::transform` = the view-projection) and
  before glyphs. The legend's "magnitude up" maps normalized position v=1 → the top (smaller
  y). `LegendSpec::rectNdc = {left, top, right, bottom}` (top < bottom). The overlay vertex
  buffer packs **lines, triangles, screenLines, screenTriangles** in that order (both paths).
  The swatch has **no opaque backing** — it composites over the scene with real alpha so it
  truly reflects transparency and matches the plot saturation (D-0045; don't re-add a backing).
  Log mode uses **decade ticks** (powers of 10) so the legend tracks the decade window (B-0011);
  linear mode uses `ticksFor`. The swatch opacity is **thickness-corrected** —
  `accumulatedOpacity(a, L) = 1−(1−a)^(256·L)` (ADR-0030; the ADR-0020 accumulation over
  `LegendSpec::referenceThickness`), so it matches the volume's accumulation (`L=0` =
  uncorrected). The viewer drives `L` via the **render-inert** `RenderParams::legendThickness` +
  `[`/`]` (a `[vk][renderer]` test pins that the ray-march ignores it). The thickness label uses
  plain **`L`** — NCM-Book lacks U+2113 `ℓ` (a `[legend]` test pins this; don't restore `ℓ`).
- **Facade targets / isolation** (ADR-0029): `renderPlot` is in **`iv_text`** (needs text,
  not GLFW); `makePlot` is in **`iv_plot`** (gated on `IV_BUILD_VIEWER AND IV_BUILD_TEXT`).
  Core `iv` / tests / `iv_bench` still build with both gates OFF (the isolation gates). The
  returned `Viewer` stays text-agnostic: the per-frame legend/annotation closure owns its
  `Shaper`+models via a `shared_ptr` (so the lambda is copyable for `std::function`).
  `PlotOptions` holds the transfer state ONCE and fans it to both RenderParams and LegendSpec.
- **Annotations (ADR-0024/0026):** `iv::vk::viewProjection` must stay consistent with the
  ADR-0012 ray camera (the `[viewproj]` collinearity test is the guard — don't drop the
  y-down/top-left flip). `buildAnnotations` **resets ALL overlay channels** (via
  `Overlay::clear()` — incl. the screen-space legend ones; it is the **per-frame reset** for a
  reused overlay, or the screen channels accumulate every frame) + sets `transform`; a caller
  composing a legend calls it FIRST, then `buildLegend` (which appends, sharing one `Shaper` so
  glyph atlas offsets stay valid). A new overlay channel MUST be added to `Overlay::clear()`.
  Labels sit on the box silhouette offset outward — the outward offset guards no-data-overlap.
- **`glslc` is a required build tool** (ADR-0011/D-0022). Shaders in `shaders/`; SPIR-V
  embedded (regenerated each build). The colormap LUT is committed data
  (`include/iv/vk/colormap_lut.hpp`) — regenerate with `tools/gen_colormap.py` if it changes;
  it is shared by the GPU sampler AND `iv::phaseColor`, so a change moves both.
- **Viewer is GLFW-coupled & isolated** (ADR-0016): only `iv_viewer`/`iv_view`/`iv_plot` link
  glfw. **Text is HarfBuzz-coupled & isolated** (ADR-0022): only `iv_text` (and `iv_plot` via
  it) link vendored HarfBuzz; **no HB type in any `include/` header** (grep-able gate).
  HarfBuzz is vendored (`third_party/harfbuzz/`, pin in VENDORING.md), built as the
  amalgamated TU with no `HAVE_*` defines.
- **Opacity correction (ADR-0020):** `α = 1 − (1−a)^(dt·256)` is a ray-integration effect, NOT
  part of the per-sample transfer — the legend depicts the authored `transferOpacity` curve,
  not the dt-corrected α. Don't conflate them.
- **Present correctness:** swapchain image must be `ePresentSrcKHR` at present (the teeth);
  `renderFinished` is per swapchain image; blit is component-aware (RGBA8 render → BGRA8
  swapchain). Viewer lifetime is pimpl-ordered (member declaration order is load-bearing).
- **Coordinate/value conventions:** RH, +Y up, volume `[0,1]³`, world position = texcoord,
  image origin top-left (ADR-0012/0023). Volume stores `(Re, Im)` not `(magnitude, phase)`
  (ADR-0015) — never interpolate the phase angle (a seam test guards this). Classic 1.0
  barriers (D-0016). `R32G32_SFLOAT` linear filtering isn't core-mandatory (nearest fallback).
- LSan scoped off for Vulkan-inclusive runs (D-0015); the validation layer is the
  Vulkan-object-leak gate. Rejection tests use the short-circuiting `rejected()` helper.

## Commands
- Build/test (Debug): `cmake --build build/debug` then
  `ASAN_OPTIONS=detect_leaks=0 ./build/debug/tests/iv_tests` (filter e.g. `"[transfer]"`,
  `"[legend]"`, `"[plot]"`, `"[renderer]"`).
- Sanitizer gate: `cmake -S . -B build/asan -DIV_SANITIZE=address,undefined` then
  `ctest --test-dir build/asan --output-on-failure`.
- GLFW-free check: `cmake -S . -B build/noviewer -DIV_BUILD_VIEWER=OFF` then build (renderPlot
  present, makePlot/iv_plot absent). Text-free check: `cmake -S . -B build/notext
  -DIV_BUILD_TEXT=OFF` then build (no HarfBuzz; text/legend/plot tests excluded; `[transfer]`
  core stays).
- Viewer (needs a display; `DISPLAY=:1` here): `cmake --build build/debug --target iv_view`
  then `DISPLAY=:1 ./build/debug/iv_view` (interactive, via `makePlot`) or `… --frames N`.
  Flags: `--input FILE --dims NX NY NZ` (raw complex64, x-fastest), `--density D`, `--decades
  N`. Keys: drag orbit, scroll zoom, `L` lin/log, `C` colormap, `R` reset, `↑/↓` density,
  `←/→` decade window, `Esc` quit.
- High-level API: `iv::makePlot(field, dims, opts)` → configured `Viewer` (call `->run()`);
  `iv::renderPlot(field, dims, w, h, opts)` → labeled `ImageReadback` (headless).
- Benchmark (Release): `cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release` →
  `./build/release/iv_bench`. ADR index: `python3 tools/regenerate_adr_index.py [--check]`.
  `IV_VULKAN_DEVICE_INDEX` forces a device.

## Pointers
- Governing process: `DEV_PROCESS.md`. Milestone arc: `MILESTONES.md` (M1–M8 complete & locked).
- Contracts: `docs/adr/INDEX.md` — **ADR-0001…0030 Accepted** (0009 superseded by 0015;
  0020/0027 extend 0013; 0028 extends 0021; 0030 extends 0020).
- Decisions & rationale: `DECISIONS.md` (D-0001…D-0046), Backlog B-0001…B-0011.
- Work + teeth per milestone: `CHANGELOG.md` (incl. § M8 and the "Post-M7" section).
- Demos: `examples/iv_render_demo [out_dir]` (offscreen PNGs); `iv_view` (interactive, via
  makePlot); `iv_bench` (perf).
- Code: host model `include/iv/` (`volume.hpp`, `orbit_camera.hpp`, `plot_axes.hpp`,
  **`transfer.hpp`**, **`legend.hpp`**, **`plot.hpp`**), `src/` (`volume.cpp`, `plot_axes.cpp`,
  **`transfer.cpp`**, **`plot_render.cpp`**, **`plot_make.cpp`**); Vulkan `include/iv/vk/`,
  `src/vk/` (context, offscreen, volume, view_projection, renderer, viewer); text
  `include/iv/text/`, `src/text/` (shaper, bundled_font, text_layout, annotations,
  **`legend_builder.cpp`**); shaders `shaders/`; generated `colormap_lut.hpp`; vendored
  `third_party/harfbuzz/`, `third_party/fonts/`; tests `tests/test_*.cpp`.
