# ADR-0019: Performance Contract & Benchmark

- **Status:** Accepted
- **Date:** 2026-06-19
- **Supersedes:** none

## Context
The founding goal is "interactive framerates for volumes of several hundred voxels
per side." M5 pins this with an enforcing benchmark. The maintainer chose the
operating point **≥ 30 FPS at a 512³ volume, 1280×720 output, on an RTX 4070-class
GPU**. Cites ADR-0011/0012 (the compute ray-marcher; early-ray termination and the
`stepCount` budget are the levers), ADR-0007 (deterministic inputs).

## Decision
**Contract.** On an **NVIDIA RTX 4070-class** GPU, a single ray-march of a **512³**
volume to a **1280×720** image with the default `stepCount` completes in
**≤ 33.3 ms** (≥ 30 FPS), taken as the **median** over N timed frames after warm-up.

**Benchmark (headless).** A dedicated `iv_bench` target (or a Catch2 test tagged
`[!benchmark]`, hidden from the default run) builds a `Context` + `Renderer` + a
fixed 512³ analytic volume, **warms up** (≈5 frames), times **N = 30**
`Renderer::render()` calls (each issues the GPU dispatch and waits its fence; the
small readback is included and is negligible relative to the march), reports
**min / median / max** ms and FPS, and **asserts median ≤ 33.3 ms**.

It is **not run by default** under `ctest` (results are hardware-dependent); it is
opt-in and run on the stated hardware. On other hardware it still reports, but the
assertion is advisory (documented). Inputs are fixed (volume + camera), so timings
are comparable run-to-run (ADR-0007).

## Contract Specification
- Target: **median ≤ 33.3 ms** per 512³ → 1280×720 `render()` on an RTX 4070-class
  GPU (≥ 30 FPS).
- Benchmark: warm-up, then **N ≥ 30** timed `render()` calls; report
  min/median/max + FPS; assert the median bound on the stated hardware; **opt-in**
  (excluded from the default `ctest`).
- Fixed, deterministic inputs (a known 512³ field and camera).
- Levers if the bound is missed: early-ray termination and the default step budget
  (ADR-0012) — not silent quality changes without an ADR note.

## Consequences
- Turns the vague "interactive" goal into a checkable number and a regression
  guard on render throughput.
- Hardware-specific (stated class), so it is opt-in rather than a default gate.
- Timing `render()` (dispatch + fence + small readback) is a **conservative** proxy
  for the viewer's pipelined present path (which omits the readback), so meeting it
  here implies the viewer is at least as fast.

## Alternatives Considered
- **Measure windowed present FPS:** rejected — vsync-capped (FIFO) and
  display-dependent; the headless render-time benchmark is the clean, CI-portable
  proxy.
- **GPU timestamp queries (exclude fence/readback):** deferred — wall-clock around
  a fenced `render()` is simpler and adequate at this scale; revisit if sub-ms
  precision is needed.
- **A default `ctest` perf gate:** rejected — hardware variance would make it
  flaky; opt-in on stated hardware is honest.

## Verification
- The benchmark **passes on the RTX 4070** (median ≤ 33.3 ms; report recorded in
  CHANGELOG § M5).
- **Teeth (MILESTONES M5):** force an oversampled march (e.g. 8× the default
  `stepCount`) → the median exceeds 33.3 ms → the benchmark assertion goes **red**;
  the contracted step → green. (Demonstrates the benchmark actually constrains the
  sampling budget.)
