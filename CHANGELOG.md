# CHANGELOG — imaginaryVolumes

Per ADR-0002, this changelog records each milestone's work, its governing ADRs,
and the **demonstrated teeth evidence** (red→green or fault injection) for its
tests. Newest milestone first.

## M2 — Vulkan Headless Bring-Up

**Status:** Complete (2026-06-19).
**Governing ADRs:** ADR-0004 (binding & ownership), ADR-0005 (instance/device/
queue selection), ADR-0006 (offscreen target & readback), ADR-0007 (concurrency
baseline). Decisions: D-0015 (LSan scoping), D-0016 (classic barriers).

### Added
- **Binding boundary (ADR-0004):** `iv/vk/vulkan.hpp` (centralized
  `VULKAN_HPP_NO_EXCEPTIONS` include); `vk::Result → iv::Errc` mapping
  (`iv/vk/result.hpp`); `iv::vk::Unique<Handle>` — our own move-only,
  single-owner RAII wrapper. Default static dispatch; the two debug-utils procs
  are loaded by hand (no dynamic dispatcher).
- **Context (ADR-0005):** instance + validation layer/`VK_EXT_debug_utils`
  messenger in Debug (best-effort, message-capturing); physical-device ranking
  (discrete > integrated > virtual > cpu, accepts software) with
  `IV_VULKAN_DEVICE_INDEX` override; logical device + one graphics queue; command
  pool. A `pNext` create/destroy messenger + shared counter make object leaks at
  `vkDestroyInstance` observable.
- **Offscreen (ADR-0006):** `clearAndReadback` — `R8G8B8A8_UNORM` device-local
  image + host-visible staging buffer; classic 1.0 barriers (D-0016); clear →
  copy → fence → map. `ImageReadback` exposes the fixed top-left, row-major,
  `(y*w+x)*4` layout.
- **Concurrency (ADR-0007):** single-threaded, not-thread-safe baseline; Debug
  thread-affinity check on Context accessors; fence-gated host reads; the atomic
  assert handler is the sole cross-thread exception.

### Verification
- Build: **warning-clean** under the strict set + `-Werror` (Debug, ASan+UBSan,
  TSan).
- ASan+UBSan: full suite green via `ctest`; **LSan scoped off for the
  Vulkan-inclusive run** (D-0015), and our own non-Vulkan code leak-checked clean
  (`iv_tests_leakcheck`, `~[vk]`).
- TSan: the `[concurrency]~[vk]` race-gate is clean.
- Exercised on the **NVIDIA RTX 4070** (selected by ranking) — instance/device/
  queue/pool bring-up validation-clean; offscreen clear reads back exact UNORM
  bytes; deterministic. 83 assertions / 19 cases.

### Teeth evidence (ADR-0002 §2)
All performed 2026-06-19; each fault reverted; the final clean rebuild is green.

1. **Readback layout — fault injection.** Transposed `(x,y)→offset` in
   `ImageReadback::at`. On the non-square `[offscreen]` layout test (3×2, distinct
   per-pixel values) it read out of bounds: `AddressSanitizer:
   heap-buffer-overflow at offscreen.cpp:39`. (Note: the *uniform-clear* GPU tests
   can't catch this — every pixel is identical — which is why the dedicated
   varying-data `at` test exists.) Reverted.
2. **Validation gate — fault injection.** Cleared the image with
   `eTransferSrcOptimal` (wrong layout). The messenger reported
   `vkCmdClearColorImage(): ... can only be TRANSFER_DST_OPTIMAL ...` and
   `CHECK(ctx->validationClean())` went **RED** (`test_vk_offscreen.cpp:43`).
   Reverted.
3. **Device-selection error code — fault injection.** Returned `Errc::internal`
   for an out-of-range `IV_VULKAN_DEVICE_INDEX`. The `[context]` test went **RED**
   (`test_vk_context.cpp:48`, `code == device_unavailable`). Reverted.
4. **Concurrency (TSan) — fault injection.** Made the assert-handler storage a
   plain pointer (non-atomic). Under `-DIV_SANITIZE=thread`:
   `ThreadSanitizer: data race at assert.cpp:27 in set_assert_handler`. Reverted.
5. **Thread-affinity — fault injection.** Disabled the affinity `IV_DEBUG_ASSERT`.
   The off-thread test went **RED** (`test_concurrency.cpp:63`, handler never
   fired). Reverted.
6. **Teardown-leak gate — fault injection.** Made the command-pool deleter a
   no-op. The pNext messenger reported the undestroyed object during teardown and
   the `[context]` teardown test went **RED** (`test_vk_context.cpp:63`,
   `counter == 0`). Reverted.

## M1 — Build, Toolchain & Test/Sanitizer Harness

**Status:** Complete (2026-06-18).
**Governing ADRs:** ADR-0001 (build/toolchain/dependency policy), ADR-0002 (test
framework + teeth-evidence convention), ADR-0003 (error & failure semantics).

### Added
- **Build:** CMake (≥3.25) / strict ISO C++23 / system GCC (≥13 enforced at
  configure). Mandatory strict warning set with `-Werror` on first-party targets
  (`iv_warnings`); tree-wide sanitizer selection via `IV_SANITIZE`
  (`address,undefined` | `thread`); `compile_commands.json` exported. Layout:
  `include/ src/ tests/ third_party/ tools/`.
- **Test framework:** Catch2 **v3.7.1** vendored (amalgamated) under
  `third_party/catch2/`; first-party tests built with the strict set and linked
  against Catch2's built-in main; registered with CTest.
- **Error model (ADR-0003):** `iv::Errc`, `iv::Error`, `iv::Result<T>`,
  `iv::Status`, `iv::make_error`, `iv::to_string`, `iv::format` — `std::expected`
  based, exception-free. (`include/iv/error.hpp`, `src/error.cpp`.)
- **Assertions (ADR-0003):** `IV_ASSERT` (always-on) / `IV_DEBUG_ASSERT`
  (Debug-only) with an overridable, atomic failure handler.
  (`include/iv/assert.hpp`, `src/assert.cpp`.)
- **Suite:** 9 test cases / 25 assertions.

### Verification
- Debug build: **warning-clean** under the full strict set + `-Werror` (exit 0).
- ASan+UBSan (`-DIV_SANITIZE=address,undefined`): **suite green** via `ctest`
  (1/1) and directly (25 assertions / 9 cases).
- TSan: not applicable to M1 — no multi-threaded code path is exercised (the
  assert handler uses `std::atomic` but tests are single-threaded). The project
  concurrency baseline is set by an M2 ADR (§6).

### Teeth evidence (ADR-0002 §2)
All demonstrations performed 2026-06-18; each fault reverted; the final clean
rebuild of both configs is green.

1. **`-Werror` gate — fault injection.** Added unused local `teeth_demo_unused`
   to `iv::format` (`src/error.cpp`). Build **failed**:
   `error: unused variable 'teeth_demo_unused' [-Werror=unused-variable]`.
   Reverted → builds clean.
2. **UBSan gate — fault injection.** Added a temporary `[teeth-demo]` case with a
   `volatile int` signed overflow (`INT_MAX + 1`). Under
   `-DIV_SANITIZE=address,undefined`:
   `test_assert.cpp:81: runtime error: signed integer overflow: 2147483647 + 1
   cannot be represented in type 'int'` (nonzero exit). Reverted.
3. **Error mapping — red→green.** Perturbed
   `to_string(Errc::invalid_argument)` → `"WRONG"` (`src/error.cpp`). Case
   *"to_string maps each Errc to its stable name"* (`test_error.cpp:12`) went
   **RED** (`"WRONG" == "invalid_argument"`), as did the `format` case. Reverted
   → green.
4. **`IV_ASSERT` logic — red→green.** Inverted the macro's condition sense
   (`if (!(cond))` → `if ((cond))`, `include/iv/assert.hpp`). The fault compiles
   (condition still evaluated). Cases *"IV_ASSERT invokes the handler on a false
   condition"* and *"… does not fire on a true condition"* went **RED** (3 of 4
   cases, 7 of 11 assertions failed). Reverted → green. (Aside: fully neutering
   the macro is caught even earlier — by `-Werror=unused-variable` on the now-
   unused test locals — so the assertion contract is guarded by two independent
   gates.)
