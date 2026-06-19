# ADR-0001: Build, Toolchain & Dependency Policy

- **Status:** Accepted
- **Date:** 2026-06-18
- **Supersedes:** none

## Context
This is the project's first build-establishing contract (DEV_PROCESS §1.1 requires
an ADR for build/dependency policy). DEV_PROCESS §7 mandates a build under
warnings-as-errors and clean ASan/UBSan (and TSan for threaded code) runs. The
maintainer fixed the toolchain to **C++23 on the system GCC**; the minimal-deps
goal (own the Vulkan boilerplate) constrains what we pull in.

Toolchain probed on the dev host (2026-06-18): GCC **13.3.0** — accepts
`-std=c++23` and compiles `<expected>`; CMake **3.28.3**; Python **3.12.3**.
**Vulkan SDK is not installed** (no headers, no `pkg-config`); it is an M2
dependency, not an M1 one. No prior ADRs exist to honor (ADR index is empty).

## Decision
**Build system: CMake.** `cmake_minimum_required(VERSION 3.25)`. Standard fixed
via `CMAKE_CXX_STANDARD 23`, `CMAKE_CXX_STANDARD_REQUIRED ON`,
`CMAKE_CXX_EXTENSIONS OFF` (strict ISO C++23, **not** `gnu++23`).
`CMAKE_EXPORT_COMPILE_COMMANDS ON`. **Compiler: GCC ≥ 13.**

**Mandatory warning baseline** on all first-party targets, **warnings-as-errors**:

```
-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
-Wnon-virtual-dtor -Wcast-align -Wunused -Woverloaded-virtual
-Wdouble-promotion -Wformat=2 -Werror
```

Narrowly-scoped local suppressions are permitted **only** at third-party boundary
adapters, via `#pragma GCC diagnostic` with a comment citing the reason
(convention-leakage rule, DEV_PROCESS §8.5). First-party code carries no blanket
suppressions.

**Build configurations:**
- **Debug:** `-O0 -g`, all assertions active, sanitizers selectable.
- **Release:** `-O2 -DNDEBUG`, but `IV_ASSERT` contract checks remain active
  (ADR-0003 governs the assertion macros).
- **Sanitizer selection** via cache option `IV_SANITIZE`:
  `address,undefined` (combined ASan+UBSan) or `thread` (TSan, for threaded code
  from later milestones). The test suite runs under **ASan+UBSan** in the gate.

**Dependency policy:**
- Minimal external dependencies. **Every new third-party dependency requires its
  own ADR** (DEV_PROCESS §1.1).
- **Acquisition:** system libraries via `find_package` (Vulkan from M2, GLFW from
  M5); small header/amalgamated libraries **vendored** under `third_party/` pinned
  to an explicit version (e.g. Catch2 per ADR-0002). **No build-time network
  fetch is required** — the build is reproducible offline once deps are present.
- **Vulkan SDK is required from M2 onward** (`find_package(Vulkan)`); **M1 builds
  and tests with no Vulkan present.**

**Source layout:** `include/` (public headers), `src/` (library implementation),
`tests/` (Catch2 tests), `tools/` (tooling), `third_party/` (vendored deps),
`docs/` (ADRs etc.).

## Contract Specification
- First-party targets compile under C++23/GCC≥13 with the mandatory warning set
  and `-Werror`; `CMAKE_CXX_EXTENSIONS OFF`.
- **Invariant (assertable):** any compiler diagnostic in the mandatory set fails
  the build. Introduce a qualifying warning ⇒ build fails; remove ⇒ build passes.
- **Invariant:** the project configures, builds, and tests successfully with **no
  Vulkan installed** through M1.
- Adding a third-party dependency without an accepting ADR is a defect (§1.1).
- The ASan+UBSan configuration links the test target; a clean sanitizer run is
  required by the Definition of Done (§7).

## Consequences
- `-Wconversion`/`-Wsign-conversion` will force explicit, visible casts at the
  Vulkan boundary (`uint32_t`/`size_t`) — intentional friction; boundary adapters
  may locally suppress with justification.
- Vendoring keeps builds offline and reproducible but obliges us to track upstream
  security/bug fixes by hand.
- ISO-strict C++ (extensions off) forecloses GNU-isms and aids portability.
- Pinning GCC≥13 (not Clang) means single-compiler coverage for now; a superseding
  ADR can add Clang/CI matrix later.

## Alternatives Considered
- **Clang/libc++:** maintainer specified system GCC; not chosen now (supersedable).
- **Meson/Bazel:** CMake is the lingua franca for Vulkan/GLFW (`find_package`),
  least friction; rejected the alternatives on integration cost.
- **FetchContent for all deps:** rejected as default — introduces build-time
  network and non-reproducibility; vendoring + `find_package` preferred.
- **`gnu++23` (extensions on):** rejected for ISO strictness.
- **Looser warning set:** rejected; rigor is the project's premise (§0).

## Verification
- A first-party target builds warning-clean under the mandatory flags (build log).
- **Teeth (M1):** inject a deliberate qualifying warning (e.g. a narrowing
  conversion or unused variable) ⇒ build fails; remove ⇒ build succeeds. Recorded
  per the ADR-0002 teeth convention.
- **Teeth (M1):** a deliberate UBSan-tripping path behind a default-off flag ⇒
  UBSan reports ⇒ confirms the sanitizer gate actually catches; default build runs
  clean.
- `-std=c++23` and `CMAKE_CXX_EXTENSIONS OFF` confirmed via `compile_commands.json`.
