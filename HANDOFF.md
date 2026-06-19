# HANDOFF.md — imaginaryVolumes

**Last updated:** 2026-06-19 by M4 session (Claude / Opus 4.8)
**Active milestone:** M5 — Interactive viewer & performance contract (M4 complete)

## Current State
**M4 is Complete and locked** (MILESTONES.md § M4; CHANGELOG.md § M4). The volume
now renders: a complex field uploaded by M3 is ray-marched to an image and
verified by pixel readback on the **NVIDIA RTX 4070**.
- **Shader toolchain (ADR-0011, D-0022):** `shaders/ray_march.comp` → `glslc` at
  build (CMake `find_program`, fatal if missing) → embedded into `libiv` by
  `tools/embed_spirv.cmake` (`iv/vk/shaders.hpp`, `makeShaderModule`).
- **Renderer (ADR-0011/0012):** `iv::vk::Renderer` — compute pipeline (1 ray/pixel,
  8×8), samples the volume, writes an `R8G8B8A8_UNORM` storage image, reads it back
  (ADR-0006 path → `ImageReadback`). Fixed pinhole camera (hand-rolled `vec3`, no
  GLM); slab ray/box vs `[0,1]³`; front-to-back `over` + early-ray termination.
  (`include/iv/vk/renderer.hpp`, `src/vk/renderer.cpp`.)
- **Transfer fn (ADR-0013) + colormap (ADR-0014):** linear/log opacity over
  `[minPositive,max]` (log floor `minPositive`; degenerate → 0, no NaN);
  `t=(θ+π)/2π` cyclic; 256-entry twilight LUT (`tools/gen_colormap.py` →
  `include/iv/vk/colormap_lut.hpp`) + analytic HSV, selectable.
- **Refactor:** shared `iv/vk/commands.hpp` (`submitOneShot`, `imageBarrier`); the
  M3 `Volume` upload uses it now (no behavior change).
- ADR-0011…0014 Accepted; D-0021…D-0023 journaled; B-0007 logged; index current.

All green from clean builds: Debug (210 assertions / 34 cases), ASan+UBSan ctest
(both entries), non-Vulkan suite leak-checked. **Four teeth** demonstrated
(CHANGELOG § M4). Committed (M4 implementation commit).

## In Flight (work started, not finished)
- Nothing in flight. M4 is closed. M5 has not been started (no ADRs authored).

## Next Action
Begin **M5 — Interactive viewer & performance contract** at its CONTRACT phase
(MILESTONES § M5). Expected ADRs: the windowing/surface dependency (**GLFW** — a
new third-party dependency, D-0002, §1.1 ⇒ a dependency ADR and a vendoring/
find_package decision under ADR-0001); the swapchain/present contract (format,
present mode, resize/recreation); the interaction/camera-control API (orbit/zoom
driving `RenderParams`); and the performance contract (target FPS, volume size,
hardware class) with an enforcing benchmark. ORIENT first (§3.0); author those
ADRs as Proposed and get them Accepted **before** implementing. (M5 may split per
§2.2 if its ADRs exceed ~5.)

M5 builds on M4: it drives `iv::vk::Renderer` (orbit/zoom update `RenderParams`),
and presents the rendered `R8G8B8A8_UNORM` image to a swapchain (blit/copy — the
compute substrate, D-0021, does not draw to the swapchain directly). Decide
whether the present path enables `synchronization2`/dynamic-rendering (deferred at
D-0016) or stays classic.

## Known-Broken / Blocked
- Nothing broken. No blockers. M5 is gated on authoring + accepting its ADRs.
- VMA still deferred (D-0017); B-0006 open — M5 adds swapchain images; revisit.
- Helper unification: `submitOneShot`/`imageBarrier` are shared (commands.hpp);
  `clearAndReadback` (M2) still has its own inline submit/barriers — fold in if
  touched (not urgent).

## Landmines & Context
- ORIENT before writing (§3.0): this file → DEV_PROCESS → MILESTONES → ADR INDEX
  → DECISIONS. Restate governing ADRs before coding.
- **`glslc` is a required build tool** (ADR-0011/D-0022): configure fails without
  it. Shaders live in `shaders/`; SPIR-V is embedded (regenerated each build). The
  colormap LUT is committed data — regenerate with `tools/gen_colormap.py` (needs
  matplotlib) if the map changes.
- **Renderer/Volume borrow the Context** (device/queue/pool) and must not outlive
  it. `Unique<>` member order is load-bearing (view/image before memory). Debug
  thread-affinity checks guard accessors (ADR-0007).
- **Coordinate convention (ADR-0012):** right-handed, **+Y up**, volume = `[0,1]³`,
  **world position = texture coordinate**, image origin top-left. A wrong axis
  order shows up in the render and ties back to the M3 x-fastest layout.
- **`R32G32_SFLOAT` linear filtering isn't core-mandatory**; the renderer queries
  it and falls back to nearest (`Renderer::volumeLinearFilter()`).
- **Precision paths are not bit-identical** (D-0020): compare a path to a
  same-precision expectation, never float-path to double-path.
- **LSan scoped off for Vulkan-inclusive runs** (D-0015); the validation layer
  (incl. the pNext teardown messenger) is the Vulkan-object-leak gate. Don't "fix"
  driver leaks.
- M2–M4 use **classic 1.0 barriers** (D-0016); no device feature enabled. Revisit
  sync2 at M5 if the present path benefits.
- Vulkan boundary (ADR-0004): include only `iv/vk/vulkan.hpp`; never leak
  `vk::Result`/`VkResult` into consumer signatures (§8.5).
- Rejection tests use the short-circuiting `rejected()` helper (`!has_value() &&
  error().code == c`) — `.error()` on a value-state `std::expected` is UB.
- Test commands: `cmake --build build/debug` then `./build/debug/tests/iv_tests`;
  filter `"[renderer]"` / `"[volume]"`; sanitizer gate
  `-DIV_SANITIZE=address,undefined` then `ctest --test-dir build/asan`; TSan gate
  `-DIV_SANITIZE=thread`, run `iv_tests "[concurrency]~[vk]"`.
  `IV_VULKAN_DEVICE_INDEX` forces a device.

## Pointers
- Governing process: `DEV_PROCESS.md`.
- Milestone arc & M5 scope: `MILESTONES.md` (§ M5).
- Contracts: `docs/adr/INDEX.md` — ADR-0001…0014 Accepted.
- Decisions & rationale: `DECISIONS.md` (D-0001…D-0023), Backlog B-0001…B-0007.
- M1–M4 work + teeth: `CHANGELOG.md`.
- Demo: `examples/iv_render_demo [out_dir]` renders sample complex fields and
  writes PNGs via the owned `examples/png.hpp` (D-0024); outputs to gitignored
  `gallery/` (regenerable, not committed).
- Code: host model `include/iv/volume.hpp`, `src/volume.cpp`; Vulkan
  `include/iv/vk/`, `src/vk/` (commands, memory, shaders, context, offscreen,
  volume, renderer); shaders `shaders/`; generated `colormap_lut.hpp`; tests
  `tests/test_volume.cpp`, `tests/test_vk_*.cpp`, `tests/test_concurrency.cpp`.
