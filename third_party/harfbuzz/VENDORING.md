# Vendored HarfBuzz (ADR-0022)

HarfBuzz is **vendored** (not a system dependency) so the build is hermetic and the
experimental `libharfbuzz-gpu` API used by ADR-0023 is pinned to a known-good drop.

## Pin
- **Upstream:** https://github.com/harfbuzz/harfbuzz
- **Commit:** `ac0979b6f44b41894c73fd208a0b4f5a8c6dc6ff` (branch `main`, 2026-06-18)
- **Version:** post-9.0.0 `main`. `libharfbuzz-gpu` (the Slug GPU renderer, ADR-0023)
  exists **only on `main`** at this time — no tagged release ships it yet — which is
  why the pin is a commit, not a tag.

## What is vendored
A near-verbatim mirror of upstream `src/` at the pin, so it can be audited by
`diff -r` against the tag. **Excluded** (none compiled in our build, and excluding
them keeps the drop single-licensed Old MIT):
- `src/*.py` — table/artifact generators (build-time only; the generated tables are
  committed alongside, e.g. `hb-ot-shaper-*-table.hh`).
- `src/wasm/`, `src/rust/`, `src/ms-use/` — optional integration / spec-data dirs
  not referenced by our compile closure (`src/ms-use/` also carries its own
  Unicode-data COPYING; dropping it keeps licensing to the single top-level one).
- Raw non-GLSL GPU shader sources (`*.wgsl`, `*.msl`, `*.hlsl`). The pre-generated
  embedding headers (`*-{wgsl,msl,hlsl}.hh`) are kept because `hb-gpu*.cc` `#include`s
  them; only `*.glsl` is consumed by our `glslc` toolchain (ADR-0023).

The included file set is exactly the transitive closure of `src/harfbuzz.cc` (core)
and `src/hb-gpu*.cc` (GPU, ADR-0023), verified to compile with our GCC at C++23.

## How it is built (`CMakeLists.txt`)
- **Core (this ADR):** one static lib `harfbuzz_core` from the amalgamated
  `src/harfbuzz.cc`. With **no `HAVE_*` defines**, HarfBuzz uses its own built-in
  OpenType + Unicode (UCD) — **no ICU / GLib / Cairo / FreeType**. `src/` is a SYSTEM
  include; the lib is **not** under our `-Werror` set (third-party, like Catch2).
- **GPU (ADR-0023):** `hb-gpu.cc`, `hb-gpu-draw.cc`, `hb-gpu-paint.cc` + the `.glsl`
  shaders — added when that ADR is implemented.

## License
HarfBuzz's permissive **"Old MIT"** license — see `COPYING` (and `AUTHORS`). No GPL/
LGPL-licensed file is in the compiled closure (verified). The GLSL Slug shaders carry
the same Old MIT plus an attribution to Eric Lengyel (Slug algorithm; patent dedicated
to the public domain — D-0034).

## Updating the pin
1. `git clone https://github.com/harfbuzz/harfbuzz` and checkout the new commit.
2. Replace `src/` with the new `src/`, re-applying the exclusions above.
3. Confirm `g++ -std=c++23 -c src/harfbuzz.cc -Isrc` and the three `hb-gpu*.cc`
   compile clean; rebuild and run the text tests (`[text]`, `[glyph]`).
4. Update the **Pin** section here and record the change in `DECISIONS.md` + `CHANGELOG.md`.
