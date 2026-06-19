# HANDOFF.md — imaginaryVolumes

**Last updated:** 2026-06-19 by M2 session (Claude / Opus 4.8)
**Active milestone:** M3 — Volume data model & GPU upload (M2 complete)

## Current State
**M2 is Complete and locked** (MILESTONES.md § M2; CHANGELOG.md § M2). Headless
Vulkan bring-up works end to end and is verified on the **NVIDIA RTX 4070**:
- Binding boundary (`iv/vk/vulkan.hpp`, `result.hpp`, `unique.hpp`) — vulkan.hpp
  in `VULKAN_HPP_NO_EXCEPTIONS` mode, our move-only `Unique<Handle>` RAII,
  `vk::Result → iv::Errc` mapping. Default static dispatch; debug-utils procs
  loaded by hand.
- `iv::vk::Context` — instance, validation messenger (+ pNext teardown gate),
  device selection (ranking, accepts software, `IV_VULKAN_DEVICE_INDEX`), graphics
  queue, command pool.
- `iv::vk::clearAndReadback` + `ImageReadback` — `R8G8B8A8_UNORM` offscreen clear,
  staging copy, fence, host readback at the fixed `(y*w+x)*4` top-left layout.
- ADR-0004…0007 Accepted; D-0011…D-0016 journaled; index current.

All green from clean builds: Debug (83 assertions / 19 cases), ASan+UBSan ctest
(both entries), TSan concurrency gate. Six teeth demonstrated (CHANGELOG § M2).
Committed (M2 implementation commit).

## In Flight (work started, not finished)
- Nothing mid-implementation. M2 closed cleanly; M3 not started.

## Next Action
Begin **M3 — Volume data model & GPU upload** at its CONTRACT phase (§3.1). Author
the M3 ADRs as Proposed and get them Accepted before implementing:
1. Public ingestion API + data-layout convention (flat `std::complex<float|double>`
   + `(nx,ny,nz)`; x-fastest `idx = x + nx*(y + ny*z)`, 0-based — per founding
   default D-0006).
2. Precision policy (compute magnitude/phase in input precision on host; store
   `fp32`; no fp64 GPU path — D-0005).
3. 3D texture format & contents (`(magnitude, phase)` as `RG32F` — D-0004).
4. Magnitude-normalization contract (auto global-max vs caller-set range).
Then implement: ingest → derive (mag, phase) → upload to a 3D `RG32F` image →
read back and verify against a known small field (teeth: wrong index order or
wrong abs/arg formula → red). Reuse the M2 staging/readback machinery.

## Known-Broken / Blocked
- Nothing broken. No blockers.
- **Decide VMA at M3 start (Backlog B-0006):** M3 multiplies allocations (3D
  texture, staging, buffers). Raw `vkAllocateMemory` was fine for M2's single
  image; M3 is the trigger to weigh adopting VMA. A new dependency ⇒ ADR (§1.1).

## Landmines & Context
- ORIENT before writing (§3.0): this file → DEV_PROCESS → MILESTONES → ADR INDEX
  → DECISIONS. Restate governing ADRs before coding.
- **LSan is scoped off for Vulkan-inclusive test runs** (D-0015): the loader +
  validation layer leak unsymbolizable global state at exit. The gate keeps ASan
  (UAF/overflow) + UBSan active, leak-checks the non-Vulkan suite (`~[vk]`), and
  relies on the **validation layer** (incl. the pNext teardown messenger) for
  Vulkan object leaks. Don't "fix" these driver leaks — they aren't ours.
- **Context member order is load-bearing** (`context.hpp`): `validationCount_`
  first (destroyed last — the pNext messenger writes to it during
  `vkDestroyInstance`); children after the instance. Resource cleanup in
  `clearAndReadback` likewise relies on declaration order (memory outlives its
  image/buffer).
- M2 uses **classic 1.0 barriers** (D-0016), not sync2 — no device feature
  enabled. Revisit sync2/dynamic-rendering at M4 if useful.
- Vulkan boundary (ADR-0004): include only `iv/vk/vulkan.hpp`; never let
  `vk::Result`/`VkResult` into consumer-facing signatures (§8.5); explicit casts
  at the boundary, not blanket warning suppressions.
- Test commands: `-DIV_SANITIZE=address,undefined` then
  `ctest --test-dir build/asan`; TSan race-gate: build `-DIV_SANITIZE=thread`,
  run `iv_tests "[concurrency]~[vk]"`. `IV_VULKAN_DEVICE_INDEX` forces a device.

## Pointers
- Governing process: `DEV_PROCESS.md`.
- Milestone arc & M3 scope: `MILESTONES.md` (§ M3).
- Accepted contracts: `docs/adr/INDEX.md` (ADR-0001…0007).
- Decisions & rationale: `DECISIONS.md` (D-0001…D-0016), Backlog B-0001…B-0006.
- M1/M2 work + teeth: `CHANGELOG.md`.
- Vulkan code: `include/iv/vk/`, `src/vk/`; tests `tests/test_vk_*.cpp`,
  `tests/test_concurrency.cpp`.
