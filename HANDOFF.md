# HANDOFF.md — imaginaryVolumes

**Last updated:** 2026-07-03 by the visual-polish session (Claude / Opus 4.8)
**Active milestone:** **M1–M9 all COMPLETE & locked.** Now in the **deferred visual-polish pass**
(no new milestone yet — these are journaled presentation refinements, not contract changes; a new
milestone, if any, starts at ORIENT → CONTRACT). M9 delivered inline LaTeX-subset math in labels
via an owned subset parser + OpenType-MATH box layout over `hb_ot_math_*` (no TeX engine; D-0048):
**ADR-0032 (mixed-font substrate)** + **ADR-0033 (inline `$…$` math)**, both Accepted. **Latest
work (2026-07-03): B-0017 (two additive cyclic colormaps) COMPLETE** — `colormapMode 2 = infinity`,
`3 = grayscale` alongside twilight (0) / HSV (1), via a layered LUT (`sampler1DArray`) baked from
`tools/colormaps/*.csv` by `gen_colormap.py`; host `iv::phaseColor` mirrors per layer, the viewer `C`
key cycles all four — **ADR-0036**/**D-0053**. Earlier today **B-0016** (legend magnitude axis in
scientific notation: decade ticks → `$10^{e}$`, small/large linear → shared-exponent `$m×10^{exp}$`,
content-aware label reserve) shipped as **D-0052** (no ADR). Prior visual polish **B-0013** complete
(D-0049; ADR-0034/D-0050; ADR-0035/D-0051).

## Current State
The library does the full job end to end: ingest a complex field (`std::complex<float|
double>` + `GridDims`, x-fastest) → a 3D RG32F texture → a compute ray-march (abs→opacity
ADR-0013/0020/0027, arg→colormap ADR-0014) → offscreen readback **and** an interactive GLFW
viewer, with a perf contract (15.3 ms / ~65 FPS @ 512³→720p on the RTX 4070). Over that:
crisp Unicode text (vendored HarfBuzz + Slug GPU glyphs, M6), a world-space bounding box +
ticked, labeled axes (M7), a **phase × magnitude legend** (M8), and a **one-call facade**
(M8), and **inline LaTeX math in labels** (M9). All M1–M9 complete & locked (MILESTONES.md;
CHANGELOG.md).

**M9 (mathematical typesetting in labels) — Complete & locked** (CHANGELOG § M9). Two ADRs:
- **ADR-0032** (63a2a2a) — **mixed-font substrate**. Multiple faces in one overlay via a single
  MERGED Slug atlas with rebased `glyphLoc` (renderer/`GlyphVertex` unchanged). New: bundled GFL
  `NewCM10-BookItalic` + `NewCMMath-Book` (`bundledFontItalic()`/`bundledFontMath()`),
  `iv::text::FontSet` (roman/italic/math Shapers), `iv::text::MixedGlyphs` (build → `finish()`).
  Resolves **B-0012**.
- **ADR-0033** (7ce38be → stage-4) — **inline `$…$` math**. `iv::text::math` parser (`splitLabel`/
  `parse`) + OpenType-MATH box layout (`math_layout`) over a Shaper math API (`mathConstant`,
  `glyphVariant`, `mathItalicCorrection`, `mathTopAccentAttachment`, …) — all metrics from the
  font, no hardcoded TeX constants. Subset: scripts, `\frac`, `\sqrt[n]`, `\hat`/`\dot`/`\overline`,
  stretchy `\left…\right` + bra–ket, Greek/symbol/operator macros, `\mathrm`/`\mathbf`/`\mathit`,
  spacing. `appendLabel`/`measureLabel` bridge labels in; `buildAnnotations`/`buildLegend` take a
  `FontSet`+shared `MixedGlyphs`. Legend `fieldName` default → `"$f$"` (math italic; amends
  ADR-0031). Resolves **B-0015**; follow-on B-0016 (legend sci-notation).

Decisions D-0001…D-0053; Backlog **open:** B-0005 (more colormaps — *generic caller-supplied* maps),
B-0006 (VMA), B-0009 (LICENSE); B-0014 (legend `L` in data units) **deferred indefinitely**
(maintainer, 2026-07-03); B-0007/0008/0010/0011/0012/0013/0015/0016/**0017** resolved (**B-0017
colormaps complete** — ADR-0036/D-0053; **B-0016 sci-notation** — D-0052). ADR index current
(**ADR-0001…0036 Accepted**; 0009 superseded by 0015; 0034 amends 0028/0031; 0035 amends 0012; **0036
amends 0014** — additive cyclic colormaps). Gates: full suite **1498/111**; ASan+UBSan `ctest`
**2/2**; GLFW-free (renderPlot present, makePlot absent) builds; text-free (transfer core, no
HarfBuzz) builds; no HarfBuzz type in any public header; `iv_view` (via makePlot) present-path
validation-CLEAN; `tools/gen_colormap.py --check` clean.

## Test data (gitignored)
`example_data/` (NOT tracked — `.gitignore`: `/example_data/`, `*.c64`): 11 heavy raw
**complex64** volumes from QM wavefunction-scattering models (`wf1.c64`…`wf11.c64`, 27 MB
each). Each is **150³**, x-fastest, so: `iv_view --input example_data/wf1.c64 --dims 150 150
150` (add `--decades`/`--density` to taste). Good real datasets for eyeballing the legend.

## In Flight (work started, not finished)
**Nothing in flight.** **B-0017 (additive cyclic colormaps `infinity`/`grayscale`) is RESOLVED**
(ADR-0036/D-0053), and **B-0016** (D-0052) + **B-0013** before it. Full suite **1498/111**; ASan+UBSan
**2/2**; text-free + GLFW-free OK; boundary clean; viewer present-path validation-CLEAN;
`gen_colormap.py --check` clean. Tree not committed (maintainer commits when he chooses).

## Next Action
B-0016 (sci-notation) and B-0017 (colormaps) are done. Remaining open backlog (no item in progress):
- **B-0005 — generic/caller-supplied colormaps.** Now the *generalization* of B-0017: a runtime API
  for caller-supplied LUTs (vs the committed maps ADR-0036 added). Would extend ADR-0014/0036; adding
  another *committed* cyclic map is already generator + data only (`tools/colormaps/<name>.csv` + a
  `LAYERS` entry in `gen_colormap.py`, then extend `kColormapModeCount`'s consumers). Needs an ADR if
  it changes the public colormap surface (runtime LUTs).
- **B-0009** declare a top-level project **LICENSE** (administrative; see VENDORING.md for the
  third-party license set — HarfBuzz/Slug, the GFL NewCM fonts).
- **B-0006** VMA (deferred, D-0017); **B-0014** legend `L` in data units — **DEFERRED INDEFINITELY**.
To eyeball a colormap: the scratch pattern (link `iv_text`, `iv::renderPlot` with
`opts.colormapMode = 0..3`, write a PNG via `examples/png.hpp`), or `DISPLAY=:1 ./build/debug/iv_view`
and press `C` to cycle twilight → HSV → infinity → grayscale. To eyeball the legend sci-notation: same
scratch pattern with `opts.opacityMode=1` (log decade ticks) or a tiny `opts.magnitudeRange` (linear
sci ticks), rendered tall/square/wide to exercise placement.
To eyeball: build `renderPlot` into a tiny driver (the scratch pattern: link `iv_text`, call
`iv::renderPlot` with labeled `PlotOptions.axes`, write a PNG via `examples/png.hpp` — render at a
TALL aspect, e.g. 600×1000, to exercise legend placement), or `DISPLAY=:1 ./build/debug/iv_view
--input example_data/wf1.c64 --dims 150 150 150 --ylabel y --yunit nm` (interactive).

Eyeball a math plot: build a quick `renderPlot` driver, or `DISPLAY=:1 ./build/debug/iv_view
--input example_data/wf1.c64 --dims 150 150 150` (its legend field name is now the math-italic `$f$`).

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
  ADR-0028) — `phaseColor` (LUT modes 0/2/3 sample the committed `kColormapLuts[layer]` with
  linear+repeat, *same data as the GPU*; mode 1 = the analytic HSV) and
  `transferNormalized`/`transferOpacity` (mirror `ray_march.comp::sampleOpacity`, **pre** the
  ADR-0020 dt-correction). They must stay in lock-step with the shader: the `[vk][renderer]` "host
  phaseColor matches the GPU colormap" cross-check (now over EVERY mode) is the guard. Don't
  "optimize" one side without the other.
- **Colormaps are a layered LUT (ADR-0014/0036).** `colormapMode`: **0 twilight, 1 HSV (analytic),
  2 infinity, 3 grayscale**; `kColormapModeCount = 4`. The LUT maps live in `colormap_lut.hpp` as a
  generated table `kColormapLuts[kColormapLutCount][256*4]` (+ `kColormapLutNames`), one layer per
  map in mode order *skipping HSV* (layer 0 twilight → mode 0, 1 infinity → mode 2, 2 grayscale →
  mode 3). The GPU binds a **`sampler1DArray`** (binding 3); both the shader (`colormapLayer`) and
  host (`colormapLutLayer`) map `mode → layer` as `mode==0?0:mode-1` (out-of-range → twilight) — keep
  them IDENTICAL. Adding another committed cyclic map = drop `tools/colormaps/<name>RGBAcolormap.csv`,
  add its name to `LAYERS` in `gen_colormap.py`, regenerate (`gen_colormap.py`; `--check` gates
  staleness), done — the mode/layer plumbing generalizes. The `C` key cycles `(mode+1) %
  kColormapModeCount`.
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
  Captions derive from `LegendSpec::fieldName` (default now **`"$f$"`** → `|𝑓|` / `arg(𝑓)`, via
  `magnitudeCaption()`/`phaseCaption()`; `magnitudeLabel`/`phaseLabel` override — ADR-0031) and,
  like every label, may carry inline `$…$` math, typeset by the M9 math layer (ADR-0033;
  **B-0012 resolved** — the italic + NewCMMath faces are bundled). See the "Inline math" landmine.
  **Legend placement & magnitude caption (ADR-0034, D-0050; refined by D-0052/B-0016):** the swatch
  position is NOT the fixed `rectNdc` default — the facade calls
  `iv::text::placeLegendRight(spec, camera, fbW, fbH, valueReservePx)` (projects the box, positions the
  swatch aspect-aware) before `buildLegend`, per-frame in `makePlot` (live camera) and once in
  `renderPlot`. It preserves the swatch width/vertical extent. **Placement priority (D-0052):** labels
  win over full box clearance — when a value label fits at/right-of the default the swatch sits at the
  default or is pushed right to clear the box (capped so labels stay on screen); when it does NOT fit
  at the default (narrow window OR a wide B-0016 sci label) the swatch **slides left toward `hi`** to
  keep the widest label on screen, but **never left of `boxClear`** (never INTO the box); if the box
  fills a portrait frame so clearing it and fitting the label conflict, clearing the box wins and the
  value labels clip at the right edge. **`valueReservePx` is measured, not fixed** — the facade passes
  `magnitudeValueReservePx(spec, glyphs)` (widest actual value label at the tick size + stubs), because
  the B-0016 sci labels vary a lot in width (a decade `10⁻⁴` is narrow, a linear `2.5×10⁻³` is wide); a
  fixed reserve clipped the exponent. `buildLegend` and the reserve share one `magnitudeTicks(spec)`
  generator. **Value-label format (B-0016/D-0052):** magnitude ticks are inline-math — log decade →
  `$10^{e}$`; linear → plain decimals except very small/large ranges (`|max|<1e-2` or `>=1e4`) →
  shared-exponent `$m\times10^{exp}$`; zero → `0`. Public formatters `decadeTickLabel`/
  `linearTickLabel`. The **magnitude** caption is drawn rotated −π/2, vertically centered LEFT of the
  swatch (`appendLabelRotated`), NOT centered above. **Label sizes:** caption-class labels (−π/0/π
  phase labels, captions, `L`) render at `labelSize = baseSize · kLegendLabelScale` (1.3, ~the plot's
  axis-label size); the **magnitude value ticks** stay at `baseSize` (= the volume's axis tick values).
  `centeredLabel`/`leftLabel` take an explicit size; vertical spacing is in `labelSize` units. The
  `placeLegendRight` px tunables (`kBoxGapPx`/`kCapAllowPx`/`kTickReservePx`/`kRightEdgeNdc`, + the
  46 px default reserve arg) live in legend_builder.cpp.
  **Plot-left framing (ADR-0035, D-0051):** when a legend is shown the facade frames the plot into
  the LEFT 75% via `RenderParams::imageShiftNdcX = −0.25` + a larger distance (`kPlotFrameDistance
  Legend = 4.0`); the legend takes the right 25%. `imageShiftNdcX` (default 0, render-inert) is a
  horizontal NDC image shift applied IDENTICALLY in two host spots that MUST stay in step:
  `fillUbo` (renderer.cpp: `topLeft −= shift·halfW·u`, the ray camera) and `viewProjection`
  (`row0 += shift·row3`, the overlay) — change one, change the other (the `[viewproj]` collinearity
  test guards it). For makePlot, the OrbitCamera sets eye/target/up each frame but leaves
  `imageShiftNdcX`, so it's set ONCE and persists across orbit. The non-projected **title** carries
  the shift itself (`buildAnnotations`: `cx = fbW·0.5·(1+imageShiftNdcX)`) to stay over the plot;
  projected labels (ticks/axis) shift automatically via `viewProjection`. The title's **vertical**
  position tracks the box too: it tucks just above the box's projected top (min projected-y over the
  cube corners) − `0.5·titleSize − kTitleGapPx`, clamped to the frame top margin — so it sits close
  to the box instead of floating in the zoom-out gap (a `[annot]` test pins this).
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
  **Axis-label placement (D-0049, B-0013):** the axis-label outward offset is NOT a fixed margin —
  it's `iv::text::axisLabelOutwardPx(n, …)` (annotations.hpp), sized so the label clears the actual
  tick-label band along the screen-outward normal (tick *width* dominates a vertical edge, *height*
  a horizontal one). Keep `kLabelCapHalf` in lock-step between the offset math and
  `addCenteredLabel`'s vertical-centering nudge (both use it as the label's half cap-height). The
  `[annot]` "clears the tick-label band" test pins this. **Facade framing:** `renderPlot`
  (plot_render.cpp) and `makePlot` (plot_make.cpp) frame the cube at orbit distance
  `kPlotFrameDistance = 3.3` (duplicated in both TUs, intentionally matched) so wide labels are not
  clipped — facade-local; the global `RenderParams`/`OrbitCamera` defaults (and reset/`R`) are
  unchanged. If you retune one facade's distance, retune the other to match.
- **Mixed-font substrate (ADR-0032, M9):** multiple faces in one overlay go through
  `iv::text::MixedGlyphs` (build with `appendRun(Face,…)` / `appendGlyph(Face,glyphId,…)`, then
  ONE `finish(overlay)`), NOT repeated `appendText`. `finish()` concatenates each used face's Slug
  atlas onto `overlay.glyphAtlas` and **rebases** that face's `glyphLoc` by the running texel base —
  so the renderer/pipeline/`GlyphVertex` are unchanged (one atlas, one draw). Roman is face 0 at
  base 0, so a roman-only build is byte-identical to `appendText` (a `[text]` test pins this — keep
  it green when migrating `buildAnnotations`/`buildLegend` to `MixedGlyphs`). DON'T mix
  `appendText`(roman) and `MixedGlyphs`(roman) on ONE overlay (double-stores roman's atlas). Faces:
  `FontSet::create(px)` → roman/italic/math (`bundledFont()`/`Italic()`/`Math()`); the math face
  carries the OpenType MATH table for ADR-0033.
- **Inline math (ADR-0033):** every caller label is parsed as text + `$…$` math
  (`iv::text::math::splitLabel`/`parse` → a box tree; `math::layout` → glyphs via `MixedGlyphs` +
  rules via `overlay.screenTriangles`). `appendLabel`/`measureLabel` (math_layout) are the bridge;
  `buildAnnotations`/`buildLegend` take a `FontSet`+shared `MixedGlyphs` and the caller calls
  `glyphs.finish(ov)` ONCE (one merged atlas). **All math metrics come from the font's MATH table**
  via the Shaper math API (`mathConstant`, `glyphVariant`, `mathItalicCorrection`,
  `mathTopAccentAttachment`, …) — NEVER hardcode a TeX constant (a `[math]` test pins the fraction
  rule to the font axis; another pins variant stretch). Box coords are baseline-relative **+y up**;
  the public `layout()`/`appendLabel()` flip to the top-left-origin framebuffer at emit. A `$`-free
  label routes to `appendRun(Roman)` and is byte-identical to the old path (a `[math]` test pins
  it). Subset extension point: the macro table in `math_parse.cpp` (`symbolTable()`). The legend
  `fieldName` default is `"$f$"` (math italic); the caption tests expect `"|$f$|"`/`"arg($f$)"`.
  **Rotated labels (ADR-0034):** `math::appendLabelRotated` lays a label out horizontally then calls
  `MixedGlyphs::rotateSince` to rotate its glyph quads in PIXEL space (keeps `texcoord` fixed → Slug
  outline unchanged, only placement rotates). It rotates **glyph quads only** — inline-math RULES
  (`\frac`/`\overline` bars → `overlay.screenTriangles`) are NOT rotated, so a rotated label must not
  contain them (the legend captions are bars + a symbol; fine).
- **`glslc` is a required build tool** (ADR-0011/D-0022). Shaders in `shaders/`; SPIR-V
  embedded (regenerated each build). The colormap LUTs are committed data
  (`include/iv/vk/colormap_lut.hpp`, the `kColormapLuts` table) — regenerate with
  `tools/gen_colormap.py` if a source (matplotlib twilight or a `tools/colormaps/*.csv`) changes;
  `--check` gates staleness. Shared by the GPU sampler AND `iv::phaseColor`, so a change moves both.
  See the layered-LUT landmine above (ADR-0036).
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
  N`; **label flags (M9)** `--title STR`, `--field STR`, `--{x,y,z}label STR`, `--{x,y,z}unit
  STR` — any string may carry inline `$…$` math, e.g. `--field '$\psi$' --title '$E=mc^2$'`.
  Keys: drag orbit, scroll zoom, `L` lin/log, `C` colormap, `R` reset, `↑/↓` density,
  `←/→` decade window, `[`/`]` legend thickness, `Esc` quit.
- High-level API: `iv::makePlot(field, dims, opts)` → configured `Viewer` (call `->run()`);
  `iv::renderPlot(field, dims, w, h, opts)` → labeled `ImageReadback` (headless).
- Benchmark (Release): `cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release` →
  `./build/release/iv_bench`. ADR index: `python3 tools/regenerate_adr_index.py [--check]`.
  `IV_VULKAN_DEVICE_INDEX` forces a device.

## Pointers
- Governing process: `DEV_PROCESS.md`. Milestone arc: `MILESTONES.md` (M1–M9 complete & locked).
- Contracts: `docs/adr/INDEX.md` — **ADR-0001…0036 Accepted** (0009 superseded by 0015;
  0020/0027 extend 0013; 0028 extends 0021; 0030 extends 0020; 0031 amends 0028/0029; **0034 amends
  0028 §(3)/0031** — legend placement + rotated magnitude caption; **0035 amends 0012** — the
  render-inert horizontal image shift that frames the plot left for the legend; **0036 amends 0014** —
  additive cyclic colormaps `infinity`/`grayscale` via a layered LUT).
- Decisions & rationale: `DECISIONS.md` (D-0001…D-0053), Backlog B-0001…B-0017.
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
