# HANDOFF.md — imaginaryVolumes

**Last updated:** 2026-06-19 by M3 session (Claude / Opus 4.8)
**Active milestone:** M4 — Ray-marching renderer & transfer function (M3 complete)

## Current State
**M3 is Complete and locked** (MILESTONES.md § M3; CHANGELOG.md § M3). A complex
field can now be ingested and uploaded to the GPU as a 3D texture, verified by
bit-exact readback on the **NVIDIA RTX 4070**:
- **Host data model** (`iv/volume.hpp`, `src/volume.cpp`): `iv::GridDims`
  (x-fastest, 0-based `index`/`count`, 64-bit; pinned by `static_assert`),
  `iv::MagnitudeRange`, `iv::VolumeOptions`; validators
  (`validateGrid`/`validateShape`/`validateOptions`); `iv::deriveField<T>` —
  per-voxel `(|z|, arg z)` derived in input precision, narrowed to fp32, with the
  auto magnitude range (`minPositive` excludes zeros).
- **Shared memory helper** (`iv/vk/memory.hpp`, `src/vk/memory.cpp`):
  `findMemoryType` + `allocateAndBindImage`/`allocateAndBindBuffer` (D-0017,
  generalized from M2; `clearAndReadback` now uses them). Raw allocation; VMA
  deferred (B-0006).
- **GPU volume** (`iv/vk/volume.hpp`, `src/vk/volume.cpp`): `iv::vk::Volume` —
  move-only 3D `R32G32Sfloat` (R = raw magnitude, G = phase ∈ [−π,π]); staging +
  `copyBufferToImage`; classic 1.0 barriers (D-0016); rests in
  `eShaderReadOnlyOptimal` + a sampling view (sampler → M4); `readback()`
  (bit-exact); `magnitudeRange()` / `autoMagnitudeRange()`; float & double
  overloads.
- ADR-0008/0009/0010 Accepted; D-0017…D-0020 journaled; index current.

All green from clean builds: Debug (169 assertions / 29 cases), ASan+UBSan ctest
(both entries), non-Vulkan suite leak-checked (`~[vk]`). **Eight teeth** demonstrated
(CHANGELOG § M3). Not yet committed (see Next Action / your call on committing).

## In Flight (work started, not finished)
- Nothing in flight. M3 is closed. M4 has not been started (no ADRs authored).

## Next Action
Begin **M4 — Ray-marching renderer & transfer function** at its CONTRACT phase.
Per MILESTONES § M4, the expected ADRs are: rendering technique & compositing
model (ray-marched DVR, step size, early-ray termination, front-to-back); the
transfer-function contract (linear/log opacity over `[minPositive, max]`,
near-zero / `log(0)` handling — consumes ADR-0010's range); the cyclic colormap
(`arg`→color: perceptually-uniform default + selectable HSV, per D-0007); and the
camera & coordinate-frame/handedness/up-axis convention. ORIENT first (§3.0),
then author those ADRs as Proposed and get them Accepted **before** implementing.

M4 builds directly on M3: it samples the `iv::vk::Volume` (create its **sampler**
now — deferred from M3), renders into the M2 `clearAndReadback` offscreen target
(`R8G8B8A8_UNORM`), and verifies known cases by pixel readback. Consider whether
sync2 / dynamic rendering is worth enabling here (deferred at D-0016).

## Known-Broken / Blocked
- Nothing broken. No blockers. M4 implementation is gated on authoring + accepting
  its ADRs (not yet written).
- VMA still deferred (D-0017); B-0006 remains open — M4 adds a sampler + pipeline,
  so revisit whether allocations now justify it.

## Landmines & Context
- ORIENT before writing (§3.0): this file → DEV_PROCESS → MILESTONES → ADR INDEX
  → DECISIONS. Restate governing ADRs before coding.
- **`iv::vk::Volume` borrows the Context's device/queue/pool** and must not
  outlive its Context (lifetime precondition; ADR-0009). Its `Unique<>` members
  are ordered memory→image→view so the view/image are destroyed before the memory
  is freed (load-bearing, like M2's `clearAndReadback` and `Context`).
- **Precision is not bit-identical across float/double input** (D-0020): each path
  is bit-exact to its *own* input-precision-then-narrow expectation; they can
  differ by ≤1 ULP. Tests must compare a path to a same-precision expectation,
  never float-path to double-path.
- **The x-fastest layout is pinned by `static_assert`** in `iv/volume.hpp`; a
  transposed `GridDims::index` is a compile error. Tight-packed `copyBufferToImage`
  (`bufferRowLength = bufferImageHeight = 0`) relies on that layout.
- **LSan is scoped off for Vulkan-inclusive test runs** (D-0015): loader +
  validation layer leak unsymbolizable global state at exit. ASan/UBSan stay
  active; `~[vk]` is leak-checked; the **validation layer** (incl. the pNext
  teardown messenger) is the Vulkan-object-leak gate. Don't "fix" driver leaks.
- **Context member order is load-bearing** (`context.hpp`): `validationCount_`
  first (destroyed last — the pNext messenger writes it during
  `vkDestroyInstance`); children after the instance.
- M2/M3 use **classic 1.0 barriers** (D-0016), not sync2 — no device feature
  enabled. Revisit at M4 if useful.
- Vulkan boundary (ADR-0004): include only `iv/vk/vulkan.hpp`; never let
  `vk::Result`/`VkResult` into consumer-facing signatures (§8.5); explicit casts
  at the boundary, not blanket warning suppressions.
- Rejection tests use a short-circuiting `rejected()` helper (`!has_value() &&
  error().code == c`): calling `.error()` on a value-state `std::expected` is UB,
  so guard with `has_value()` first (matters when fault-injecting validators).
- Test commands: `cmake --build build/debug` then `./build/debug/tests/iv_tests`;
  sanitizer gate `-DIV_SANITIZE=address,undefined` then `ctest --test-dir
  build/asan`; TSan race-gate: build `-DIV_SANITIZE=thread`, run
  `iv_tests "[concurrency]~[vk]"`. `IV_VULKAN_DEVICE_INDEX` forces a device.
  Filter volume tests with `iv_tests "[volume]"`.

## Pointers
- Governing process: `DEV_PROCESS.md`.
- Milestone arc & M4 scope: `MILESTONES.md` (§ M4).
- Accepted contracts: `docs/adr/INDEX.md` (ADR-0001…0010).
- Decisions & rationale: `DECISIONS.md` (D-0001…D-0020), Backlog B-0001…B-0006.
- M1/M2/M3 work + teeth: `CHANGELOG.md`.
- Code: host model `include/iv/volume.hpp`, `src/volume.cpp`; Vulkan
  `include/iv/vk/`, `src/vk/` (memory, context, offscreen, volume); tests
  `tests/test_volume.cpp`, `tests/test_vk_*.cpp`, `tests/test_concurrency.cpp`.
