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
- **Status:** Planned
- **Goal:** Own the Vulkan boilerplate to stand up a device and render a cleared
  offscreen color image, copy it to host memory, and verify its pixels — no
  window, no swapchain. This proves device/queue/command machinery and gives us
  a deterministic image-readback path that later milestones test against.
- **Done when:**
  - [ ] Vulkan instance created; validation layers enabled in debug builds.
  - [ ] Physical-device selection and logical device + queues + command pool.
  - [ ] An offscreen color image is cleared to a known color and copied to host.
  - [ ] A test reads back the image and asserts the pixels equal the clear color.
  - [ ] Validation layers report no errors across the path.
- **Expected ADRs:**
  - Vulkan object ownership & lifetime model (RAII wrappers; who allocates/frees).
  - Device/queue selection contract (how a device is chosen; what is required).
  - Offscreen render-target format & color convention (e.g. linear vs sRGB).
  - Concurrency baseline (DEV_PROCESS §6, explicit): the threading model and the
    public API's thread-safety contract are stated, not assumed.
- **Tests with teeth:** Readback equals the clear color. Teeth shown by fault
  injection: change the clear color constant (or skip the clear) and the test
  goes red; restore and it greens. Validation-layer cleanliness asserted.
- **Actual ADRs:** _(filled at completion)_

## M3 — Volume Data Model & GPU Upload
- **Status:** Planned
- **Goal:** A public API that accepts a flat `std::complex<float|double>` array
  plus `(nx, ny, nz)`, under a defined memory-layout/indexing convention, and
  uploads it to a 3D texture storing per-voxel `(magnitude, phase)` as `RG32F`.
  Double input has its magnitude/phase computed in double precision on the host,
  then stored as `fp32`. A magnitude-normalization range (auto global-max or
  caller-specified) is established for downstream opacity mapping.
- **Done when:**
  - [ ] Public ingestion API defined (array + dimensions + precision).
  - [ ] Memory-layout/indexing convention fixed (default x-fastest, 0-based).
  - [ ] `(magnitude, phase)` `RG32F` 3D texture populated from the input.
  - [ ] A known small field round-trips: texture readback matches expected
        magnitude and phase within the tolerance policy.
  - [ ] Both `float` and `double` input paths verified.
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
- **Actual ADRs:** _(filled at completion)_

## M4 — Ray-Marching Renderer & Transfer Function
- **Status:** Planned
- **Goal:** Offscreen direct volume rendering of the `(magnitude, phase)`
  texture: a camera with ray/box intersection, front-to-back alpha compositing,
  `abs`→opacity with a live linear/logarithmic toggle, and `arg`→cyclic
  colormap. Verified against analytically known cases via pixel readback.
- **Done when:**
  - [ ] Ray-march compositing shader renders the volume to an offscreen image.
  - [ ] Camera + ray/box intersection produce a correct view of the unit volume.
  - [ ] Linear and logarithmic opacity scaling both selectable and correct.
  - [ ] Cyclic colormap maps `arg ∈ (-π, π]` to color per the colormap ADR.
  - [ ] Known cases verified: single bright voxel of known phase → expected
        color/opacity; uniform-phase field → expected hue; empty field →
        background.
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
- **Actual ADRs:** _(filled at completion)_

## M5 — Interactive Viewer & Performance Contract
- **Status:** Planned
- **Goal:** A thin GLFW-based viewer over the offscreen core: surface +
  swapchain + present loop, orbit/zoom camera controls, and frame pacing —
  meeting interactive framerates for volumes of several hundred voxels per side,
  pinned by a benchmark. (May split per §2.2 if its ADRs exceed ~5.)
- **Done when:**
  - [ ] GLFW window + Vulkan surface + swapchain + present loop run.
  - [ ] Orbit/zoom camera controls drive the existing renderer interactively.
  - [ ] A present loop runs N frames validation-clean.
  - [ ] A benchmark demonstrates the stated FPS target at a stated volume size
        on a stated hardware class.
- **Expected ADRs:**
  - Windowing/surface dependency & platform integration (GLFW; the new
    third-party dependency, §1.1).
  - Swapchain/present contract (format, present mode, resize/recreation policy).
  - Interaction/camera-control API (public surface for the viewer).
  - Performance contract (target FPS, volume size, hardware class, sampling
    budget) with the benchmark that enforces it.
- **Tests with teeth:** Present loop runs validation-clean for N frames;
  benchmark within the ADR-stated bound. Teeth shown by fault injection: a
  deliberately oversampled march step blows the frame budget → red; the
  contracted step → green.
- **Actual ADRs:** _(filled at completion)_
