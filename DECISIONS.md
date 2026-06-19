# DECISIONS.md — imaginaryVolumes

This is the decision journal (DEV_PROCESS §2.8): architecturally significant
choices and the rationale that drove them, plus the Backlog (roads not taken and
review concerns not yet addressed). It is lighter-weight than an ADR; choices
with public-contract impact (§1.1) *also* get an ADR, referenced here.

## Decision Log
(Newest first. The founding set, D-0001…D-0008, was all decided on 2026-06-18
during project initiation; D-0009…D-0010 were added during M1's CONTRACT phase
the same day. Future entries prepend above.)

### D-0014 — Concurrency baseline: single-threaded, not thread-safe
- **Date / milestone:** 2026-06-18 / M2 (CONTRACT)
- **Choice:** Thread-safe public API now vs. a single-threaded baseline vs.
  leaving concurrency unstated.
- **Decision:** Public API is single-threaded and not thread-safe; no internal
  threads in M2; Debug thread-affinity asserts; host reads gated on fences; the
  atomic assert handler is the sole cross-thread exception. Output is bitwise
  deterministic per device.
- **Rationale:** §6 requires an explicit declaration; races are made
  unrepresentable by sharing nothing; locking would be premature cost.
- **Contract impact:** ADR-0007 (Proposed).
- **Deferred alternatives:** future async/multi-thread work gets its own ADR.

### D-0013 — Offscreen target format & host-readback convention
- **Date / milestone:** 2026-06-18 / M2 (CONTRACT)
- **Choice:** Readback target format and pixel layout (UNORM vs sRGB vs float;
  origin/packing).
- **Decision:** `R8G8B8A8_UNORM`, tightly packed, top-left origin, row-major,
  pixel `(x,y)` at byte `(y*w+x)*4`, channels R,G,B,A; image
  `eColorAttachment|eTransferSrc|eTransferDst`; staging buffer for readback.
- **Rationale:** UNORM-linear gives bit-exact, implementation-independent pixel
  verification (real teeth); top-left is Vulkan-native.
- **Contract impact:** ADR-0006 (Proposed).
- **Deferred alternatives:** float/HDR readback if M4 needs it; → Backlog B-0006
  (VMA allocator).

### D-0012 — Instance, physical-device & queue selection
- **Date / milestone:** 2026-06-18 / M2 (CONTRACT)
- **Choice:** Device requirements and selection strategy; validation policy;
  API baseline.
- **Decision:** Vulkan 1.3 baseline; rank discrete>integrated>virtual>cpu (accept
  software — only llvmpipe exists here), require a graphics queue family,
  `IV_VULKAN_DEVICE_INDEX` override; validation layer + debug messenger in Debug
  (best-effort), captured so tests can assert cleanliness; one graphics queue.
- **Rationale:** Must run on the software-only dev host yet prefer real GPUs;
  capturing validation messages makes "validation-clean" testable.
- **Contract impact:** ADR-0005 (Proposed).
- **Deferred alternatives:** dedicated transfer/compute queue later if needed.

### D-0011 — Vulkan binding & object-ownership model
- **Date / milestone:** 2026-06-18 / M2 (CONTRACT)
- **Choice:** C API + own RAII vs. Vulkan-Hpp (no-exceptions) + own RAII vs.
  vk::raii. (Maintainer decision.)
- **Decision:** Vulkan-Hpp (`vulkan.hpp`) with `VULKAN_HPP_NO_EXCEPTIONS`,
  result-returning, wrapped in our own move-only single-owner RAII types; default
  dispatch; a boundary helper maps `vk::Result`→`iv::Errc`.
- **Rationale:** Type-safe and exception-free (fits ADR-0003); explicit, auditable
  ownership; vk::raii's exception-coupled ctors conflict with ADR-0003.
- **Contract impact:** ADR-0004 (Proposed).
- **Deferred alternatives:** dynamic dispatcher if a future extension needs it.

### D-0010 — Build/toolchain/dependency policy
- **Date / milestone:** 2026-06-18 / M1 (CONTRACT)
- **Choice:** Warning strictness, build configs, sanitizer wiring, and how third-
  party deps are acquired (C++23/system-GCC were maintainer-fixed givens).
- **Decision:** CMake (≥3.25), ISO C++23 (extensions off), GCC≥13; a strict
  mandatory warning set with `-Werror`; Debug/Release with `IV_ASSERT` always on;
  ASan+UBSan gate; deps via `find_package` (system) or vendored+pinned (small
  libs), no build-time network; new dep ⇒ ADR.
- **Rationale:** Satisfies §7 gates; vendoring keeps builds offline/reproducible;
  strict warnings surface conversions at the Vulkan boundary; CMake is the
  least-friction path to `find_package(Vulkan)`/GLFW.
- **Contract impact:** ADR-0001 (Proposed).
- **Deferred alternatives:** Clang/CI-matrix and FetchContent-for-all — not
  backlogged (supersedable later if multi-compiler CI is wanted).

### D-0009 — Error & failure model: std::expected + always-on IV_ASSERT, no exceptions
- **Date / milestone:** 2026-06-18 / M1 (CONTRACT)
- **Choice:** Exceptions vs. `std::expected` for recoverable failures; how to keep
  boundary contracts alive in Release; the assertion mechanism.
- **Decision:** Recoverable failures return `std::expected<T, iv::Error>`; no
  exceptions as a control channel (only `std::bad_alloc` may propagate, fatal);
  contract violations abort via an always-on, overridable `IV_ASSERT` (so
  precondition misuse is defined-abort, not UB, and is unit-testable).
- **Rationale:** Exception-free propagation suits the GPU/perf hot paths and is
  C++23-native; `assert()` would vanish under `-DNDEBUG`, so a custom always-on
  macro is required; an overridable handler makes abort paths testable without
  death tests.
- **Contract impact:** ADR-0003 (Proposed).
- **Deferred alternatives:** none material.

### D-0001 — Viewer model: offscreen core + optional interactive viewer
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M2 & M5
- **Choice:** Offscreen-only, library-owns-window, or both.
- **Decision:** Both — the offscreen renderer is the core; a thin GLFW-based
  interactive viewer layers on top.
- **Rationale:** Offscreen rendering is deterministic and embeddable and gives
  contract tests real teeth via pixel readback (§2.4); a separate viewer keeps
  the windowing dependency out of the core and off the test path.
- **Contract impact:** ADR pending — public API surface partitioned across
  M2 (offscreen core) and M5 (viewer).
- **Deferred alternatives:** none (the alternatives are subsumed by "both").

### D-0002 — Windowing/surface backend: GLFW
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M5
- **Choice:** GLFW, SDL3, or direct xcb/Wayland for window + Vulkan surface + input.
- **Decision:** GLFW.
- **Rationale:** Tiny, battle-tested, cross-platform Vulkan-surface + input
  support; lets us spend our owned-boilerplate budget on Vulkan rather than
  windowing and the X11/Wayland split.
- **Contract impact:** ADR pending in M5 (new third-party dependency, §1.1).
- **Deferred alternatives:** → Backlog B-0001 (direct xcb/Wayland), B-0002 (SDL3).

### D-0003 — Test framework: Catch2 (single-header)
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M1
- **Choice:** Catch2 single-header, a minimal custom harness, or GoogleTest.
- **Decision:** Catch2 (single-header), dev-only dependency.
- **Rationale:** Good ergonomics for the red→green / fault-injection discipline
  with a minimal footprint; consistent with the minimal-deps ethos while not
  reinventing assertion/reporting machinery.
- **Contract impact:** ADR-0002 (Proposed) — dev dependency + teeth-evidence convention.
- **Deferred alternatives:** → Backlog B-0003 (minimal custom harness).

### D-0004 — Texture stores derived (magnitude, phase); transfer function in-shader
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M3 & M4
- **Choice:** Upload raw complex and derive in-shader, vs. upload precomputed
  `(magnitude, phase)` and apply opacity scaling + colormap in-shader.
- **Decision:** Upload precomputed `(magnitude, phase)` as `RG32F`; apply
  scaling and colormap in the fragment/compute path.
- **Rationale:** Single upload; linear/log opacity and colormap selection become
  free runtime toggles (uniforms) with no re-upload; keeps the GPU-resident
  representation independent of the input precision.
- **Contract impact:** ADR pending in M3 (texture format/contents) and M4
  (transfer function).
- **Deferred alternatives:** none material.

### D-0005 — Precision policy: derive in input precision, store fp32, no fp64 GPU path
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M3
- **Choice:** Keep a double-precision path on the GPU vs. downconvert.
- **Decision:** Compute magnitude/phase in the input's precision on the host,
  store `fp32` in the texture; rendering is `fp32`. No fp64 GPU path.
- **Rationale:** Vulkan cannot sample fp64 textures; opacity/color resolution
  does not need fp64. Computing the reduction (abs/arg) in input precision before
  downconversion preserves accuracy where it matters.
- **Contract impact:** ADR pending in M3 (precision policy).
- **Deferred alternatives:** → Backlog B-0004 (fp64 compute path).

### D-0006 — Flat-array memory layout: x-fastest, 0-based
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M3
- **Choice:** Index ordering and base for the flat `(nx, ny, nz)` input array.
- **Decision:** `idx = x + nx*(y + ny*z)`, x-fastest, 0-based.
- **Rationale:** The overwhelmingly common convention for an "nx × ny × nz" flat
  buffer; matches natural 3D-texture row ordering on upload.
- **Contract impact:** ADR pending in M3 (a §5 convention with public surface).
- **Deferred alternatives:** none; caller-overridable strides may be revisited
  if needed (not committed).

### D-0007 — Phase colormap: perceptually-uniform cyclic by default, HSV selectable
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M4
- **Choice:** Classic HSV hue wheel vs. a perceptually-uniform cyclic map.
- **Decision:** Default to a perceptually-uniform cyclic map (twilight/phase
  style); keep the classic HSV hue wheel selectable (traditional domain coloring).
- **Rationale:** Perceptually-uniform cyclic maps avoid the misleading
  brightness banding of HSV while preserving the cyclic phase mapping; HSV
  retained for familiarity/compatibility with domain-coloring conventions.
- **Contract impact:** ADR pending in M4 (exact `arg`→color mapping).
- **Deferred alternatives:** → Backlog B-0005 (additional colormaps).

### D-0008 — Rendering technique: ray-marched DVR, verified headlessly first
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M2–M4
- **Choice:** Direct volume rendering technique and verification strategy.
- **Decision:** Ray-marched DVR with front-to-back alpha compositing; verify the
  renderer offscreen via deterministic pixel readback before adding the window.
- **Rationale:** DVR maps cleanly onto the abs→opacity / arg→color transfer
  function and onto interactive framerates; headless-first keeps contract tests
  deterministic and decoupled from windowing.
- **Contract impact:** ADR pending in M4 (compositing model).
- **Deferred alternatives:** none material at this stage.

---

## Backlog

### B-0001 — Direct xcb/Wayland windowing (zero windowing dependency)
- **Origin:** D-0002 (road not taken).
- **What:** Own the platform windowing/surface code directly instead of GLFW.
- **Why deferred:** Substantial platform-specific boilerplate and the X11/Wayland
  split; not worth it while GLFW satisfies the minimal-deps goal.
- **Revisit when:** A hard zero-third-party-dependency requirement emerges.
- **Contract link:** would supersede the M5 windowing-dependency ADR.

### B-0002 — SDL3 windowing/input backend
- **Origin:** D-0002 (road not taken).
- **What:** Use SDL3 instead of GLFW.
- **Why deferred:** Heavier than GLFW; its extra subsystems (gamepad, audio)
  aren't needed.
- **Revisit when:** Broader input/multimedia needs appear.
- **Contract link:** would supersede the M5 windowing-dependency ADR.

### B-0003 — Minimal custom test harness (zero test dependency)
- **Origin:** D-0003 (road not taken).
- **What:** Replace Catch2 with an owned minimal assertion/reporting harness.
- **Why deferred:** Catch2's footprint is acceptable and its ergonomics aid the
  teeth discipline.
- **Revisit when:** Catch2's build cost or footprint becomes objectionable.
- **Contract link:** would supersede the M1 test-framework ADR.

### B-0004 — fp64 GPU compute path
- **Origin:** D-0005 (road not taken).
- **What:** Carry double precision onto the GPU for derived quantities.
- **Why deferred:** Vulkan can't sample fp64 textures; no demonstrated need.
- **Revisit when:** Rendering reveals precision artifacts traceable to `fp32`
  derived storage.
- **Contract link:** would supersede the M3 precision-policy ADR.

### B-0005 — Additional colormaps beyond default + HSV
- **Origin:** D-0007 (road not taken).
- **What:** Offer further cyclic colormaps (and/or caller-supplied maps).
- **Why deferred:** Two maps cover the immediate need; more is scope creep now.
- **Revisit when:** Users request specific additional maps.
- **Contract link:** would extend the M4 colormap ADR.

### B-0006 — Vulkan Memory Allocator (VMA) for device memory
- **Origin:** D-0013 / ADR-0006 (road not taken).
- **What:** Adopt AMD's VMA (single-header) instead of raw `vkAllocateMemory`.
- **Why deferred:** M2 needs only one image + one staging buffer; raw allocation
  is adequate and avoids a new dependency before it pays off.
- **Revisit when:** M3 — allocations proliferate (3D textures, staging, buffers).
- **Contract link:** a new dependency ADR (§1.1) + would amend ADR-0006's memory
  section.
