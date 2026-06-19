# ADR-0002: Test Framework & Teeth-Evidence Convention

- **Status:** Accepted
- **Date:** 2026-06-18
- **Supersedes:** none

## Context
DEV_PROCESS §2.4 requires tests with **teeth** — a test that fails when the
contract it guards is violated — and §2.6 gates milestone completion on teeth
that are *demonstrated, not claimed*. §8 makes an unproven claim of teeth a
defect. We need (a) a test framework and (b) a concrete, auditable convention for
**recording** the teeth so the claim can be checked later by an agent with no
memory of how the test was written. D-0003 selected Catch2 (single-header);
ADR-0001 fixes vendoring + offline builds and the ASan/UBSan gate.

Note on "single-header": Catch2 **v2.13** is the classic single header; Catch2
**v3** moved to a compiled library but ships an **amalgamated** distribution
(`catch_amalgamated.hpp` + `catch_amalgamated.cpp`) — two files, the maintained
line, equally vendor-friendly.

## Decision
**Framework: Catch2 v3, amalgamated distribution**, vendored under
`third_party/catch2/` (`catch_amalgamated.hpp` + `catch_amalgamated.cpp`) pinned
to an explicit version recorded alongside the files. No network fetch at build
(ADR-0001).

**Integration:** tests live in `tests/`, compiled into a test target registered
with **CTest**, and built under the **ASan+UBSan** configuration (ADR-0001).

**Teeth-evidence convention (binding):**
1. **Named fault per test.** Every contract/behavior test carries a brief comment
   naming the specific wrong implementation it would catch
   (`// teeth: catches transposed x/z voxel indexing`). A test with no nameable
   fault is decorative and must be fixed or removed (§2.4).
2. **Demonstrated per milestone, recorded in `CHANGELOG.md`.** Under each
   milestone, record either:
   - a **red→green** note — what was broken/seeded, which named test went red,
     then green; or
   - a **fault-injection** note — the exact perturbation (flipped comparison,
     perturbed constant, skipped synchronization), the named test that caught it,
     and confirmation it was restored.
   Each entry names the test.
3. **Completion gate.** A milestone may not be marked Complete (§2.6) without its
   teeth entries present in `CHANGELOG.md`.
4. **Abort-path testing.** Contract assertions that abort (ADR-0003 `IV_ASSERT`)
   are exercised by installing the **overridable assertion handler** (ADR-0003)
   and asserting it fired — no death tests / process forking required.
5. **Concurrent/GPU teeth (M2+).** Such tests run enough iterations/configurations
   to give a real race a real chance (§2.4, §6); the iteration count and rationale
   are stated in the test's comment.

## Contract Specification
- `CHANGELOG.md` exists at the project root and **is** the changelog the
  development loop updates (§3.4); teeth evidence is recorded there per milestone.
- A contract test lacking a named-fault comment is a defect.
- A milestone marked Complete without recorded teeth evidence is a defect
  (§2.6, §8).
- The test target builds and runs via CTest under ASan+UBSan.

## Consequences
- Small per-test overhead (the comment + a changelog note) — deliberate: it
  converts "tests have teeth" from assertion into an audit trail living in the
  repo (repo-as-memory, §0).
- Vendoring Catch2 amalgamated keeps builds offline/reproducible; upstream updates
  are manual.
- The overridable-handler approach (ADR-0003) removes any need for a death-test
  framework.

## Alternatives Considered
- **Catch2 v2.13 single header:** maintenance-mode; v3 amalgamated is current and
  equally vendor-friendly — chosen instead.
- **GoogleTest:** heavier dependency; its death tests are unnecessary given the
  overridable assertion handler. Deferred (the minimal-harness road is B-0003).
- **Minimal custom harness:** deferred (B-0003); Catch2 ergonomics win now.
- **Teeth recorded only in commit messages:** rejected — `CHANGELOG.md` is a
  durable, greppable, in-repo record; commit messages are easy to lose track of.

## Verification
- M1 produces the first `CHANGELOG.md` teeth entries: a recorded **red→green** for
  the trivial owned symbol's test, plus **fault-injection** notes for the
  warnings-as-errors and UBSan gates (ADR-0001 Verification).
- The test target builds and runs under CTest + ASan/UBSan with no failures.
