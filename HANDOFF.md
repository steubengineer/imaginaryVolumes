# HANDOFF.md — imaginaryVolumes

**Last updated:** 2026-06-19 by the M5 session (Claude / Opus 4.8)
**Active milestone:** none in flight — **M5 complete**; next is milestone planning
(see Next Action).

## Current State
**M1–M5 are Complete and locked** (MILESTONES.md; CHANGELOG.md). The library
ingests a complex field, uploads it as a 3D RG32F texture, ray-marches it with a
compute pipeline, and now **presents it in an interactive GLFW window** — and a
benchmark pins the performance contract.

- **M5 viewer (ADR-0016/0017/0018):** `iv::vk::Viewer` (in the isolated `iv_viewer`
  target) owns a GLFW window + surface, a presentation-capable `Context`, a swapchain
  (UNORM/`FIFO`/`eTransferDst`), and the M4 `Renderer`. Per frame: acquire →
  `Renderer::recordFrame` (dispatch into an internal storage image, **blit** into the
  swapchain image) → `→ePresentSrcKHR` barrier → submit → present; one frame in
  flight, in-flight fence, **per-image** `renderFinished` semaphore; recreate on
  out-of-date/suboptimal/resize. Input: left-drag orbit, scroll zoom, keys
  Esc/L/C/R (`iv::OrbitCamera`, pure host). `run()` / `runFrames(n)` /
  `requestResize(w,h)`.
- **Presentation `Context` (ADR-0016):** `ContextConfig{instanceExtensions,
  deviceExtensions}` + `Context::create(config)`; headless `create()` unchanged.
- **Renderer present path (ADR-0017):** `recordFrame` + private
  `ensureFrameResources` / `writeComputeDescriptors`; shared `fillUbo`. The M4
  offscreen `render()`+readback path is unchanged.
- **Benchmark (ADR-0019):** headless `iv_bench` — 512³ → 1280×720, warm-up + N=30
  timed `render()`, asserts median ≤ 33.3 ms. **Measured 15.3 ms (~65 FPS) on the
  RTX 4070 → PASS.**
- **CMake:** `find_package(glfw3)` + `option(IV_BUILD_VIEWER …)` (default ON when
  found). `iv`, the tests, and `iv_bench` link **no** GLFW.

All green from clean builds: Debug full suite **235 assertions / 38 cases**;
ASan+UBSan `ctest` (both entries); non-Vulkan suite leak-checked (117/19). M5 teeth
(CHANGELOG § M5): benchmark `--no-early-term --step-mult 8` → red; present-barrier
drop → validation error; pitch-clamp removal → red. ADR-0016…0019 Accepted; index
current (it had been missing the M5 rows on disk — regenerated). D-0026…D-0030
journaled; B-0008 logged.

## In Flight (work started, not finished)
**Nothing in flight.** M5 is complete and committed. The tree builds clean and all
gates are green.

## Next Action — plan the next milestone (M6)
The maintainer's stated next goal (verbatim intent): *"features required to make our
software a usable scientific data plotting library."* Begin DEV_PROCESS at ORIENT →
**CONTRACT**: propose the M6 scope and its ADRs (Proposed → get them Accepted before
implementing). Candidate scope to shape with the maintainer (do not assume — this is
a planning conversation):
- **Quantitatively-correct opacity** — **B-0008**: per-sample opacity is *not*
  corrected for ray step spacing `dt`, so `stepCount` changes the displayed density
  and the early-termination point (D-0030). A scientific tool likely wants density
  invariant to the sampling rate (`α_dt = 1 − (1 − a)^(dt/refStep)`). Extends
  ADR-0013.
- **Spatial reference** — **B-0007**: draw the `[0,1]³` bounding box with axis
  ticks/labels (maintainer-requested at M4 review).
- A **high-level "plot this data" API** (today the viewer is two-step: `create()` →
  build `Volume` from `context()` → `setVolume`); colorbar/phase-legend; value
  probing; explicit value↔world axis mapping/units; saving views.
- Possibly **additional colormaps** (B-0005), a transfer-function editor, slicing.
Pick the slice with the maintainer, write the ADRs, get acceptance, then implement.

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
- **Perf lever / B-0008:** render cost is largely insensitive to `stepCount` because
  per-sample opacity ignores `dt` and early-ray termination caps accumulation at a
  step count independent of N. So the ADR-0019 "8× stepCount" teeth needs
  `--no-early-term` to bite (D-0030). Don't be surprised that more steps ≈ same FPS.
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
