# HANDOFF.md — imaginaryVolumes

**Last updated:** 2026-06-19 by the M6 session (Claude / Opus 4.8)
**Active milestone:** **M6 — Text & Annotation Foundation** (IMPLEMENT in progress;
contract Accepted). M1–M5 complete & locked.

## Current State
**M1–M5 Complete and locked** (MILESTONES.md; CHANGELOG.md): ingest a complex field →
3D RG32F texture → compute ray-march → offscreen readback **and** interactive GLFW
viewer, with a perf benchmark (15.3 ms / ~65 FPS @ 512³→720p on the RTX 4070).

**M6 is the text & annotation foundation** for publication-quality, quantitatively
legible plots; split from M7 (annotations: box/axes/legend/units/API). **LaTeX is
deferred** to a later milestone — M6 labels are text/Unicode.

- **M6 CONTRACT Accepted** — ADR-0020 (opacity correction) · ADR-0021 (2D overlay
  substrate = first graphics pipeline) · ADR-0022 (vendor HarfBuzz + Unicode shaping;
  default font **New Computer Modern**, **GFL faces only**) · ADR-0023
  (libharfbuzz-gpu / **Slug** GPU glyph rendering). D-0031…D-0034; B-0008 scheduled,
  B-0009 logged. Index current.
- **✅ ADR-0020 DONE & committed (53d7c84):** `shaders/ray_march.comp` corrects
  per-sample opacity for step spacing — `α = 1 − (1−a)^(dt·kReferenceSteps)`,
  `kReferenceSteps = 256` (a file-scope shader const). Density is now invariant to
  `stepCount` and scales with path length (B-0008). Invariance test +
  demonstrated teeth in `tests/test_vk_renderer.cpp` ("opacity is invariant to
  stepCount").
- **✅ ADR-0021 DONE & committed (c951229):** 2D overlay substrate (first graphics
  pipeline) — see In Flight below.
- **✅ ADR-0022 DONE (this session's 3rd M6 commit):** vendored HarfBuzz +
  `iv::text::Shaper` + bundled GFL font. HarfBuzz `main` @ `ac0979b` mirrored into
  `third_party/harfbuzz/` (D-0035), built minimal (`harfbuzz_core`, amalgamated
  `src/harfbuzz.cc`, no ICU/GLib/Cairo/FreeType, SYSTEM, not `-Werror`), gated by
  `IV_BUILD_TEXT` (default ON; OFF = text-free isolation gate). `iv::text::Shaper`
  (UTF-8+size → positioned glyphs; **no HB type in any public header** — verified)
  wraps it; `NewCM10-Book.otf` (NCM 8.1.0, GFL) embedded via `tools/embed_bytes.cmake`
  → `iv::text::bundledFont()`. Tests `tests/test_text_shaper.cpp` ("[text]", 6 cases)
  + demonstrated teeth (disable `liga` → "ffi" stays 3 glyphs → red). Full suite
  **296/46**; ASan+UBSan green; text-free build green.

## In Flight (work started, not finished) — M6 IMPLEMENT
ADR-0020, ADR-0021, and ADR-0022 are done. **One ADR remains** (0023; it needs both
0021's overlay and 0022's shaping):
- **✅ ADR-0021 — 2D overlay substrate (DONE, in this session's 2nd M6 commit).**
  First graphics pipeline: render target has `eColorAttachment`; a classic render
  pass (`loadOp = eLoad`, alpha blend) draws line/triangle geometry over the volume
  image in both `render()` (→ readback) and `recordFrame()` (→ blit). Public
  `iv::vk::Overlay {lines, triangles, transform}`; `render()`/`recordFrame()` take an
  optional overlay; `Viewer::overlay()` drives it. Shaders `overlay.vert/.frag`.
  Headless composite test + teeth (disable blend → red); `iv_view --frames` overlay
  validation-clean. **For glyphs (ADR-0023), emit quads into `Overlay::triangles`;
  the camera→clip `transform` (world-space box/axes) is M7's to fill.**
- **✅ ADR-0022 — vendor HarfBuzz + shaping (DONE, this session's 3rd M6 commit).**
  HarfBuzz `main` @ `ac0979b` mirrored into `third_party/harfbuzz/src/` (incl. the
  `hb-gpu*.cc` sources + `.glsl` shaders for ADR-0023); `harfbuzz_core` static lib
  from the amalgamated `src/harfbuzz.cc`, no ICU/GLib/Cairo/FreeType, SYSTEM, not
  `-Werror`. `iv::text::Shaper` (`include/iv/text/shaper.hpp`, `src/text/shaper.cpp`)
  hides HarfBuzz (ADR-0004); `NewCM10-Book.otf` (NCM 8.1.0, GFL) embedded via
  `tools/embed_bytes.cmake` → `iv::text::bundledFont()`. `iv_text` target + tests
  gated by `IV_BUILD_TEXT`. Pin/licensing in `third_party/harfbuzz/VENDORING.md` +
  `third_party/fonts/README.md` (D-0035). **For ADR-0023, the GPU sources are already
  vendored** — wire `hb-gpu*.cc` into the build and compile the `.glsl`.
- **ADR-0023 — Slug glyph rendering.** Encode glyph outlines with `libharfbuzz-gpu`
  (cached per `(font, glyphId)`); draw quads in the ADR-0021 overlay with its **GLSL**
  Slug shaders compiled through our `glslc`→SPIR-V→embed toolchain (ADR-0011/D-0022).
  Documented fallback: self-baked MSDF if the experimental API is unworkable.

## Next Action (continue M6 IMPLEMENT)
**Implement ADR-0023 (Slug GPU glyph rendering)** — the last M6 ADR. The GPU sources
are already vendored (D-0035): add `harfbuzz_gpu` to `third_party/harfbuzz/CMakeLists.txt`
(`hb-gpu.cc`, `hb-gpu-draw.cc`, `hb-gpu-paint.cc`; links `harfbuzz_core` + `m`),
extend `iv_text` to use it. Per `hb-gpu.h`: `hb_gpu_draw_create_or_fail()` →
`hb_gpu_draw_get_funcs()` fed to `hb_font_draw_glyph(font, gid, funcs, draw)` →
`hb_gpu_draw_encode()` returns the encoded outline blob (cache per `(font,glyphId)`,
ADR-0014 style); `hb_gpu_shader_source(stage, HB_GPU_SHADER_LANG_GLSL)` yields the
GLSL (the `.glsl` files are vendored — compile via our `glslc`→SPIR-V→embed
toolchain, ADR-0011). Draw one quad per shaped glyph into `Overlay::triangles`
(ADR-0021), alpha-blended. Verify: glyph coverage at sampled pixels vs a recorded
reference + zoom-invariance (two scales); teeth = skip the encode / wrong shader →
blank/wrong → red. **MSDF is the documented fallback** if the experimental API is
unworkable at the pin.

Then **M6 RECORD**: write the CHANGELOG § M6 (all teeth: opacity invariance,
overlay blend, ligature shaping, glyph coverage), MILESTONES M6 → Complete & locked,
update HANDOFF, final M6 commit. Commit at verified checkpoints (so far: 53d7c84 =
contract + ADR-0020; c951229 = ADR-0021; 3rd M6 commit = ADR-0022).

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
- Milestone arc: `MILESTONES.md` (M1–M5 complete & locked; M6 IMPLEMENT, 3/4 ADRs done).
- Contracts: `docs/adr/INDEX.md` — ADR-0001…0023 Accepted (ADR-0009 superseded by
  ADR-0015).
- Decisions & rationale: `DECISIONS.md` (D-0001…D-0035), Backlog B-0001…B-0009.
- M1–M5 work + teeth: `CHANGELOG.md` (M6 section written at M6 RECORD; per-ADR teeth
  are in the commit messages until then).
- Demos: `examples/iv_render_demo [out_dir]` (offscreen PNGs via the owned
  `examples/png.hpp`, D-0024; gitignored `gallery/`); `iv_view` (interactive
  viewer); `iv_bench` (perf).
- Code: host model `include/iv/volume.hpp`, `include/iv/orbit_camera.hpp`,
  `src/volume.cpp`; Vulkan `include/iv/vk/`, `src/vk/` (commands, memory, shaders,
  context, offscreen, volume, renderer, viewer); text `include/iv/text/`, `src/text/`
  (shaper, bundled_font); vendored `third_party/harfbuzz/` (pin in VENDORING.md),
  `third_party/fonts/`; shaders `shaders/`; generated `colormap_lut.hpp`; benchmark
  `tools/bench.cpp`; tests `tests/test_*.cpp`.
