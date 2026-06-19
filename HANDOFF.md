# HANDOFF.md — imaginaryVolumes

**Last updated:** 2026-06-18 by M2 session (Claude / Opus 4.8)
**Active milestone:** M2 — Vulkan headless bring-up (CONTRACT phase)

## Current State
M1 is Complete and locked. **M2 is in its CONTRACT phase (§3.1):** four ADRs are
authored as **Proposed** and awaiting maintainer acceptance:
- **ADR-0004** — Vulkan Binding & Object-Ownership Model
- **ADR-0005** — Instance, Physical-Device & Queue Selection
- **ADR-0006** — Offscreen Render Target, Format & Host-Readback Convention
- **ADR-0007** — Concurrency Baseline

Decisions journaled (`DECISIONS.md` D-0011…D-0014, Backlog B-0006); index current.
**No Vulkan implementation code written yet** — held until the ADRs are Accepted.

Toolchain re-probed (2026-06-18): `find_package(Vulkan)` → 1.4.313 with
`glslc`/`glslangValidator`; `vulkan.h/.hpp/_raii.hpp` present;
`VK_LAYER_KHRONOS_validation` available. **Only device is `llvmpipe` (software,
CPU)** — deterministic, good for headless readback tests; but see the M5 perf
landmine below.

## In Flight (work started, not finished)
- ADR-0004…0007 Proposed, not Accepted. No implementation in progress.

## Next Action
**Acceptance gate.** On maintainer acceptance, flip ADR-0004…0007 to Accepted,
regenerate the index, then IMPLEMENT M2:
1. `include/iv/vk/vulkan.hpp` — sets `VULKAN_HPP_NO_EXCEPTIONS` (+ no-op
   `VULKAN_HPP_ASSERT_ON_RESULT`) then includes `<vulkan/vulkan.hpp>`; link
   `Vulkan::Vulkan` in CMake (find_package(Vulkan)).
2. `vk::Result → iv::Errc` boundary helper (ADR-0004 mapping table).
3. Move-only RAII owner wrappers; instance (+validation/debug-utils in Debug),
   physical-device selection (ranking + `IV_VULKAN_DEVICE_INDEX`, accept software),
   logical device + one graphics queue, command pool (ADR-0004/0005).
4. Offscreen R8G8B8A8_UNORM image + staging buffer; clear→barrier→copy→fence→read;
   `ImageReadback` per ADR-0006 layout.
5. Tests + teeth → `CHANGELOG.md` (M2): readback==clear color (perturb color);
   `IV_VULKAN_DEVICE_INDEX` out of range → `device_unavailable`; validation
   messenger count==0 (force a misuse → nonzero); TSan handler hammer (make
   storage non-atomic → race); Debug thread-affinity assert (cross-thread use →
   handler fires).

## Known-Broken / Blocked
- **Blocker (gate):** M2 implementation held until ADR-0004…0007 Accepted.
- **M5 perf landmine:** llvmpipe is software — it will NOT hit interactive
  framerates for several-hundred³ volumes. M2–M4 correctness is unaffected (and
  benefits from deterministic software rendering), but the M5 performance contract
  must be stated for a real-GPU hardware class and verified on such hardware.
- Nothing else broken; M1 green in Debug and under ASan+UBSan.

## Landmines & Context
- ORIENT before writing (§3.0): this file → DEV_PROCESS → MILESTONES → ADR INDEX
  → DECISIONS. Restate governing ADRs before coding.
- `docs/adr/INDEX.md` is generated — never hand-edit; run
  `tools/regenerate_adr_index.py` (`--check` is a CI gate).
- Vulkan boundary (ADR-0004): centralize macros in `iv/vk/vulkan.hpp`; never let
  `vk::Result`/`VkResult` into consumer-facing signatures (boundary adapter, §8.5);
  expect `-Wconversion`/`-Wsign-conversion` friction on `uint32_t`/`size_t` — use
  explicit casts in boundary code, not blanket suppressions.
- Error/assert contract (ADR-0003): `Result<T>`/`Status`, no own exceptions;
  always-on `IV_ASSERT`; tests intercept aborts via the overridable handler
  (`tests/test_assert.cpp` `HandlerGuard`).
- Teeth convention (ADR-0002): every contract test names the fault it catches;
  per-milestone red→green / fault-injection evidence goes in `CHANGELOG.md` or the
  milestone cannot complete. Concurrency teeth (§2.4) require TSan + enough
  iterations.
- Sanitizer builds: `-DIV_SANITIZE=address,undefined` (gate) and
  `-DIV_SANITIZE=thread` (TSan, for ADR-0007 verification).

## Pointers
- Governing process: `DEV_PROCESS.md`.
- Milestone arc & M2 scope: `MILESTONES.md` (§ M2).
- M2 contracts (Proposed): `docs/adr/0004…0007-*.md`; accepted M1: ADR-0001/0002/0003.
- Decisions & rationale: `DECISIONS.md` (D-0001…D-0014), Backlog B-0001…B-0006.
- M1 work + teeth: `CHANGELOG.md` (§ M1).
- Build entry: top-level `CMakeLists.txt`; tests under `tests/`.
- Git: repository initialized, **no commits yet** — maintainer not yet asked for a
  commit. Offer when appropriate.
