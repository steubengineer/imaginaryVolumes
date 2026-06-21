# CHANGELOG — imaginaryVolumes

Per ADR-0002, this changelog records each milestone's work, its governing ADRs,
and the **demonstrated teeth evidence** (red→green or fault injection) for its
tests. Newest milestone first.

## M9 — Mathematical Typesetting in Labels (in progress)

Inline LaTeX-subset math in labels (`"Wave $f(x)=\frac{1}{2}$"`), via an owned subset parser +
OpenType-MATH box layout over `hb_ot_math_*` (no TeX engine; D-0048). Two ADRs: ADR-0032 (the
mixed-font substrate) then ADR-0033 (the `$…$` model + subset + layout).

- **Mixed-font glyph substrate** (ADR-0032; resolves **B-0012**): multiple font faces — roman
  (`NewCM10-Book`), true italic (`NewCM10-BookItalic`), and the OpenType MATH face
  (`NewCMMath-Book`) — now coexist in one overlay via a **single merged Slug atlas** with rebased
  `glyphLoc`, leaving the renderer / pipeline / `GlyphVertex` **unchanged** (the merge lives in
  `iv::text`). New: the two GFL faces vendored + embedded (`bundledFontItalic()`/`bundledFontMath()`),
  an `iv::text::FontSet` (owns the three Shapers), and `iv::text::MixedGlyphs` (accumulate
  face-tagged glyph quads → `finish()` concatenates each used face's atlas + rebases). A face with
  no glyphs adds no atlas bytes; Roman is merged first (base 0), so roman-only output is byte-identical
  to the single-face `appendText`. **Teeth:** (1) FontSet loads three *genuinely distinct* faces —
  the italic `f` outline differs from roman's (catches the same font bundled twice); (2) **merge
  rebase** — the italic glyph's `glyphLoc` equals roman's texel count; *demonstrated red* by
  disabling the rebase (`+= base*0` → italic `glyphLoc` `0` vs expected `366` → fail), restored to
  green; (3) **backward compat** — a roman-only `MixedGlyphs` build is byte-identical (glyphs +
  atlas) to `appendText`; (4) two faces render from one merged overlay, both halves paint,
  validation-clean. Gates: full suite **1097/84**; ASan+UBSan **2/2**; text-free + GLFW-free builds
  OK; no HarfBuzz type in any public header.

## Post-M8 — Legend Fixes

Bug fixes and refinements to the M8 legend, after the maintainer eyeballed it in the viewer.

- **Caller-named legend field** (ADR-0031; D-0047): the legend captions derive from a `fieldName`
  (default `"f"`) — `|f|` / `arg(f)`, or `|Phi|` / `arg(Phi)` — distinct from the plot title.
  Additive: `LegendSpec`/`PlotOptions` keep `magnitudeLabel`/`phaseLabel` as overrides (now
  default empty); derivation lives on `LegendSpec::magnitudeCaption()`/`phaseCaption()`. The field
  renders **upright** — true CM italic is deferred (**B-0012**: needs mixed-font/multi-atlas glyph
  support; the overlay is single-atlas and NCM-Book has no italic). **Teeth:** string equality on
  the derived captions (`"|Phi|"`, `"arg(Phi)"`, override precedence, empty → empty).
- **Thickness-corrected opacity** (ADR-0030, extends ADR-0020; D-0046): the volume accumulates
  opacity along each ray, so the per-sample legend read far more transparent than the render. The
  swatch alpha is now `accumulatedOpacity(a, L) = 1 − (1−a)^(256·L)` — the ADR-0020 accumulation
  over a tunable reference thickness `L` (soft default `0.1`; `0` = the uncorrected per-sample
  legend). New host evaluator `iv::accumulatedOpacity` + `iv::kReferenceSteps`; new
  `LegendSpec::referenceThickness` / `PlotOptions::legendThickness` / a **render-inert**
  `RenderParams::legendThickness` (the viewer's live `[`/`]` channel); a legend `L = …` label
  (plain `L` — the bundled NCM-Book face lacks U+2113 `ℓ`). **Teeth:** `accumulatedOpacity`
  anchors + monotonicity; a low-alpha swatch is markedly more opaque with thickness than at `0`;
  and two renders differing only in `legendThickness` are pixel-identical (render-inert).
- **Truly transparent swatch** (D-0045): removed the opaque backing panel — the swatch now
  composites straight over the scene with its real alpha, matching the plot's saturation instead
  of desaturating partial-opacity cells toward a gray tone.
- **Per-frame overlay reset** (`Overlay::clear()`): `buildAnnotations` cleared the world channels
  but not the M8 screen-space ones, so the viewer's reused overlay accumulated a fresh legend
  swatch every frame (compounding opacity + unbounded memory growth). `buildAnnotations` now
  resets ALL channels. **Teeth:** rebuilding into one reused overlay twice keeps the
  screen-channel counts equal (was 18432 → 36864 → red). `renderPlot` (fresh overlay each call)
  was unaffected, which is why the headless tests missed it.
- **Decade ticks in log mode** (resolves B-0011): the legend's magnitude ticks were linear
  nice-numbers that barely moved as the decade window changed (loBound ≈ 0 for high-dynamic-range
  data), so the legend looked unresponsive to `--decades`. Log mode now uses decade ticks (powers
  of 10) over the active window, evenly spaced on the log bar and tracking it. **Teeth:** a wider
  decade window emits more tick lines + labels. Linear mode unchanged (`ticksFor`).
- Gates after: full suite **746/76**; ASan+UBSan green; `iv_view` (via makePlot) validation-CLEAN.

## M8 — Legend/Colorbar & High-Level Plot API

**Status:** Complete (2026-06-20).
**Governing ADRs:** ADR-0028 (legend for the phase × magnitude transfer function; extends
ADR-0021, mirrors ADR-0013/0014/0027), ADR-0029 (high-level `makePlot`/`renderPlot` facade).
Decisions D-0042…D-0044.

### Added
- **Host transfer evaluators** (ADR-0028; `include/iv/transfer.hpp`, `src/transfer.cpp`; pure
  host, core `iv`): `phaseColor(phase, colormapMode)` mirrors the shader's arg→color (ADR-0014;
  mode 0 samples the *same* committed `kTwilightLut` as the GPU, mode 1 the analytic HSV);
  `transferNormalized(m, range, opacityMode, logDecades)` → mn ∈ [0,1] and `transferOpacity(…,
  densityScale, …)` mirror the shader's abs→opacity (ADR-0013/0027, pre-dt-correction). The
  single source of truth any host depiction of the transfer function draws through.
- **Screen-space overlay channels** (ADR-0028, extends ADR-0021): `Overlay::screenLines` /
  `screenTriangles` (Vulkan clip y-down, identity transform; D-0044), drawn after the
  world-space geometry in both `render()` and `recordFrame()`, so a 2-D HUD (the legend) and
  the 3-D box/axes coexist in one Overlay.
- **Legend** (ADR-0028; `include/iv/legend.hpp` `iv::LegendSpec`, core `iv`;
  `iv::text::buildLegend`, `src/text/legend_builder.cpp`, `iv_text`): a 2-D swatch — phase
  (−π…+π) across, magnitude→opacity up — with a border, bottom phase ticks (−π, 0, π),
  nice-number magnitude ticks (right) placed by `transferNormalized`, and labels. Colors/alphas
  come only through the host evaluators, so the legend cannot drift from the render. Appends to
  the overlay (composes with `buildAnnotations`).
- **High-level facade** (ADR-0029; `include/iv/plot.hpp`): `iv::PlotOptions` holds the transfer
  state once, fanned out to both the RenderParams and the LegendSpec. `renderPlot` (headless →
  labeled `ImageReadback`; in `iv_text`, needs text not GLFW) and `makePlot` (returns a
  configured Viewer; new `iv_plot` target, needs viewer + text) installing a per-frame closure
  that rebuilds box/axes + legend from the LIVE params (so hotkeys update the legend). `iv_view`
  now builds its labeled plot via `makePlot`.

### Verification (teeth)
- **Transfer evaluators** (ADR-0028; `[transfer]`): colormap seam/periodicity, the
  committed-LUT anchor (θ=0 → lerp of twilight entries 127/128), HSV anchors, and every opacity
  branch (linear / log-full / decade-window / degenerate). **Teeth** — a wrong LUT texel offset
  (`+0.5` vs `−0.5`) reddens the committed-LUT anchor.
- **Legend ↔ shader colormap cross-check** (ADR-0028; `[vk][renderer]`): a saturated uniform
  field over black renders ≈ `phaseColor` for several phases in both colormap modes. **Teeth** —
  perturbing `phaseColor` / the LUT diverges the host prediction from the GPU render → red.
- **Legend render** (ADR-0028; `[legend]`, `[vk][legend]`): the swatch populates the screen
  channels + labels; the rendered swatch is opaque cyan at the top (high magnitude, θ=0) and
  dark at the bottom (transparent). **Teeth** — a constant-alpha swatch makes the bottom opaque,
  reddening the opacity-gradient checks (host `minA==0` and the GPU bottom-dark).
- **Facade** (ADR-0029; `[vk][plot]`): a `renderPlot` image differs from a bare-volume render by
  the overlay pixels (box/legend); the legend swatch and the volume show the same hue for the
  same phase (single source). **Teeth** — not building the overlay collapses the diff → red.
  `makePlot` is exercised by `iv_view --frames` (validation-CLEAN on the vortex and a 150³ set).
- **Gates:** full suite **738/74**; ASan+UBSan `ctest` green; GLFW-free (renderPlot present,
  makePlot absent) **738/74**; text-free (transfer core, no HarfBuzz) **565/57**; no HarfBuzz
  type in any public header; `iv_view` (via makePlot) validation-CLEAN.

## Post-M7 — Standalone Enhancements

Small, ADR-governed enhancements between milestones (not a milestone themselves).

- **Log-scale decade window** (ADR-0027, extends ADR-0013; D-0041): public
  `RenderParams::logDecades` (float, default 0). In log mode, `> 0` windows the opacity
  ramp to the top `logDecades` decades below `max`
  (`mn = clamp(1 + log10(m/max)/logDecades, 0, 1)`); `0` is the unchanged ADR-0013 map.
  Packed into a spare UBO slot (no layout change); live in the viewer (`--decades N`;
  `[`/`]` hotkeys). **Teeth:** a uniform field that log mode renders transparent
  (degenerate range, decades=0) becomes opaque at decades=4 (`m==max → mn=1`), and a
  wider window admits more of a graded field (brighter); disabling the ADR-0027 branch
  reverts both → red. Full suite **631/61**; ASan+UBSan green.
- **Smooth (anti-aliased) overlay lines** (D-0040): `Context` opportunistically enables
  `VK_EXT_line_rasterization` + `smoothLines`; the overlay line pipeline uses
  `eRectangularSmooth` (graceful fallback). Plus label polish: per-label sizes (title
  1.5×, axis 1.3× tick labels, via a size-independent atlas), larger tick-label margins,
  and a default of 1 minor tick between majors.
- **Viewer dataset loading & controls:** `iv_view --input FILE --dims NX NY NZ`
  (raw interleaved-`(re,im)` float32 / numpy complex64, x-fastest), `--density`, and the
  inverted mouse-drag orbit direction. Demo-only.

## M7 — Bounding Box, Ticked Axes & Labels

**Status:** Complete (2026-06-19).
**Governing ADRs:** ADR-0024 (plot coordinate model + declarative axis/label API +
nice-number tick generation), ADR-0025 (present-path / viewer glyph rendering;
resolves B-0010), ADR-0026 (world-space bounding box, ticked axes & labels). Decisions
D-0037…D-0039.

### Added
- **Declarative plot model** (ADR-0024; `include/iv/plot_axes.hpp`, `src/plot_axes.cpp`;
  pure host, core `iv`): `iv::PlotAxes { Axis x,y,z; title; visibility toggles;
  BoxTickStyle; vector<ThroughAxis> }`, per-axis `{min,max,label,unit, majorCount?,
  minorCount?}` in the caller's data units. `ticksFor` generates nice `{1,2,5}·10ᵏ`
  major + minor ticks (optional counts); `formatTick` formats major labels (value
  only; unit on the axis label). `world`/`physical` mapping, `dataCenter`, `axisFor`.
- **Present-path glyph rendering** (ADR-0025): `Renderer::recordFrame` now draws
  `Overlay::glyphs`, so the viewer shows text. Persistent glyph resources grown on
  demand (`ensureFrameGlyphResources`): the atlas buffer/view/descriptor rebuild only
  on growth, the vertex + atlas data re-upload each frame (D-0038). `drawOverlay` takes
  glyph handles so headless and present paths share one draw. Closes the
  ADR-0023/D-0036 headless-only scope.
- **World-space annotations** (ADR-0026): `iv::vk::viewProjection(camera, aspect)`
  (`include/iv/vk/view_projection.hpp`) — a world→clip matrix derived from the ADR-0012
  ray camera (a point projects to the pixel its ray passes through); `projectToPixel`.
  `iv::text::buildAnnotations` (`include/iv/text/annotations.hpp`,
  `src/text/annotations.cpp`) fills an `Overlay` from `PlotAxes` + camera: the 12-edge
  bounding box, tick marks (outer silhouette edges by exact face-facing test, or all
  faces), through-volume reference axes (data-unit), and screen-space labels placed on
  the silhouette edge whose outward normal is most "down-and-out", offset outward (so
  they never overlap the data). `Viewer::setOnFrame` rebuilds the camera-tracking
  overlay each frame; `iv_view` shows a labeled, navigable plot.

### Verification (teeth)
- **Tick generation** (ADR-0024): `ticksFor` matches recorded references (`[0,1]→`
  fifths, `[0,100]→` twenties, `[0,10]→{0,2,…,10}`), minor counts/positions, degenerate/
  reversed ranges, `formatTick` precision, mapping round-trips. **Teeth** — dropping the
  nice-number rounding (raw `range/intervals` step) makes `[0,1]→0.25` and `[0,100]→25`
  → the references go red.
- **Present-path glyphs** (ADR-0025): a headless `recordFrame` readback renders a white
  `H` matching the `render()` path. **Teeth** — forcing the present-path glyph count to
  0 makes only the recordFrame test go red (ink 0) while the headless glyph tests stay
  green.
- **View-projection** (ADR-0026): projecting points and reconstructing the ADR-0012 ray
  confirms each point lies on its ray. **Teeth** — dropping the y-flip breaks ray
  collinearity → red.
- **Annotations** (ADR-0026): toggle/count coverage; the labels-outside-silhouette
  contract (convex-hull point test); a GPU end-to-end render of box + labels over the
  volume. **Teeth** — offsetting tick labels inward puts 175/264 glyph verts inside the
  projected box silhouette → the labels-outside check red.
- Full suite **614 assertions / 59 cases**; ASan+UBSan `ctest` green; text-free
  (`IV_BUILD_TEXT=OFF`), text-off-viewer, and GLFW-free builds green; `iv_view --frames`
  (annotated, with a resize) validation-clean.

## M6 — Text & Annotation Foundation

**Status:** Complete (2026-06-19).
**Governing ADRs:** ADR-0020 (opacity correction; extends ADR-0013), ADR-0021 (2D
overlay substrate — the project's first graphics pipeline), ADR-0022 (vendored
HarfBuzz + Unicode shaping; default font New Computer Modern, GFL faces), ADR-0023
(libharfbuzz-gpu / Slug GPU glyph rendering). Decisions D-0031…D-0036; Backlog B-0008
(resolved by ADR-0020), B-0009, B-0010.

### Added
- **Opacity correction** (ADR-0020): `shaders/ray_march.comp` corrects per-sample
  opacity for step spacing — `α = 1 − (1−a)^(dt·kReferenceSteps)`, `kReferenceSteps
  = 256` — so displayed density is invariant to `stepCount` and scales with path
  length (B-0008).
- **2D overlay substrate** (ADR-0021): the volume render target gains
  `eColorAttachment`; after the compute pass a classic `VkRenderPass` (`loadOp =
  eLoad`, alpha blend) draws colored line-list + triangle-list geometry into the same
  image, identical headless (`render()` → readback) and windowed (`recordFrame` →
  blit). Public `iv::vk::Overlay {lines, triangles, transform}`; `Viewer::overlay()`;
  shaders `overlay.vert/.frag` (`include/iv/vk/renderer.hpp`, `src/vk/renderer.cpp`,
  `src/vk/viewer.cpp`).
- **Vendored HarfBuzz + shaping** (ADR-0022): HarfBuzz vendored at a pinned `main`
  commit (`third_party/harfbuzz/`, D-0035), built minimal as `harfbuzz_core` (one
  amalgamated TU, no ICU/GLib/Cairo/FreeType). `iv::text::Shaper` (UTF-8 + font +
  size → positioned glyphs) hides HarfBuzz (ADR-0004). Default face
  `NewCM10-Book.otf` (New Computer Modern 8.1.0, GFL) embedded via
  `tools/embed_bytes.cmake` → `iv::text::bundledFont()`. New `iv_text` target +
  `IV_BUILD_TEXT` gate (`include/iv/text/`, `src/text/`).
- **Slug GPU glyph rendering** (ADR-0023): `harfbuzz_gpu` lib (the experimental
  libharfbuzz-gpu). `Shaper::encodeGlyph()`/`glyphAtlas()` pack glyph outlines into a
  per-Shaper RGBA16I atlas; the renderer’s third overlay pipeline samples it as an
  R16G16B16A16_SINT uniform texel buffer and rasterizes analytic Slug coverage
  (`shaders/glyph.vert/.frag`, the fragment `#include`-ing the vendored Slug GLSL via
  `glslc -fauto-bind-uniforms`). `iv::text::appendText()` lays a string into NDC
  glyph quads + the atlas; `Overlay` gains `{glyphs, glyphAtlas}`. Headless
  `render()` path (viewer text deferred to M7, B-0010; D-0036).

### Verification (teeth)
- **Opacity invariance** (ADR-0020): a uniform field rendered at `stepCount` 32 vs
  256 matches with the correction; **teeth** — removing the dt-correction makes the
  two diverge (channels by ~150) → the invariance test goes red.
- **Overlay composite** (ADR-0021): a half-screen 50%-alpha red quad over a blue
  volume blends to ~(128,0,128); **teeth** — disabling the pipeline’s alpha blend
  writes pure red → red.
- **Shaping** (ADR-0022): the serif face ligates `ffi` to one glyph; **teeth** —
  disabling the `liga` feature leaves three glyphs (`ffi.size() < 3` fails, 3 < 3),
  proving a real shaper, not a 1:1 map.
- **Glyph rendering** (ADR-0023): a headless white `H` paints in-range ink with
  mid-row stem/crossbar crossings, an outside point stays background, and ink area
  scales ~4× from 48→96 px (resolution independence); **teeth** — skipping the glyph
  draw makes ink == 0 → the coverage and scaling checks go red.
- Full suite **341 assertions / 49 cases**; ASan+UBSan `ctest` green (both entries);
  `IV_BUILD_TEXT=OFF` (text-free) and `IV_BUILD_VIEWER=OFF` (GLFW-free) builds green;
  no HarfBuzz type in any public header; `iv_view --frames` validation-clean.

## M5 — Interactive Viewer & Performance Contract

**Status:** Complete (2026-06-19).
**Governing ADRs:** ADR-0016 (windowing/GLFW & presentation-capable Context),
ADR-0017 (swapchain & present loop), ADR-0018 (interaction & camera control),
ADR-0019 (performance contract & benchmark). Decisions: D-0026…D-0030; Backlog
B-0008.

### Added
- **Presentation-capable `Context`** (ADR-0016): `iv::vk::ContextConfig`
  {`instanceExtensions`, `deviceExtensions`} + `Context::create(const ContextConfig&)`;
  device selection checks `supportsDeviceExtensions`. Headless `create()` delegates
  to it and is unchanged (`include/iv/vk/context.hpp`, `src/vk/context.cpp`).
- **`iv::OrbitCamera`** (ADR-0018): pure host (no Vulkan/GLFW)
  target/distance/yaw/pitch with clamped pitch (`±(π/2−ε)`) and distance, `eye()`
  per the ADR-0012 closed form (`include/iv/orbit_camera.hpp`).
- **Renderer present path** (ADR-0017): `Renderer::recordFrame(cmd, volume, params,
  dstImage, dstExtent)` — dispatches into a lazily (re)created internal storage
  image and **blits** into `dstImage` (component-aware, so an RGBA8 render lands
  correctly in a BGRA8 swapchain); no host readback. Extracted `fillUbo` /
  `writeComputeDescriptors` / `ensureFrameResources`; the M4 `render()`+readback
  path is unchanged (`include/iv/vk/renderer.hpp`, `src/vk/renderer.cpp`).
- **`iv::vk::Viewer`** (ADR-0016/0017/0018), in the isolated **`iv_viewer`** target:
  GLFW window (`GLFW_NO_API`) + surface; swapchain (UNORM, `FIFO`, `eTransferDst`);
  present loop (acquire → record → →`ePresentSrcKHR` barrier → submit → present) with
  one frame in flight, an in-flight fence, and a **per-image** `renderFinished`
  semaphore; resize/recreate on out-of-date/suboptimal/resize (device-idle first;
  minimized → idle). Input: left-drag orbit, scroll zoom, keys Esc/L/C/R;
  `run()` / `runFrames(n)` / `requestResize(w,h)` (`include/iv/vk/viewer.hpp`,
  `src/vk/viewer.cpp`).
- **`iv_view`** demo (`examples/view_demo.cpp`) and headless **`iv_bench`** benchmark
  (`tools/bench.cpp`, ADR-0019).
- **CMake**: `find_package(glfw3)` + `option(IV_BUILD_VIEWER …)` (default ON when
  glfw3 is found); `iv`/tests/`iv_bench` never link GLFW.

### Verification
- Full suite green (**235 assertions / 38 cases**); ASan+UBSan `ctest` green (both
  entries); non-Vulkan suite leak-checked (117/19).
- **Core is GLFW-free:** a `-DIV_BUILD_VIEWER=OFF` configuration builds `iv`, the
  tests, and `iv_bench` with no `glfw3` and no viewer targets (ADR-0016).
- **Viewer on the display** (`DISPLAY=:1`): `iv_view --frames 30` is
  **validation-CLEAN** across acquire / dispatch / blit / present **and** a forced
  swapchain recreation (resize 1280×720 → 960×540) (ADR-0017).
- **Benchmark on the RTX 4070** (Release): median **15.3 ms (~65 FPS)** for one
  512³ → 1280×720 `render()` — **PASS** (≤ 33.3 ms / ≥ 30 FPS, ADR-0019; D-0030).

### Teeth evidence (ADR-0002 §2)
Performed 2026-06-19; all faults reverted; final clean rebuild green.

1. **Benchmark constrains the march budget (ADR-0019) — fault injection.**
   `iv_bench --no-early-term --step-mult 8` (every ray marches 8× the samples) takes
   the median **11.7 ms → 34.3 ms (29.2 FPS)** → the `median ≤ 33.3 ms` assertion
   goes **RED** (nonzero exit); the contracted step → green. NB: plain `--step-mult
   8` does *not* bite (median ≈ 13 ms) because early-ray termination caps cost
   independent of `stepCount` — see **D-0030** / **B-0008**.
2. **Present path (ADR-0017) — fault injection.** Removed the
   `eTransferDstOptimal → ePresentSrcKHR` barrier before present; `iv_view --frames`
   went **DIRTY** — validation: *"images passed to present must be in layout
   PRESENT_SRC_KHR … but is in TRANSFER_DST_OPTIMAL"* (exit 2). Restored → CLEAN.
   This is the teeth behind the `validationClean()` smoke check.
3. **Camera clamp (ADR-0018) — fault injection.** Removed the pitch clamp in
   `OrbitCamera::setPitch`; *"OrbitCamera clamps pitch and distance"* went **RED**
   (`10.0f <= 1.5534f` failed, `test_orbit_camera.cpp:61`). Restored → green. The
   `eye()`-closed-form and orbit-preserves-distance cases guard the rest of the math.
4. **Core GLFW isolation (ADR-0016).** The `IV_BUILD_VIEWER=OFF` build proves
   `iv`/tests/`iv_bench` compile and link with GLFW entirely absent; linking GLFW
   into `iv` (the counterfactual teeth) would break that configuration.

## Fix — Phase seam: store complex (Re, Im), derive magnitude/phase in-shader (ADR-0015)

**Status:** Complete (2026-06-19). **Governing ADRs:** ADR-0015 (supersedes
ADR-0009). Decision: D-0025.

### Problem
M4 rendering showed a thin wrong-color seam along the negative-real axis (dark in
twilight, cyan in HSV). Root cause: ADR-0009 stored per-voxel `phase = arg(z)`, and
the GPU linearly interpolates texture samples — interpolating an *angle* across the
±π branch cut averages +π and −π to ≈0, a spurious value. Magnitude was fine; only
the angle is discontinuous.

### Changed
- The volume texture now stores the **raw complex value** (`R = Re(z), G = Im(z)`);
  `magnitude = length(re,im)` and `phase = atan2(im,re)` are derived **per-sample in
  `shaders/ray_march.comp`**. Interpolating the continuous complex value is correct
  across the cut. (`iv/volume.hpp` `deriveField`; `iv::vk::Volume`;
  `VolumeReadback::Texel` → `{re, im}`.)
- **Precision (ADR-0015):** `double` input is narrowed `double → float` **per
  component in `deriveField`** when filling the staging buffer; magnitude/phase are
  computed in fp32 in-shader; the magnitude range (ADR-0010) is still computed from
  `|z|` in input precision, then narrowed.
- ADR-0009 → **Superseded by ADR-0015**; D-0004/D-0005 revised (D-0025).

### Verification
- Full suite green (**217 assertions / 35 cases**); ASan+UBSan `ctest` green.
- M3 round-trip tests updated to `(re, im)`; M4 known-case renders unchanged.
- New standing test *"Renderer: phase is correct across the branch cut (no seam)"*
  renders a face-on phase wheel on a **coarse** volume (wide interpolation band) and
  asserts the −x axis is red (HSV), not the interpolated-to-zero cyan.

### Teeth evidence (ADR-0002 §2)
Performed 2026-06-19; faults reverted; final clean rebuild green.

1. **Diagnosis A/B (root cause).** Face-on HSV phase wheel: **linear** filtering →
   a cyan line at `θ = ±π`; **nearest** filtering → clean. This isolated the cause
   to linear interpolation of the stored angle across the branch cut (and ruled out
   the renderer logic).
2. **Seam-regression test has teeth — fault injection.** Temporarily reverted to
   storing the phase *angle* (`deriveField`) and reading it back as phase (shader).
   The new *"… across the branch cut (no seam)"* test went **RED** — the −x pixel
   turned cyan, so `r > g+40` failed (`test_vk_renderer.cpp:263–264`). Restored →
   green. (The same A/B at full resolution also confirmed the fix visually: the
   face-on wheel is clean under linear filtering.)

## M4 — Ray-Marching Renderer & Transfer Function

**Status:** Complete (2026-06-19).
**Governing ADRs:** ADR-0011 (rendering substrate & shader toolchain), ADR-0012
(camera, ray/box & compositing), ADR-0013 (opacity transfer function), ADR-0014
(cyclic phase colormap). Decisions: D-0021 (compute substrate), D-0022 (build-time
glslc / embedded SPIR-V), D-0023 (coordinate frame & DVR convention).

### Added
- **Shader toolchain (ADR-0011, D-0022):** `shaders/ray_march.comp` compiled by
  `glslc` at build (CMake `find_program`, **fatal if absent** — extends ADR-0001's
  dependency policy) and embedded into `libiv` via `tools/embed_spirv.cmake`
  (`iv/vk/shaders.hpp`, `makeShaderModule`).
- **Renderer (ADR-0011/0012):** `iv::vk::Renderer` — a compute pipeline (one ray
  per pixel, 8×8 workgroups) samples the volume and writes an `R8G8B8A8_UNORM`
  storage image, read back via the ADR-0006 path. Owns the pipeline, descriptor
  layout/pool, samplers (volume linear/clamp with a nearest fallback if
  `R32G32_SFLOAT` lacks linear filtering; colormap linear/repeat) and the colormap
  LUT. Hand-rolled `vec3` camera math (no GLM); fixed pinhole camera; slab ray/box
  vs `[0,1]³`; front-to-back `over` with early-ray termination.
  (`include/iv/vk/renderer.hpp`, `src/vk/renderer.cpp`.)
- **Transfer function (ADR-0013) & colormap (ADR-0014)**, in-shader: linear/log
  opacity over `[minPositive, max]` (log floor = `minPositive`; degenerate → 0, no
  NaN); phase → `t = (θ+π)/2π` cyclic; a 256-entry twilight LUT (default;
  generated by `tools/gen_colormap.py` → `include/iv/vk/colormap_lut.hpp`) plus
  analytic HSV, selectable by uniform.
- **Shared command helpers (refactor):** `iv/vk/commands.hpp` (`submitOneShot`,
  `imageBarrier`); the M3 `Volume` upload was refactored onto them (no behavior
  change; M3 tests still green).

### Verification
- Build: **warning-clean** under the strict set + `-Werror` (Debug, ASan+UBSan).
- Debug: full suite green — **210 assertions / 34 cases** on the NVIDIA RTX 4070
  (41 assertions / 5 cases new for M4).
- ASan+UBSan: `ctest` green (both entries); LSan scoped (D-0015); non-Vulkan suite
  leak-checked. Renders are validation-clean and deterministic.

### Teeth evidence (ADR-0002 §2)
All performed 2026-06-19; each fault reverted; the final clean rebuild is green.
(Cited by test-case name — Catch2 randomizes case order.)

1. **Compositing order (ADR-0012) — fault injection.** Reversed the march to
   sample back-first (`t1 - (i+0.5)·dt`). *"Renderer: front-to-back compositing is
   order-dependent"* went **RED** — the rear (red) layer dominated, so
   `center.b > center.r` failed. Reverted.
2. **Linear/log opacity (ADR-0013) — fault injection.** Swapped the `opacityMode`
   branch (`== 0u` → `!= 0u`). *"… uniform field (linear, HSV) …"* then rendered
   the degenerate log path (cyan → background) and *"… log opacity on a uniform
   (degenerate-range) field …"* rendered linear (background → cyan); both **RED**
   (plus dependent renders). Reverted.
3. **Colormap phase mapping (ADR-0014) — fault injection.** Offset `t` by `+0.25`.
   The known-phase pixels rotated hue: *"… uniform field (linear, HSV) …"* (no
   longer cyan) and *"… LUT colormap matches the twilight table …"* went **RED**.
   Reverted.
4. **Shader build-tool gate (ADR-0011) — fault injection.** Pointed
   `find_program` at a nonexistent compiler; a fresh configure aborted with
   `CMake Error … ADR-0011: glslc … is required to compile shaders` (exit 1).
   Reverted.

## M3 — Volume Data Model & GPU Upload

**Status:** Complete (2026-06-19).
**Governing ADRs:** ADR-0008 (ingestion API & data-layout convention), ADR-0009
(GPU volume texture: format/derived contents/precision/upload), ADR-0010
(magnitude-range metadata). Decisions: D-0017 (raw allocation), D-0018
(ingestion/upload contract), D-0019 (range metadata), D-0020 (precision paths are
not bit-identical).

### Added
- **Host data model (ADR-0008/0010):** `iv::GridDims` (x-fastest, 0-based
  `index`/`count`, 64-bit arithmetic; the convention pinned by `static_assert`),
  `iv::MagnitudeRange`, `iv::VolumeOptions`; host validators (`validateGrid`,
  `validateShape`, `validateOptions`); `iv::deriveField<T>` — per-voxel
  `(|z|, arg z)` computed in input precision then narrowed to fp32, returning the
  auto magnitude range (`minPositive` excludes zeros). (`include/iv/volume.hpp`,
  `src/volume.cpp`.)
- **Shared memory helper (ADR-0009, D-0017):** `iv::vk::findMemoryType` plus
  `allocateAndBindImage` / `allocateAndBindBuffer` — the generalized successors to
  M2's file-local `findMemoryType`; `clearAndReadback` refactored onto them. Raw
  `vkAllocateMemory`; VMA still deferred (B-0006). (`include/iv/vk/memory.hpp`,
  `src/vk/memory.cpp`.)
- **GPU volume (ADR-0009):** `iv::vk::Volume` — move-only 3D `R32G32Sfloat`
  texture (R = raw magnitude, G = phase ∈ [−π, π]); host-visible staging filled
  x-fastest + `copyBufferToImage`; classic 1.0 barriers (D-0016); rests in
  `eShaderReadOnlyOptimal` with a sampling view (the sampler is deferred to M4);
  borrows the Context's device/queue/pool. `readback()` round-trips to the host
  bit-exactly and restores the resting layout. `magnitudeRange()`
  (override-else-auto) and `autoMagnitudeRange()`. Separate `float` and `double`
  input overloads. (`include/iv/vk/volume.hpp`, `src/vk/volume.cpp`.)

### Verification
- Build: **warning-clean** under the strict set + `-Werror` (Debug, ASan+UBSan).
- Debug: full suite green — **169 assertions / 29 cases** on the NVIDIA RTX 4070.
- ASan+UBSan: `ctest` green (both entries); LSan scoped per D-0015; the non-Vulkan
  suite (incl. the new host `[volume]` tests) leak-checked clean (`~[vk]`).
- Round-trips verified **bit-exactly** for both `float` and `double` input on the
  real GPU; create/upload/readback/teardown validation-clean (pNext messenger
  gate); identical across repeated runs (deterministic, ADR-0007).

### Teeth evidence (ADR-0002 §2)
All performed 2026-06-19; each fault reverted; the final clean rebuild is green.
(Teeth are cited by test-case name — Catch2 randomizes case order.)

1. **Index-order convention (ADR-0008) — compile-time.** Transposed
   `GridDims::index` to z-fastest. The header `static_assert`s fired:
   `error: static assertion failed` at `include/iv/volume.hpp:47-49`
   (e.g. `index(1,0,0) == 1`). The x-fastest convention is pinned at compile time.
   Reverted.
2. **R/G channel order (ADR-0009) — fault injection.** Swapped the magnitude/phase
   writes in `deriveField`. Went **RED**: *"deriveField computes (|z|, arg z) in
   channel order"*, *"deriveField double path derives in double then narrows"*,
   and both GPU round-trips (*"… bit-exactly (float)"*, *"… double input round-trips
   its own fp32 expectation"*). Reverted.
3. **Magnitude formula (ADR-0009) — fault injection.** `std::abs` → `std::norm`
   (|z|²). Went **RED**: *"deriveField computes (|z|, arg z) in channel order"*
   (every non-zero voxel's magnitude, plus the range). Reverted.
4. **Phase formula (ADR-0009) — fault injection.** Negated the phase
   (`std::arg(std::conj(z))`, standing in for an `atan2`-argument swap). Went
   **RED**: the phase assertions of *"deriveField computes (|z|, arg z) in channel
   order"* for non-zero-phase voxels. Reverted.
5. **Precision policy (ADR-0009 / D-0005 / D-0020) — fault injection.** Derived
   after casting the input to `float` regardless of `T`. **Only the double GPU
   round-trip** (*"… double input round-trips its own fp32 expectation"*) went
   **RED**; every `float` test stayed green — proving derivation happens in input
   precision and exhibiting exactly the float/double ULP divergence of D-0020.
   Reverted.
6. **Magnitude-range zero-exclusion (ADR-0010) — fault injection.** Changed
   `mag > 0` to `mag >= 0` so zeros pollute `minPositive`. Went **RED**: the
   `minPositive` assertions of the two `deriveField` tests and
   *"Volume exposes auto and overridden magnitude range"*. Reverted.
7. **Shape validation (ADR-0008) — fault injection.** Dropped the `size == count`
   check in `validateShape`. A wrong-sized input was accepted; went **RED**:
   *"validators reject malformed ingestion inputs"* and
   *"Volume::create rejects malformed inputs"*. Reverted.
8. **Override validation (ADR-0010) — fault injection.** Dropped the override
   sanity check in `validateOptions`. An invalid `max < minPositive` override was
   accepted; went **RED**: *"validators reject malformed ingestion inputs"* and
   *"Volume::create rejects malformed inputs"*. Reverted.

(Test-robustness note: the rejection assertions use a short-circuiting `rejected()`
helper — `!has_value() && error().code == c` — so a fault that wrongly *succeeds*
fails the CHECK cleanly instead of calling `.error()` on a value, which would be
UB on `std::expected`.)

## M2 — Vulkan Headless Bring-Up

**Status:** Complete (2026-06-19).
**Governing ADRs:** ADR-0004 (binding & ownership), ADR-0005 (instance/device/
queue selection), ADR-0006 (offscreen target & readback), ADR-0007 (concurrency
baseline). Decisions: D-0015 (LSan scoping), D-0016 (classic barriers).

### Added
- **Binding boundary (ADR-0004):** `iv/vk/vulkan.hpp` (centralized
  `VULKAN_HPP_NO_EXCEPTIONS` include); `vk::Result → iv::Errc` mapping
  (`iv/vk/result.hpp`); `iv::vk::Unique<Handle>` — our own move-only,
  single-owner RAII wrapper. Default static dispatch; the two debug-utils procs
  are loaded by hand (no dynamic dispatcher).
- **Context (ADR-0005):** instance + validation layer/`VK_EXT_debug_utils`
  messenger in Debug (best-effort, message-capturing); physical-device ranking
  (discrete > integrated > virtual > cpu, accepts software) with
  `IV_VULKAN_DEVICE_INDEX` override; logical device + one graphics queue; command
  pool. A `pNext` create/destroy messenger + shared counter make object leaks at
  `vkDestroyInstance` observable.
- **Offscreen (ADR-0006):** `clearAndReadback` — `R8G8B8A8_UNORM` device-local
  image + host-visible staging buffer; classic 1.0 barriers (D-0016); clear →
  copy → fence → map. `ImageReadback` exposes the fixed top-left, row-major,
  `(y*w+x)*4` layout.
- **Concurrency (ADR-0007):** single-threaded, not-thread-safe baseline; Debug
  thread-affinity check on Context accessors; fence-gated host reads; the atomic
  assert handler is the sole cross-thread exception.

### Verification
- Build: **warning-clean** under the strict set + `-Werror` (Debug, ASan+UBSan,
  TSan).
- ASan+UBSan: full suite green via `ctest`; **LSan scoped off for the
  Vulkan-inclusive run** (D-0015), and our own non-Vulkan code leak-checked clean
  (`iv_tests_leakcheck`, `~[vk]`).
- TSan: the `[concurrency]~[vk]` race-gate is clean.
- Exercised on the **NVIDIA RTX 4070** (selected by ranking) — instance/device/
  queue/pool bring-up validation-clean; offscreen clear reads back exact UNORM
  bytes; deterministic. 83 assertions / 19 cases.

### Teeth evidence (ADR-0002 §2)
All performed 2026-06-19; each fault reverted; the final clean rebuild is green.

1. **Readback layout — fault injection.** Transposed `(x,y)→offset` in
   `ImageReadback::at`. On the non-square `[offscreen]` layout test (3×2, distinct
   per-pixel values) it read out of bounds: `AddressSanitizer:
   heap-buffer-overflow at offscreen.cpp:39`. (Note: the *uniform-clear* GPU tests
   can't catch this — every pixel is identical — which is why the dedicated
   varying-data `at` test exists.) Reverted.
2. **Validation gate — fault injection.** Cleared the image with
   `eTransferSrcOptimal` (wrong layout). The messenger reported
   `vkCmdClearColorImage(): ... can only be TRANSFER_DST_OPTIMAL ...` and
   `CHECK(ctx->validationClean())` went **RED** (`test_vk_offscreen.cpp:43`).
   Reverted.
3. **Device-selection error code — fault injection.** Returned `Errc::internal`
   for an out-of-range `IV_VULKAN_DEVICE_INDEX`. The `[context]` test went **RED**
   (`test_vk_context.cpp:48`, `code == device_unavailable`). Reverted.
4. **Concurrency (TSan) — fault injection.** Made the assert-handler storage a
   plain pointer (non-atomic). Under `-DIV_SANITIZE=thread`:
   `ThreadSanitizer: data race at assert.cpp:27 in set_assert_handler`. Reverted.
5. **Thread-affinity — fault injection.** Disabled the affinity `IV_DEBUG_ASSERT`.
   The off-thread test went **RED** (`test_concurrency.cpp:63`, handler never
   fired). Reverted.
6. **Teardown-leak gate — fault injection.** Made the command-pool deleter a
   no-op. The pNext messenger reported the undestroyed object during teardown and
   the `[context]` teardown test went **RED** (`test_vk_context.cpp:63`,
   `counter == 0`). Reverted.

## M1 — Build, Toolchain & Test/Sanitizer Harness

**Status:** Complete (2026-06-18).
**Governing ADRs:** ADR-0001 (build/toolchain/dependency policy), ADR-0002 (test
framework + teeth-evidence convention), ADR-0003 (error & failure semantics).

### Added
- **Build:** CMake (≥3.25) / strict ISO C++23 / system GCC (≥13 enforced at
  configure). Mandatory strict warning set with `-Werror` on first-party targets
  (`iv_warnings`); tree-wide sanitizer selection via `IV_SANITIZE`
  (`address,undefined` | `thread`); `compile_commands.json` exported. Layout:
  `include/ src/ tests/ third_party/ tools/`.
- **Test framework:** Catch2 **v3.7.1** vendored (amalgamated) under
  `third_party/catch2/`; first-party tests built with the strict set and linked
  against Catch2's built-in main; registered with CTest.
- **Error model (ADR-0003):** `iv::Errc`, `iv::Error`, `iv::Result<T>`,
  `iv::Status`, `iv::make_error`, `iv::to_string`, `iv::format` — `std::expected`
  based, exception-free. (`include/iv/error.hpp`, `src/error.cpp`.)
- **Assertions (ADR-0003):** `IV_ASSERT` (always-on) / `IV_DEBUG_ASSERT`
  (Debug-only) with an overridable, atomic failure handler.
  (`include/iv/assert.hpp`, `src/assert.cpp`.)
- **Suite:** 9 test cases / 25 assertions.

### Verification
- Debug build: **warning-clean** under the full strict set + `-Werror` (exit 0).
- ASan+UBSan (`-DIV_SANITIZE=address,undefined`): **suite green** via `ctest`
  (1/1) and directly (25 assertions / 9 cases).
- TSan: not applicable to M1 — no multi-threaded code path is exercised (the
  assert handler uses `std::atomic` but tests are single-threaded). The project
  concurrency baseline is set by an M2 ADR (§6).

### Teeth evidence (ADR-0002 §2)
All demonstrations performed 2026-06-18; each fault reverted; the final clean
rebuild of both configs is green.

1. **`-Werror` gate — fault injection.** Added unused local `teeth_demo_unused`
   to `iv::format` (`src/error.cpp`). Build **failed**:
   `error: unused variable 'teeth_demo_unused' [-Werror=unused-variable]`.
   Reverted → builds clean.
2. **UBSan gate — fault injection.** Added a temporary `[teeth-demo]` case with a
   `volatile int` signed overflow (`INT_MAX + 1`). Under
   `-DIV_SANITIZE=address,undefined`:
   `test_assert.cpp:81: runtime error: signed integer overflow: 2147483647 + 1
   cannot be represented in type 'int'` (nonzero exit). Reverted.
3. **Error mapping — red→green.** Perturbed
   `to_string(Errc::invalid_argument)` → `"WRONG"` (`src/error.cpp`). Case
   *"to_string maps each Errc to its stable name"* (`test_error.cpp:12`) went
   **RED** (`"WRONG" == "invalid_argument"`), as did the `format` case. Reverted
   → green.
4. **`IV_ASSERT` logic — red→green.** Inverted the macro's condition sense
   (`if (!(cond))` → `if ((cond))`, `include/iv/assert.hpp`). The fault compiles
   (condition still evaluated). Cases *"IV_ASSERT invokes the handler on a false
   condition"* and *"… does not fire on a true condition"* went **RED** (3 of 4
   cases, 7 of 11 assertions failed). Reverted → green. (Aside: fully neutering
   the macro is caught even earlier — by `-Werror=unused-variable` on the now-
   unused test locals — so the assertion contract is guarded by two independent
   gates.)
