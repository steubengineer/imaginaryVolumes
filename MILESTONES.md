# MILESTONES.md — imaginaryVolumes

**Status:** Living (see DEV_PROCESS §2.5). Future milestones may be refined,
reordered, split, or added. Completed milestones are locked: their *Goal*,
*Done when*, and *Actual ADRs* freeze.

**Project:** A C++23 library for volumetric (direct-volume-rendered) plotting of
complex scalar fields. Input is a flat array of `std::complex<float>` or
`std::complex<double>` plus a grid size `(nx, ny, nz)`. Per voxel, `abs(z)` maps
to **opacity** (linear *or* logarithmic scaling) and `arg(z)` maps cyclically to
a **colormap**. Rendering uses a Vulkan backend we largely own, targeting
interactive framerates for volumes of several hundred voxels per side.

**Founding architecture (journaled in DECISIONS.md; contract-fixed by the ADRs
named below):** ray-marched direct volume rendering; the field is uploaded once
as an `(magnitude, phase)` 3D texture (`RG32F`) with the transfer function
(opacity scaling + colormap) applied in-shader; an offscreen renderer is the
core, with a thin GLFW-based interactive viewer layered on top; tests verify the
renderer headlessly via deterministic pixel readback before the window exists.

---

## Milestone Overview

1. **M1** — Build, toolchain & test/sanitizer harness.
2. **M2** — Vulkan headless bring-up (cleared offscreen frame, verified by readback).
3. **M3** — Volume data model & GPU upload (`complex` → `(magnitude, phase)` texture).
4. **M4** — Ray-marching renderer & transfer function (abs→opacity, arg→colormap).
5. **M5** — Interactive viewer (swapchain/GLFW) & performance contract.

---

## M1 — Build, Toolchain & Test/Sanitizer Harness
- **Status:** Complete (2026-06-18) — locked (§2.5).
- **Goal:** A reproducible C++23/GCC build and a verification gate that has
  teeth. `cmake --build` compiles warning-clean under warnings-as-errors; the
  test suite (Catch2) runs under ASan and UBSan; a deliberately seeded failing
  test demonstrates the red→green path of the gate end-to-end.
- **Done when:**
  - [x] CMake project builds with C++23 on system GCC, warnings-as-errors.
  - [x] Catch2 (v3.7.1, vendored amalgamated) wired in; `ctest` runs the suite.
  - [x] ASan + UBSan build configured (`-DIV_SANITIZE=address,undefined`) and
        runs clean (9 cases / 25 assertions).
  - [x] Owned library symbols exist and are exercised by tests (the ADR-0003
        error API and `IV_ASSERT` handler).
  - [x] Recorded red→green / fault-injection transitions demonstrate the gates
        catch real faults (CHANGELOG.md, M1 teeth evidence 1–4).
- **Expected ADRs:**
  - Build & dependency policy (C++23, system GCC, warnings-as-errors, the
    sanitizer gate, and the rule for vetting/admitting third-party deps).
  - Test-framework choice (Catch2) and the teeth-evidence convention (§2.4).
  - Project-wide error/result type and failure semantics (§5: what is UB, what
    throws, what returns an error).
- **Tests with teeth:** Pin that the gate itself fails on a real fault. Teeth
  shown by a recorded red→green: a test asserting the trivial symbol's behavior
  goes red when the symbol is broken, green when restored; and a sanitizer
  smoke test (e.g. a deliberate UB path behind a disabled flag) confirms UBSan
  actually trips.
- **Actual ADRs:** ADR-0001 (build/toolchain/dependency policy), ADR-0002 (test
  framework + teeth-evidence convention), ADR-0003 (error & failure semantics).
  Teeth demonstrated in `CHANGELOG.md` (§ M1).

## M2 — Vulkan Headless Bring-Up
- **Status:** Complete (2026-06-19) — locked (§2.5).
- **Goal:** Own the Vulkan boilerplate to stand up a device and render a cleared
  offscreen color image, copy it to host memory, and verify its pixels — no
  window, no swapchain. This proves device/queue/command machinery and gives us
  a deterministic image-readback path that later milestones test against.
- **Done when:**
  - [x] Vulkan instance created; validation layers enabled in debug builds
        (best-effort, message-capturing).
  - [x] Physical-device selection (ranking, accepts software) and logical device
        + graphics queue + command pool.
  - [x] An offscreen `R8G8B8A8_UNORM` image is cleared to a known color and copied
        to host via a staging buffer.
  - [x] A test reads back the image and asserts every pixel equals the clear
        color (exact UNORM bytes), plus a varying-data `ImageReadback::at` layout
        test and a determinism test.
  - [x] Validation reports no errors across create/use/teardown (pNext messenger
        gate); verified on the NVIDIA RTX 4070.
- **Expected ADRs:**
  - Vulkan object ownership & lifetime model (RAII wrappers; who allocates/frees).
  - Device/queue selection contract (how a device is chosen; what is required).
  - Offscreen render-target format & color convention (e.g. linear vs sRGB).
  - Concurrency baseline (DEV_PROCESS §6, explicit): the threading model and the
    public API's thread-safety contract are stated, not assumed.
- **Tests with teeth:** Readback equals the clear color. Teeth shown by fault
  injection: change the clear color constant (or skip the clear) and the test
  goes red; restore and it greens. Validation-layer cleanliness asserted.
- **Actual ADRs:** ADR-0004 (binding & ownership), ADR-0005 (instance/device/
  queue selection), ADR-0006 (offscreen target & readback), ADR-0007 (concurrency
  baseline). Supporting decisions: D-0011…D-0016. Teeth demonstrated in
  `CHANGELOG.md` (§ M2), evidence 1–6.

## M3 — Volume Data Model & GPU Upload
- **Status:** Complete (2026-06-19) — locked (§2.5).
- **Goal:** A public API that accepts a flat `std::complex<float|double>` array
  plus `(nx, ny, nz)`, under a defined memory-layout/indexing convention, and
  uploads it to a 3D texture storing per-voxel `(magnitude, phase)` as `RG32F`.
  Double input has its magnitude/phase computed in double precision on the host,
  then stored as `fp32`. A magnitude-normalization range (auto global-max or
  caller-specified) is established for downstream opacity mapping.
- **Done when:**
  - [x] Public ingestion API defined (array + dimensions + precision):
        `iv::vk::Volume::create(Context&, span<const complex<float|double>>,
        GridDims, VolumeOptions)`.
  - [x] Memory-layout/indexing convention fixed (x-fastest, 0-based; `iv::GridDims`,
        pinned by `static_assert`).
  - [x] `(magnitude, phase)` `R32G32Sfloat` 3D texture populated from the input.
  - [x] A known small field round-trips: texture readback matches the expected
        magnitude and phase **bit-exactly** (each precision path vs its own
        input-precision-then-narrow expectation; D-0020 refined the original
        "tolerance policy" wording to exact equality).
  - [x] Both `float` and `double` input paths verified (NVIDIA RTX 4070).
- **Expected ADRs:**
  - Public input API & data-layout convention (a §1.1 interface *and* a §5
    convention: flat-array indexing, dimension semantics, index width).
  - Precision policy (derived quantities computed in input precision; `fp32`
    texture storage; no fp64 GPU path).
  - 3D texture format & contents (what each channel holds; `RG32F`).
  - Magnitude-normalization contract (auto vs caller-specified range; the
    numerical tolerance policy, §5).
- **Tests with teeth:** Upload a small analytic field and assert readback
  magnitude/phase. Teeth shown by fault injection: a wrong index order
  (`x + nx*(y + ny*z)` vs a transposed form) or a wrong `abs`/`arg` formula
  makes the readback diverge → red; correct form → green.
- **Actual ADRs:** ADR-0008 (ingestion API & data-layout), ADR-0009 (GPU volume
  texture: format/derived contents/precision/upload), ADR-0010 (magnitude-range
  metadata). Supporting decisions: D-0017 (raw allocation), D-0018 (ingestion/
  upload contract), D-0019 (range metadata), D-0020 (precision-path divergence).
  Teeth demonstrated in `CHANGELOG.md` (§ M3), evidence 1–8.

## M4 — Ray-Marching Renderer & Transfer Function
- **Status:** Complete (2026-06-19) — locked (§2.5).
- **Goal:** Offscreen direct volume rendering of the `(magnitude, phase)`
  texture: a camera with ray/box intersection, front-to-back alpha compositing,
  `abs`→opacity with a live linear/logarithmic toggle, and `arg`→cyclic
  colormap. Verified against analytically known cases via pixel readback.
- **Done when:**
  - [x] Ray-march compute shader renders the volume to an offscreen image
        (`iv::vk::Renderer`, ADR-0011).
  - [x] Camera + ray/box intersection produce a correct view of the unit volume
        (silhouette: box-hitting pixels render; others read background).
  - [x] Linear and logarithmic opacity scaling both selectable and correct
        (including the degenerate-range and `log(0)` handling, ADR-0013).
  - [x] Cyclic colormap maps `arg` to color per ADR-0014 (twilight LUT default +
        selectable HSV), seam at ±π.
  - [x] Known cases verified by pixel readback: empty field → background;
        uniform-phase field → expected hue (HSV cyan at φ=0); and front-to-back
        order, linear/log, and the colormap mapping each pinned by fault injection
        (CHANGELOG § M4). (The original "single bright voxel" case was generalized
        to the stronger analytic uniform-field cases.)
- **Expected ADRs:**
  - Rendering technique & compositing model (DVR, sampling step, early-ray
    termination, front-to-back order).
  - Transfer-function contract (linear/log opacity formulas, normalization,
    near-zero/`log(0)` handling).
  - Cyclic colormap definition (the exact `arg`→color mapping; default
    perceptually-uniform map plus selectable HSV hue wheel).
  - Camera & coordinate-frame/handedness/up-axis convention (§5).
- **Tests with teeth:** Known input → known pixel. Teeth shown by fault
  injection: flip compositing order (front-to-back ↔ back-to-front), perturb the
  colormap phase offset, or swap linear/log — each makes a known-case pixel
  diverge → red; restore → green.
- **Actual ADRs:** ADR-0011 (rendering substrate & shader toolchain), ADR-0012
  (camera, ray/box & compositing), ADR-0013 (opacity transfer function), ADR-0014
  (cyclic phase colormap). Supporting decisions: D-0021 (compute substrate),
  D-0022 (build-time glslc / embedded SPIR-V), D-0023 (coordinate frame & DVR
  convention). Teeth demonstrated in `CHANGELOG.md` (§ M4), evidence 1–4.

## M5 — Interactive Viewer & Performance Contract
- **Status:** Complete (2026-06-19) — locked (§2.5).
- **Goal:** A thin GLFW-based viewer over the offscreen core: surface +
  swapchain + present loop, orbit/zoom camera controls, and frame pacing —
  meeting interactive framerates for volumes of several hundred voxels per side,
  pinned by a benchmark. (May split per §2.2 if its ADRs exceed ~5.)
- **Done when:**
  - [x] GLFW window + Vulkan surface + swapchain + present loop run
        (`iv::vk::Viewer`; ADR-0016/0017).
  - [x] Orbit/zoom camera controls drive the existing renderer interactively
        (`iv::OrbitCamera`, left-drag/scroll/keys; ADR-0018).
  - [x] A present loop runs N frames validation-clean (`iv_view --frames 30`,
        validation-CLEAN incl. a forced swapchain recreation; ADR-0017).
  - [x] A benchmark demonstrates the stated FPS target at a stated volume size on
        a stated hardware class (`iv_bench`: median 15.3 ms / ~65 FPS at 512³ →
        1280×720 on the RTX 4070; ADR-0019).
- **Tests with teeth:** Present loop runs validation-clean for N frames; benchmark
  within the ADR-stated bound. **Demonstrated** (CHANGELOG § M5): benchmark teeth
  via `--no-early-term --step-mult 8` → median 34.3 ms → red (plain 8× does not
  bite because early-ray termination caps cost independent of `stepCount` — D-0030 /
  B-0008); present-path teeth via dropping the `→ePresentSrcKHR` barrier → validation
  error; camera-clamp teeth via removing the pitch clamp → red.
- **Actual ADRs:** ADR-0016 (windowing/GLFW & presentation Context), ADR-0017
  (swapchain & present loop), ADR-0018 (interaction & camera control), ADR-0019
  (performance contract & benchmark). Decisions D-0026…D-0030; Backlog B-0008.
- **Note:** completed exactly as scoped (4 ADRs, ≤ ~5 per §2.2; no split).

## M6 — Text & Annotation Foundation
- **Status:** Planned (CONTRACT in progress, 2026-06-19).
- **Goal:** The rendering foundation for *publication-quality, quantitatively
  legible* plots: (a) a 2D screen-space **overlay** pass (the project's first
  graphics pipeline) composited over the compute-rendered volume, working
  identically headless and windowed; (b) **crisp Unicode text** via vendored
  **HarfBuzz** shaping + **libharfbuzz-gpu** (Slug algorithm) resolution-independent
  GPU glyph rendering; and (c) **opacity correction** so displayed density is
  invariant to `stepCount` (B-0008). This is the substrate M7 builds annotations on.
  LaTeX math is explicitly **deferred** to a later milestone; M6 labels are
  text/Unicode. (First half of the "usable scientific plotting library" goal; split
  from M7 per §2.2.)
- **Done when:**
  - [ ] Opacity is corrected for ray step spacing: the rendered density of a fixed
        field is invariant to `stepCount` (within tolerance) (B-0008; extends
        ADR-0013).
  - [ ] A 2D overlay (lines + textured quads) composites over the volume render in
        both `Renderer::render()` (headless) and the `Viewer`.
  - [ ] HarfBuzz (vendored, pinned) shapes a Unicode string to positioned glyphs;
        `libharfbuzz-gpu` (Slug) renders them resolution-independently on the GPU.
  - [ ] A demo/headless image shows a crisp Unicode text string + a line/box drawn
        over the volume, legible across a zoom range.
- **Expected ADRs:**
  - Opacity correction (`dt`-correct transfer function; extends ADR-0013).
  - 2D annotation/overlay substrate (first graphics pipeline; compositing over the
    compute output; headless + windowed).
  - Vendored **HarfBuzz** dependency + Unicode text shaping (the new third-party
    dependency, §1.1: vendoring mechanism, pinned commit, license, build).
  - GPU glyph rendering via **libharfbuzz-gpu** (Slug); its GLSL shaders through the
    existing glslc→SPIR-V→embed toolchain (ADR-0011/D-0022).
- **Tests with teeth:** opacity-correction invariance test (teeth: revert to
  uncorrected α → density changes with `stepCount` → red); overlay composited
  validation-clean (teeth: a deliberately wrong layout/barrier → validation error);
  a known-string glyph-coverage/render test (teeth: perturb shaping positions or the
  Slug encode → the rendered text pixels diverge from the reference → red).
- **Actual ADRs:** _(filled at completion)_

## M7 — Quantitative Plot Annotations
- **Status:** Planned.
- **Goal:** Turn the M6 foundation into a labeled, publication-quality plot: the
  volume **bounding box** with **ticked, labeled axes** (B-0007), a **legend/colorbar**
  (phase color wheel + magnitude scale), a caller-supplied **coordinate range / units
  / label** model (map the voxel grid to physical coordinates), and the public API to
  set titles/axis/legend label strings (Unicode; LaTeX still deferred). (Second half
  of the "usable scientific plotting library" goal.)
- **Done when:**
  - [ ] Caller sets per-axis coordinate ranges + unit/label strings; the render maps
        the `[0,1]³` grid to those physical coordinates.
  - [ ] Bounding box + ticked axes with numeric tick labels render over the volume.
  - [ ] A legend/colorbar shows the phase→color wheel and the magnitude→opacity scale
        with labeled bounds.
  - [ ] A high-level "plot this field" path produces a fully labeled image headless
        and in the viewer.
- **Expected ADRs:**
  - Coordinate / units / scene-annotation model + public label API.
  - Bounding box + axis ticks/labels (B-0007).
  - Legend / colorbar (phase wheel + magnitude scale).
  - (Possibly) a high-level convenience API over the two-step viewer/volume setup.
- **Tests with teeth:** coordinate-mapping test (teeth: wrong axis mapping → tick
  positions diverge → red); legend/colorbar correctness vs. the transfer
  function/colormap (teeth: mismatch → red). Refined at M7 CONTRACT.
- **Actual ADRs:** _(filled at completion)_
