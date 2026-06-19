# ADR-0007: Concurrency Baseline

- **Status:** Accepted
- **Date:** 2026-06-18
- **Supersedes:** none

## Context
DEV_PROCESS §6 requires the concurrency contract to be **explicit**; absent a
stated contract the default is "not thread-safe" (§6.0), and §6 is otherwise inert
only if the project declares itself single-threaded in an ADR. M2 introduces
Vulkan objects, a queue, command buffers, and host↔device synchronization, all of
which have external-synchronization rules. This ADR states the project's baseline
threading contract; later milestones that add parallelism must state their own
contracts (extending/superseding this one).

## Decision
**The public API is single-threaded and not thread-safe.** Every `iv` object must
be created, used, and destroyed on **one thread**; the library spawns **no internal
threads** in M2. Concurrent use of the same object — or of objects sharing a device
or queue — is a **precondition violation**.

**Debug thread-affinity check.** In Debug builds, device-scoped objects record the
`std::thread::id` that created them and `IV_DEBUG_ASSERT` that each subsequent use
is on that thread. This makes the single-thread contract **checkable and testable**
(compiled out in Release).

**Vulkan external synchronization.** We honor Vulkan's host-synchronization rules:
a `vk::Queue`, its `vk::CommandPool`, and command buffers from that pool are
externally synchronized and are touched only from the owning thread. Nothing is
shared across threads in M2.

**Host↔device synchronization.** GPU work is submitted and then **awaited with a
fence** before any dependent host read (e.g. readback, ADR-0006). Submit→wait is
fully ordered; there is no async in M2.

**The assertion handler (ADR-0003) is the single exception:** its get/set is
`std::atomic` and may be called from any thread.

**Determinism (§6.2).** M2's clear+copy output is **bitwise deterministic for a
given device** (no floating-point reduction is involved). Cross-device bitwise
equality is **not** promised. (Floating-point reduction order becomes relevant only
when the renderer composites — addressed by M4's ADR.)

## Contract Specification
- All public types are **not thread-safe**; single-thread create/use/destroy is a
  caller precondition.
- The library creates no threads in M2.
- In Debug, device-scoped objects assert thread-affinity on use
  (`IV_DEBUG_ASSERT`); in Release this check is absent.
- Host reads of GPU-written memory occur only after the governing fence is
  signaled.
- `set_assert_handler`/`get_assert_handler` are thread-safe (atomic); no other
  symbol is.
- M2 output is bitwise deterministic per device.

## Consequences
- Simple, race-free baseline; races are largely *unrepresentable* because nothing
  is shared (§6.0).
- The Debug affinity check turns "single-threaded" from a comment into an assertion
  (§8.9 avoided).
- Future async transfer / multi-threaded recording each require their own ADR; this
  is the contract they start from.

## Alternatives Considered
- **Thread-safe public API now:** rejected — premature; no use case in M2, and it
  would impose locking cost and complexity before any concurrency exists.
- **Leave concurrency unstated (rely on §6.0 default):** rejected — §6 requires an
  explicit declaration, and the affinity check + determinism statement need a home.
- **Async submit (no fence wait) in M2:** rejected — readback needs the result;
  async adds nothing here.

## Verification
- **TSan gate:** the suite runs clean under `-DIV_SANITIZE=thread`. A dedicated
  test hammers `set_assert_handler`/`get_assert_handler` from multiple threads over
  many iterations — race-free because the storage is atomic. **Teeth:** temporarily
  make the handler storage a plain pointer ⇒ TSan reports a data race; restore.
- **Affinity check:** a test creates a device-scoped object on one thread and uses
  it from another with a capturing assert handler installed; the handler fires
  (Debug). **Teeth:** remove the affinity assert ⇒ the handler does not fire → red.
- **Determinism:** two identical clear+readback runs produce byte-identical results
  (ADR-0006 verification).
