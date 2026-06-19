# CHANGELOG — imaginaryVolumes

Per ADR-0002, this changelog records each milestone's work, its governing ADRs,
and the **demonstrated teeth evidence** (red→green or fault injection) for its
tests. Newest milestone first.

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
