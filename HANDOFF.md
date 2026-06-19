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
  stepCount"). Full suite **245/39**; ASan+UBSan green.

## In Flight (work started, not finished) — M6 IMPLEMENT
ADR-0020 and ADR-0021 are done. **Two ADRs remain** (order: 0022 → 0023; 0023 needs
both 0021's overlay and 0022's shaping):
- **✅ ADR-0021 — 2D overlay substrate (DONE, in this session's 2nd M6 commit).**
  First graphics pipeline: render target has `eColorAttachment`; a classic render
  pass (`loadOp = eLoad`, alpha blend) draws line/triangle geometry over the volume
  image in both `render()` (→ readback) and `recordFrame()` (→ blit). Public
  `iv::vk::Overlay {lines, triangles, transform}`; `render()`/`recordFrame()` take an
  optional overlay; `Viewer::overlay()` drives it. Shaders `overlay.vert/.frag`.
  Headless composite test + teeth (disable blend → red); `iv_view --frames` overlay
  validation-clean. **For glyphs (ADR-0023), emit quads into `Overlay::triangles`;
  the camera→clip `transform` (world-space box/axes) is M7's to fill.**
- **ADR-0022 — vendor HarfBuzz + shaping.** Vendor into `third_party/harfbuzz/` at a
  **pinned commit** (incl. experimental `libharfbuzz-gpu`); build minimal (no
  ICU/GLib/Cairo/FreeType), `SYSTEM` includes, **not** under `-Werror`; wrap behind
  `iv::text::Shaper` (UTF-8+font+size → positioned glyphs; no HB type in public API).
  Bundle **NCM GFL** face(s) (e.g. `NewCMSans10-Regular`, ~0.6 MB) under
  `third_party/` with license; **exclude** the GPL subset (`NewCM10-Regular`,
  `NewCMUncial*`, `*Devanagari`). A text build gate keeps the core text-free.
  (Re-fetch faces: `https://mirrors.ctan.org/fonts/newcomputermodern.zip`, 34 MB,
  `otf/` dir; download via `python3 urllib` — `curl` is absent in the sandbox.)
- **ADR-0023 — Slug glyph rendering.** Encode glyph outlines with `libharfbuzz-gpu`
  (cached per `(font, glyphId)`); draw quads in the ADR-0021 overlay with its **GLSL**
  Slug shaders compiled through our `glslc`→SPIR-V→embed toolchain (ADR-0011/D-0022).
  Documented fallback: self-baked MSDF if the experimental API is unworkable.

## Next Action (continue M6 IMPLEMENT)
**Implement ADR-0022 (vendor HarfBuzz + Unicode shaping)** — vendor at a pinned commit
into `third_party/harfbuzz/` (incl. experimental `libharfbuzz-gpu`), build minimal
(no ICU/GLib/Cairo/FreeType) as a static lib not under `-Werror`, wrap behind
`iv::text::Shaper`; bundle an NCM GFL face. Then ADR-0023 (Slug glyphs as overlay
triangles). Verify each (shaping reference / glyph coverage + teeth), then RECORD
(CHANGELOG § M6, MILESTONES M6 → Complete, HANDOFF) and the final M6 commit. Commit at
verified checkpoints (committed so far: 53d7c84 = contract + ADR-0020; the 2nd M6
commit = ADR-0021).

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
- Milestone arc: `MILESTONES.md` (M1–M5 complete; M6 to be planned).
- Contracts: `docs/adr/INDEX.md` — ADR-0001…0019 Accepted (ADR-0009 superseded by
  ADR-0015).
- Decisions & rationale: `DECISIONS.md` (D-0001…D-0030), Backlog B-0001…B-0008.
- M1–M5 work + teeth: `CHANGELOG.md`.
- Demos: `examples/iv_render_demo [out_dir]` (offscreen PNGs via the owned
  `examples/png.hpp`, D-0024; gitignored `gallery/`); `iv_view` (interactive
  viewer); `iv_bench` (perf).
- Code: host model `include/iv/volume.hpp`, `include/iv/orbit_camera.hpp`,
  `src/volume.cpp`; Vulkan `include/iv/vk/`, `src/vk/` (commands, memory, shaders,
  context, offscreen, volume, renderer, viewer); shaders `shaders/`; generated
  `colormap_lut.hpp`; benchmark `tools/bench.cpp`; tests `tests/test_*.cpp`.
