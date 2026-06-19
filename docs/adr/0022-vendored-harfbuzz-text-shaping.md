# ADR-0022: Vendored HarfBuzz Dependency & Unicode Text Shaping

- **Status:** Accepted
- **Date:** 2026-06-19
- **Supersedes:** none

## Context
Labels (axis/tick/title/legend text) require turning a Unicode string + font into
**positioned glyphs** — i.e. text *shaping*. **HarfBuzz** is the standard shaper
("the `ffmpeg` of text shaping"); it also exposes glyph **outlines** via its `hb-draw`
API from its own OpenType parser (no FreeType needed). This is a new third-party
dependency, so per **ADR-0001 §1.1** it needs this ADR + a vetting/acquisition
decision; the maintainer chose to **vendor** it (unlike GLFW's system path, D-0026)
— deliberately, because the GPU glyph renderer (ADR-0023) uses HarfBuzz's
**experimental** `libharfbuzz-gpu`, and vendoring pins a known-good version. LaTeX
math is **deferred** (maintainer): M6 labels are text/Unicode. Cites ADR-0004 (keep
third-party types behind our boundary), ADR-0001 (warnings on first-party only).

## Decision
**Vendor HarfBuzz at a pinned commit; wrap shaping behind an `iv` interface.**
- **Acquisition:** vendor HarfBuzz into `third_party/harfbuzz/` at a **pinned commit**
  (recorded here at acceptance), including the experimental `libharfbuzz-gpu` sources
  (ADR-0023). A pinned drop freezes the (experimental) API and keeps the build
  hermetic.
- **Build:** compile a **minimal** HarfBuzz as a static lib via our CMake — **no
  ICU / GLib / Cairo / FreeType** integrations (use HarfBuzz's own OpenType tables +
  `hb-draw` for outlines). Prefer the amalgamated source (`harfbuzz.cc`) if it covers
  the GPU lib, else its CMake with features disabled. Built **without** the
  first-party `-Werror` set (third-party, like Catch2/Vulkan); its includes are
  `SYSTEM`. Core `iv` need not link it — text lives in the annotation layer
  (ADR-0021), so a `IV_BUILD_TEXT`-style gate keeps a text-free build possible.
- **Boundary (ADR-0004):** HarfBuzz types do **not** appear in `iv` public headers.
  A small `iv::text::Shaper` wraps it: input UTF-8 + font + pixel size → output a
  sequence of positioned glyphs (`glyphId`, advance/offset in pixels) consumed by
  ADR-0023.
- **Default font:** bundle **New Computer Modern (NCM)** — the OpenType,
  extended-Unicode successor to Knuth's Computer Modern (the publication "TeX look",
  and a matching `NewCMMath` face for the deferred math milestone). Bundle only the
  **GFL-licensed** faces we use (vetted from NCM 8.1.0: e.g. `NewCMSans10-Regular` for
  labels and/or the `NewCM10-Book` Roman + `NewCMMono10-Regular`), **not** the 34 MB
  package — ~0.6–1.4 MB. **Explicitly exclude the GPL3+FE+DE subset**
  (`NewCM10-Regular` specifically, all `NewCMUncial*`, all `*Devanagari`) to keep
  bundling maximally permissive. Callers may supply their own font file later.
- **License:** HarfBuzz's permissive "Old MIT" license; the NCM faces under the
  **GUST Font License (GFL, LPPL v1.3c-based, free/libre)** — both verified from their
  `COPYING`/`License.txt` and recorded under `third_party/` (with NCM's version,
  8.1.0, and copyright). The unrelated absence of a top-level project `LICENSE` is
  noted as **B-0009** (does not affect bundling GFL fonts; would matter for the GPL
  subset, which we exclude).

## Contract Specification
- HarfBuzz vendored at a **pinned commit** in `third_party/`, built minimally
  (no ICU/GLib/Cairo/FreeType), `SYSTEM` includes, **not** under `-Werror`.
- Shaping is reached only through `iv::text::Shaper` (UTF-8 + font + size → positioned
  glyphs); **no HarfBuzz type crosses an `iv` public signature** (ADR-0004).
- **New Computer Modern** is the bundled default font, using **GFL-licensed faces
  only** (the GPL3+FE+DE subset — `NewCM10-Regular`, `NewCMUncial*`, `*Devanagari` —
  is excluded); HarfBuzz + NCM/GFL licenses recorded (ADR-0001 §1.1).
- The core library/tests build with text disabled; text is in the annotation layer.

## Consequences
- A correct, industry-standard shaper (complex scripts, kerning, ligatures) for
  labels, and the outline source feeding the Slug GPU renderer (ADR-0023).
- Vendoring grows the tree and shifts update responsibility to us, but pins the
  experimental GPU API and keeps builds reproducible — the intended trade-off.
- A new build knob and a bundled font asset to track.

## Alternatives Considered
- **System `find_package`** (as GLFW): rejected by the maintainer — the experimental
  `libharfbuzz-gpu` makes a pinned vendored version safer than a moving system one.
- **FreeType (+HarfBuzz)** for outlines/rasterization: rejected — `hb-draw` supplies
  outlines, so HarfBuzz alone suffices; avoids a second dependency.
- **Hand-rolled bitmap-font / stb_truetype:** rejected — no real shaping; poor for
  scientific/Unicode labels and not resolution-independent (ADR-0023).

## Verification
- A shaping test: shape a known string with the bundled font and assert the glyph
  count and advances/offsets match a recorded reference (the `iv::text::Shaper`
  contract), with no HarfBuzz type in the public API (compile-time).
- **Teeth:** perturb the shaper input (e.g. drop cluster/position handling, or feed a
  wrong glyph mapping) → the positioned-glyph reference check goes red.
- Build teeth: the text-disabled configuration still builds `iv`/tests (HarfBuzz
  absent from the core), mirroring the ADR-0016 GLFW-isolation gate.
