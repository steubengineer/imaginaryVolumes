# Third-Party Notices

imaginaryVolumes itself is licensed under the MIT License (see [LICENSE](LICENSE)).
It bundles the following third-party components, each under its own permissive
license. Their license texts are included in the repository at the paths noted
below and are retained per those licenses' attribution terms.

## HarfBuzz / libharfbuzz-gpu — "Old MIT"
- **What:** Unicode text shaping, and GPU glyph rendering via the **Slug** algorithm
  (`libharfbuzz-gpu`). Vendored as an amalgamated build of a pinned upstream `main`
  commit (see `third_party/harfbuzz/VENDORING.md` for the exact pin and the vendored
  file set).
- **Used for:** shaping label text and rasterizing crisp, resolution-independent
  glyphs on the GPU.
- **License:** the "Old MIT" license — `third_party/harfbuzz/COPYING`.
- **Patent note:** the Slug glyph-rendering algorithm's patent was dedicated to the
  public by its author; no patent grant is required to use it here.

## New Computer Modern (NewCM) fonts — GUST Font License (GFL)
- **What:** the bundled label typeface — `NewCM10-Book.otf`, `NewCM10-BookItalic.otf`,
  and the OpenType-MATH face `NewCMMath-Book.otf` (New Computer Modern 8.1.0, by
  Antonis Tsolomitis). Embedded into the `iv_text` library at build time.
- **Used for:** the publication-quality "TeX look" label and math typesetting.
- **License:** the **GUST Font License** (GFL, an LPPL-1.3c-based free/libre license) —
  `third_party/fonts/GUST-FONT-LICENSE.txt` (package license: `NCM-License.txt`).
  The three bundled faces are GFL; the package's GPL-licensed subset (`NewCM10-Regular`,
  Uncial, Devanagari) is deliberately **excluded** from this repository. The faces are
  shipped verbatim and unmodified. See `third_party/fonts/README.md` for provenance.

## Catch2 — Boost Software License 1.0 (BSL-1.0)
- **What:** the unit-test framework (amalgamated distribution, v3.7.1), in
  `third_party/catch2/`.
- **Used for:** the test suite only — **not** part of the shipped library.
- **License:** Boost Software License 1.0; the notice is carried in the header of
  `third_party/catch2/catch_amalgamated.hpp` / `.cpp`.

## Build-time tools (not distributed)
- **Vulkan** headers/loader and **`glslc`** (shader compiler) are required to build,
  but are not redistributed as part of this repository.
