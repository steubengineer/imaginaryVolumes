# HANDOFF.md — imaginaryVolumes

**Last updated:** 2026-06-19 by the M7 session (Claude / Opus 4.8)
**Active milestone:** **M7 Complete & locked.** Next: **M8 — Legend/Colorbar &
High-Level Plot API** (not started; begin at ORIENT → CONTRACT). M1–M7 complete & locked.

## Current State
**M1–M5 Complete and locked** (MILESTONES.md; CHANGELOG.md): ingest a complex field →
3D RG32F texture → compute ray-march → offscreen readback **and** interactive GLFW
viewer, with a perf benchmark (15.3 ms / ~65 FPS @ 512³→720p on the RTX 4070).

**M6 (text & annotation foundation) is Complete & locked** (CHANGELOG.md § M6):
opacity correction (ADR-0020), the 2D overlay substrate / first graphics pipeline
(ADR-0021), vendored **HarfBuzz** + `iv::text::Shaper` + bundled **New Computer
Modern** GFL face (ADR-0022), and **Slug** GPU glyph rendering (ADR-0023, headless).
Decisions D-0031…D-0036.

**M7 (bounding box, ticked axes & labels) is Complete & locked** (CHANGELOG.md § M7).
Three ADRs, three commits, all gates green:
- **ADR-0024** (ec9035b) — declarative **`iv::PlotAxes`** model (pure host, core `iv`:
  `include/iv/plot_axes.hpp`): per-axis `{min,max,label,unit, counts?}` in data units,
  visibility toggles, `BoxTickStyle`, `ThroughAxis`; nice `{1,2,5}` major+minor
  `ticksFor`, value-only `formatTick`.
- **ADR-0025** (8f19540) — **present-path glyph rendering**: `recordFrame` draws
  `Overlay::glyphs` (persistent, grown-on-demand `ensureFrameGlyphResources`; atlas
  rebuilt on growth, vertices re-uploaded each frame, D-0038). `drawOverlay` takes
  glyph handles. Resolves B-0010; the viewer shows text.
- **ADR-0026** (d305381) — **world-space annotations**: `iv::vk::viewProjection` (core
  `iv`, matches the ADR-0012 ray camera) via `Overlay::transform`; `iv::text::
  buildAnnotations` fills an Overlay (box + silhouette/all-faces ticks + through-axes +
  silhouette-edge labels offset outward); `Viewer::setOnFrame` rebuilds it per frame.
  `iv_view` is now a labeled, navigable plot.

Decisions D-0031…D-0039; Backlog B-0008/B-0010 resolved, B-0007 (resolved by M7),
B-0009 open. ADR index current (ADR-0001…0026 Accepted). Gates at M7 close: full suite
**614/59**; ASan+UBSan `ctest` green; text-free / text-off-viewer / GLFW-free builds
green; no HarfBuzz type in any public header; `iv_view --frames` (annotated)
validation-clean.

## In Flight (work started, not finished)
**Nothing in flight.** M7 is closed; M8 has not started. Working tree is clean at
`d305381` (ADR-0026) + the M7 RECORD commit.

## Next Action (begin M8)
**Start M8 — Legend/Colorbar & High-Level Plot API** at ORIENT → CONTRACT (DEV_PROCESS
§3.0, §2). Per MILESTONES.md M8: a **legend/colorbar** (the phase→color wheel + the
magnitude→opacity scale, with labeled bounds, matching the ADR-0013 transfer function
/ ADR-0014 colormap) and a **high-level `plot(field, dims, options)` facade** over the
two-step volume/viewer setup. Reuse: the ADR-0021 overlay (lines/triangles/glyphs) for
the colorbar swatch + labels, `iv::text::buildAnnotations`/`appendText` for legend
text, `iv::PlotAxes`-style declarative options. Write the M8 ADRs (Proposed → Accepted
gate) before implementing. LaTeX math is still deferred.

## Known-Broken / Blocked
- **Nothing broken.** The tree builds and all gates pass.
- VMA still deferred (D-0017); B-0006 open — the viewer adds swapchain images
  (driver-managed) and a couple of small per-frame allocations; revisit if memory
  management grows.
- `clearAndReadback` (M2) still has its own inline submit/barriers rather than the
  shared `commands.hpp` helpers — fold in if touched (not urgent).
- A separate present queue is **not** supported (ADR-0016): graphics == present on
  the target; deferred to Backlog.

## Landmines & Context
- ORIENT before writing (§3.0): this file → DEV_PROCESS → MILESTONES → ADR INDEX →
  DECISIONS. Restate governing ADRs before coding; ADRs are append-only and Accepted
  ones are immutable — record deviations in DECISIONS.md + CHANGELOG (as D-0030 did
  for the ADR-0019 teeth).
- **`glslc` is a required build tool** (ADR-0011/D-0022): configure fails without it.
  Shaders live in `shaders/`; SPIR-V is embedded (regenerated each build). The
  colormap LUT is committed data — regenerate with `tools/gen_colormap.py` if it
  changes.
- **Viewer is GLFW-coupled and isolated** (ADR-0016): only `iv_viewer`/`iv_view`
  link `glfw`. The core `iv`, tests, and `iv_bench` must keep building with
  `-DIV_BUILD_VIEWER=OFF` / no glfw3 — that configuration is the isolation gate.
  Build the viewer with the system GLFW (`libglfw3-dev`, 3.3.10 here).
- **Text is HarfBuzz-coupled and isolated** (ADR-0022): only `iv_text` (and its
  tests) link vendored HarfBuzz. `-DIV_BUILD_TEXT=OFF` must keep `iv`/tests building
  with **no HarfBuzz present** — the text-free isolation gate (mirrors the viewer).
  HarfBuzz is **vendored, not system** (`third_party/harfbuzz/`, pin in VENDORING.md);
  it builds as the single amalgamated TU `src/harfbuzz.cc` (~12 MB object, ~12 s) with
  **no `HAVE_*` defines** (built-in OpenType+UCD). Never add a `HAVE_*`/ICU/FreeType
  define. `iv::text::Shaper` is the only place that includes `<hb.h>`; **no HB type
  may appear in any `include/` header** (ADR-0004 — there's a grep-able gate). The
  bundled face is embedded (no runtime file); positions are 26.6 fixed point →
  divide by 64 for pixels (already done in `Shaper::shape`).
- **Slug glyph rendering (ADR-0023, D-0036) gotchas:** the atlas is **RGBA16I**
  (4×int16/texel, uploaded as `R16G16B16A16_SINT`), *not* int32 — this is why blobs
  are 8-byte- not 16-byte-aligned, and the source of the ±8000-unit coordinate range.
  Glyph outlines are encoded in **font units** (a separate upem-scaled `hb_font`), so
  `EncodedGlyph::extents` are font units (e.g. 'H' ≈ 33,0..716,683 at upem 1000); the
  layout scales by `pixelSize/upem`. The vendored Slug GLSL is OpenGL-style; it only
  compiles for Vulkan because `shaders/glyph.frag` is built with
  `-fauto-bind-uniforms -fauto-map-locations` (the atlas `isamplerBuffer` → **set 0 /
  binding 0** — the glyph descriptor layout must match). Glyph quads are baked to
  **clip space (NDC) on the CPU** (no `hb_gpu_dilate`). Glyphs render on the
  **headless `render()` path only**; the present path passes `nullptr` glyphs
  (B-0010). The glyph pipeline is in core `iv` (no HarfBuzz link), so it builds with
  `IV_BUILD_TEXT=OFF` — don't make it depend on `iv_text`.
- **Annotations (ADR-0024/0026) gotchas:** `iv::vk::viewProjection` must stay
  **consistent with the ADR-0012 ray camera** (the `[viewproj]` collinearity test is
  the guard — don't "simplify" the matrix or drop the y-down/top-left flip). It is
  **column-major** for the overlay shader. `iv::text::buildAnnotations` (in `iv_text`,
  needs a `Shaper`) fills an `Overlay`: box/ticks/through-axes are **world-space**
  (via `Overlay::transform`); **labels are screen-space** glyphs (NDC-baked,
  `appendText`) anchored to the **box silhouette** (exact face-facing test) and offset
  **outward** — the outward offset is load-bearing for no-data-overlap (the
  `[annot]` "labels outside silhouette" test is the guard). The viewer tracks the
  camera via `Viewer::setOnFrame` (rebuilds the overlay each frame, after
  `applyCamera`); `iv_view` links `iv_text` only when `IV_BUILD_TEXT` (`#ifdef
  IV_VIEW_TEXT`, else a simple overlay). `PlotAxes`/`ticksFor` are pure host (core
  `iv`) — log axes / custom formatters are out of scope (ADR-0024).
- **Viewer lifetime is pimpl-ordered** (`src/vk/viewer.cpp` `Viewer::Impl`): member
  declaration order is load-bearing (GLFW lib guard first → destroyed last; window
  after the Context; all device children before the Context). `~Impl` waits the
  device idle (via a cached `device` handle, not the affinity-checked accessor)
  before any Unique<> deleter runs. The GLFW user-pointer points into the stable
  `unique_ptr<Impl>`, so the Viewer stays cheaply movable.
- **Present correctness:** the swapchain image must be in `ePresentSrcKHR` at present
  (dropping that barrier is a validation error — the teeth). `renderFinished` is
  **per swapchain image** (not per frame) so present never waits a reused semaphore.
  Blit is **component-aware**, so an RGBA8 render lands correctly in a BGRA8
  swapchain (don't "fix" this with a copy).
- **Opacity correction (ADR-0020, done):** `ray_march.comp` now applies
  `α = 1 − (1−a)^(dt·kReferenceSteps)` (`kReferenceSteps = 256`), so density is
  invariant to `stepCount`. Combined with early-ray termination, render cost stays
  largely `stepCount`-insensitive, so the ADR-0019 perf teeth still needs
  `--no-early-term` to bite (D-0030). Don't "simplify" the bench teeth back to plain
  `--step-mult 8`.
- **Coordinate convention (ADR-0012):** right-handed, **+Y up**, volume = `[0,1]³`,
  **world position = texture coordinate**, image origin top-left.
- **Volume stores `(Re, Im)`, not `(magnitude, phase)`** (ADR-0015, supersedes
  ADR-0009): magnitude/phase derived in-shader (`length`/`atan2`). Never store or
  interpolate the phase *angle* (a standing seam test guards this).
- **`R32G32_SFLOAT` linear filtering isn't core-mandatory**; the renderer queries it
  and falls back to nearest (`Renderer::volumeLinearFilter()`).
- **Precision** (D-0020 / D-0025): `double` input is narrowed per component in
  `deriveField`; the magnitude *range* is computed from `|z|` in input precision.
- **LSan scoped off for Vulkan-inclusive runs** (D-0015); the validation layer (incl.
  the pNext teardown messenger) is the Vulkan-object-leak gate.
- M2–M5 use **classic 1.0 barriers** (D-0016); no sync2 (present path didn't need
  it). Vulkan boundary (ADR-0004): include only `iv/vk/vulkan.hpp`; never leak
  `vk::Result` into consumer signatures.
- Rejection tests use the short-circuiting `rejected()` helper.

## Commands
- Build/test (Debug): `cmake --build build/debug` then
  `ASAN_OPTIONS=detect_leaks=0 ./build/debug/tests/iv_tests` (filter e.g.
  `"[renderer]"` / `"[camera]"`).
- Sanitizer gate: `cmake -S . -B build/asan -DIV_SANITIZE=address,undefined` then
  `ctest --test-dir build/asan --output-on-failure`.
- GLFW-free check: `cmake -S . -B build/noviewer -DIV_BUILD_VIEWER=OFF` then build.
- Text-free check: `cmake -S . -B build/notext -DIV_BUILD_TEXT=OFF` then build
  `iv_tests` (no HarfBuzz; the `[text]` suite is excluded). Text tests: `[text]`.
- Viewer (needs a display; `DISPLAY=:1` here): `cmake --build build/debug --target
  iv_view` then `DISPLAY=:1 ./build/debug/iv_view` (interactive) or `… --frames N`
  (renders N frames, reports validation cleanliness, exits).
- Benchmark (RTX 4070; build Release for a fair number): `cmake -S . -B build/release
  -DCMAKE_BUILD_TYPE=Release` → `./build/release/iv_bench` (knobs: `--frames N`,
  `--step-mult K`, `--no-early-term`, `--advisory`).
- ADR index: `python3 tools/regenerate_adr_index.py [--check]`.
- `IV_VULKAN_DEVICE_INDEX` forces a device.

## Pointers
- Governing process: `DEV_PROCESS.md`.
- Milestone arc: `MILESTONES.md` (M1–M7 complete & locked; M8 next).
- Contracts: `docs/adr/INDEX.md` — ADR-0001…0026 Accepted (ADR-0009 superseded by
  ADR-0015).
- Decisions & rationale: `DECISIONS.md` (D-0001…D-0039), Backlog B-0001…B-0010.
- M1–M7 work + teeth: `CHANGELOG.md`.
- Demos: `examples/iv_render_demo [out_dir]` (offscreen PNGs via the owned
  `examples/png.hpp`, D-0024; gitignored `gallery/`); `iv_view` (interactive
  viewer); `iv_bench` (perf).
- Code: host model `include/iv/volume.hpp`, `include/iv/orbit_camera.hpp`,
  `include/iv/plot_axes.hpp` (ADR-0024), `src/volume.cpp`, `src/plot_axes.cpp`; Vulkan
  `include/iv/vk/`, `src/vk/` (commands, memory, shaders, context, offscreen, volume,
  renderer, **view_projection** (ADR-0026), viewer); text `include/iv/text/`,
  `src/text/` (shaper, bundled_font, text_layout, **annotations** (ADR-0026)); vendored
  `third_party/harfbuzz/` (pin in VENDORING.md), `third_party/fonts/`; shaders
  `shaders/`; generated `colormap_lut.hpp`; benchmark `tools/bench.cpp`; tests
  `tests/test_*.cpp`.
