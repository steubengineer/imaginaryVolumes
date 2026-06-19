# ADR-0003: Error & Failure Semantics

- **Status:** Accepted
- **Date:** 2026-06-18
- **Supersedes:** none

## Context
DEV_PROCESS §5 requires error & failure semantics to be fixed once, in an ADR,
rather than re-assumed per file; §3.2 requires every relied-upon invariant to be
an assertion (not a comment, §8.9), including runtime contracts at boundaries.
ADR-0001 gives us C++23, which provides `std::expected`, and a Release build that
keeps `-DNDEBUG` — so the standard `assert()` would vanish in Release, which is
unacceptable for boundary contracts. We need one policy stating **what is UB,
what aborts, and what returns an error**, precise enough to test.

## Decision
**Two failure categories, two mechanisms.**

1. **Contract violations** (programming errors — a caller breaking a documented
   precondition, or a broken internal invariant): handled by the assertion macros
   below, which are **always active (Debug *and* Release)**. On failure they
   report `file:line`, the failed expression, and a message, then by default
   `std::abort`. Because they are always active, **a precondition violation is
   well-defined behavior (a deterministic abort), not UB.**

2. **Recoverable runtime failures** (environment/resource — unsupported
   configuration, device/resource creation failure, an allocation failure
   reported by an API we call): reported by **return value** using
   `std::expected<T, iv::Error>`. No exceptions are used for control flow.

**Exceptions:** the library does **not** use exceptions as a contractual error
channel and throws none of its own across its public boundary. The only exception
that may propagate out is `std::bad_alloc` (host memory exhaustion), treated as
**fatal/unrecoverable** — no recovery is promised.

**Types (in namespace `iv`):**
- `enum class Errc` — error codes (e.g. `invalid_argument`,
  `unsupported_configuration`, `device_unavailable`, `allocation_failed`,
  `internal`). **Append-only:** values are never renumbered or removed; new codes
  are appended.
- `struct Error { Errc code; std::string message; }` plus a formatting helper.
- `template <class T> using Result = std::expected<T, Error>;` and
  `using Status = std::expected<void, Error>;`

**Assertion macros:**
- `IV_ASSERT(cond, msg)` — **always active.** On a false condition it invokes the
  **overridable** handler `iv::detail::on_assert_failure(file, line, expr, msg)`,
  whose default prints and `std::abort`s. The handler is settable so **tests can
  intercept it** and verify assertion firing without killing the test process
  (ADR-0002 §4).
- `IV_DEBUG_ASSERT(cond, msg)` — active **only in Debug** (compiled out in
  Release). For hot-path internal checks whose always-on cost is unacceptable and
  whose invariant is independently guaranteed by construction. **Never** used for
  public-API preconditions.
- **Public-API preconditions use `IV_ASSERT`** (always-on), so caller misuse is a
  deterministic abort, never UB.

**UB policy:** the library introduces no UB of its own for documented misuse —
preconditions abort via `IV_ASSERT`. UB exists only where C++/Vulkan inherently
has it (e.g. a use-after-free we must avoid); those are caught by the sanitizer
gate (ADR-0001), never rationalized as benign (§8.8).

## Contract Specification
- Public functions that can fail at runtime return `Result<T>`/`Status`.
- `IV_ASSERT` is active in **all** build configurations. Its handler has the
  signature `void(const char* file, int line, const char* expr, const char* msg)`
  and is settable/restorable at runtime; the default aborts.
- `Errc` is **append-only** — never renumber or remove a value.
- The library throws no exceptions of its own; only `std::bad_alloc` may
  propagate, as fatal.
- **Invariant (assertable):** with a custom handler installed, `IV_ASSERT(false,…)`
  invokes it exactly once with the correct `expr`/`file`/`line`.

## Consequences
- `std::expected` gives ergonomic, allocation-light, exception-free error
  propagation — well suited to the GPU/perf domain (no unwinding on hot paths) and
  native to C++23.
- Always-on `IV_ASSERT` costs a branch at boundaries in Release — acceptable;
  boundaries are not hot loops. Hot loops use `IV_DEBUG_ASSERT`.
- The overridable handler makes abort-paths unit-testable (teeth) without process
  death (ADR-0002).
- Append-only `Errc` keeps consumers' switch statements forward-compatible if they
  handle a default case.

## Alternatives Considered
- **Exceptions as the primary channel:** rejected — unwinding across the
  Vulkan/GPU boundary and on hot paths is costly and harder to make deterministic;
  `std::expected` is clearer at call sites and C++23-native.
- **`<cassert>` / `assert()` as the contract mechanism:** rejected — it compiles
  out under `-DNDEBUG` (Release per ADR-0001), so boundary contracts would vanish
  and misuse would become UB (§3.2, §8.9). `IV_ASSERT` stays on.
- **Error codes without messages:** rejected — context strings aid diagnosis and
  cost only on the failure path.
- **`std::error_code` / `std::system_error`:** heavier and exception-coupled;
  `Errc` + `expected` is lighter and exception-free.

## Verification
- **Teeth:** a function forced into each failure mode returns the expected `Errc`;
  breaking the code→failure mapping turns the test red.
- **Teeth (abort path, death-free):** install a capturing handler, trigger
  `IV_ASSERT(false,…)`, assert the handler observed the correct `expr`/`file`;
  changing the asserted expression makes it mismatch (red).
- The sanitizer gate (ADR-0001) backs the "no UB of our own" claim.
