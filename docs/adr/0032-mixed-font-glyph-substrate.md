# ADR-0032: Mixed-Font Glyph Substrate (Multiple Faces per Overlay)

- **Status:** Accepted
- **Date:** 2026-06-21
- **Supersedes:** none

## Context
Math typesetting (M9, ADR-0033) and even the legend's italic field name (deferred at
ADR-0031 as **B-0012**) need **several font faces in one overlay** at once: upright
roman for structure (`|`, `arg`, `(`, digits, operators), **true CM italic** for math
variables, and the **NewCMMath** face for symbols, large operators, and stretchy
delimiters. Today the glyph path is **single-face per overlay**: `iv::text::appendText`
*replaces* `Overlay::glyphAtlas` with one `Shaper`'s cumulative Slug atlas, and every
`GlyphVertex::glyphLoc` is a texel offset into that one atlas (ADR-0023/0025). Two faces
in one frame is unrepresentable. This ADR lifts that limit and vendors the two faces M9
needs. Cites ADR-0021 (overlay graphics pass + alpha blend), ADR-0023/0025 (Slug glyph
rendering, headless + present), ADR-0022 (vendored HarfBuzz + the NCM/GFL bundled-font
policy — which already named `NewCMMath` as "the matching face for the deferred math
milestone"), ADR-0004 (HarfBuzz hidden behind the `iv::text` boundary), B-0012.

## Decision
**Support N font faces in one overlay via a single *merged* Slug atlas; confine all
mixed-font bookkeeping to `iv::text` so the renderer, pipeline, and `GlyphVertex` are
unchanged.**

- **One GPU atlas, many faces (merged, rebased).** The Slug atlas is a flat `int16`
  texel stream and each glyph is encoded **self-contained** at its `atlasOffset`
  (ADR-0023). So multiple faces coexist in **one** `Overlay::glyphAtlas` by
  **concatenation**: face *k*'s atlas is appended at a base texel offset `base[k]`, and
  its glyphs' `glyphLoc` are written as `base[k] + EncodedGlyph::atlasOffset`. One atlas
  buffer, one descriptor, one draw — the renderer's glyph path (ADR-0023/0025) and the
  `GlyphVertex` layout do **not** change.
- **A face set owns the Shapers.** A new `iv::text` abstraction (`FontSet`) holds one
  `Shaper` per bundled face — **roman** (`NewCM10-Book`, the existing default), **italic**
  (`NewCM10-BookItalic`), **math** (`NewCMMath-Book`) — each owning its own font and Slug
  atlas. Appended glyph runs name their face; the builder encodes into that face's Shaper
  and merges its atlas with the per-face rebase above. (`appendText` keeps its single-face
  signature and behavior — it is the one-face case of the merge.)
- **Vendor two GFL faces** as embedded data, exactly like the Roman face (ADR-0022): add
  `NewCM10-BookItalic.otf` (true CM italic) and `NewCMMath-Book.otf` (the OpenType **MATH**
  font ADR-0033 reads via `hb_ot_math_*`) to `third_party/fonts/`, embedded into `iv_text`
  at build time. **Both are GUST Font License (GFL)** — verified from their `name` table
  copyright (nameID 0: "released under the GUST Font License", © Antonis Tsolomitis), the
  same permissive license as the bundled Roman face, and both are "Book"-weight faces
  **outside** the GPL3+FE subset ADR-0022 excludes (`NewCM10-Regular`, `NewCMUncial*`,
  `*Devanagari`). ~0.7 MB + ~1.4 MB.

## Contract Specification
- **Types/layout (unchanged on the GPU side):** `GlyphVertex` (`pos`, `texcoord`,
  `glyphLoc`, `color`) and the single `Overlay::glyphAtlas` (`int16` texel stream,
  uploaded as one `R16G16B16A16_SINT` uniform texel buffer) are **unchanged**; the
  renderer still issues one glyph draw with one atlas descriptor. Adding a face must not
  change the renderer or pipeline.
- **Merge invariant (assertable):** for every emitted glyph, `glyphLoc` indexes the
  texel of *its own face's* encoded outline within the merged atlas — i.e.
  `glyphLoc == base[face] + encodeGlyph(glyphId).atlasOffset`, where `base[face]` is the
  texel offset at which face *face*'s atlas was concatenated. A face contributing no
  glyphs contributes no atlas bytes.
- **Face set:** `iv::text` exposes a `FontSet` owning the roman/italic/math Shapers (the
  italic and math faces from the newly bundled GFL assets); it is the single source of the
  faces a math/label build uses. No HarfBuzz type appears in any `include/` header
  (ADR-0004) — the `FontSet` and the merge are `iv::text` concerns; the renderer sees only
  plain quads + one `int16` atlas.
- **Backward compatibility (assertable):** an overlay built from a **single** face is
  byte-identical to the pre-ADR path (`appendText` unchanged) — the legend/axis/title text
  of M7/M8 renders exactly as before. Mixed-font is strictly additive.
- **Isolation:** the new faces live in `iv_text` (text build); core `iv`/tests and the
  text-free build are unaffected (the `IV_BUILD_TEXT` gate, ADR-0022). License files for
  both faces recorded under `third_party/fonts/` (ADR-0001 §1.1).
- **Resolves B-0012:** with two faces in one overlay, the legend field name (ADR-0031) can
  render in true CM italic; whether to flip that default is a presentation choice (journaled,
  not a contract change).

## Consequences
- One overlay can mix roman/italic/math — the enabler for M9 math layout (ADR-0033) and the
  deferred legend italic (B-0012), with **no** renderer/pipeline/`GlyphVertex` churn.
- The merged atlas is larger than a single-face atlas (sum of the faces' encoded glyphs),
  well within the uniform-texel-buffer range for a frame's glyphs; the rebasing is the only
  new bookkeeping.
- Two more bundled font assets (~2 MB) and a `FontSet` to own the Shapers.
- Forecloses nothing: a future per-face-descriptor scheme remains possible if a single
  merged atlas ever proves limiting, behind the same `iv::text` boundary.

## Alternatives Considered
- **Per-face atlas + descriptor array (N draws / N bindings):** add a face index to
  `GlyphVertex`, carry N atlases on the Overlay, bind per draw-group. Rejected — it churns
  the renderer, the pipeline, and the public `GlyphVertex`/`Overlay` layout for no gain over
  a merged atlas, since Slug glyphs are self-contained and concatenate cleanly.
- **Oblique-sheared roman as fake italic** (the ADR-0031/D-0047 stopgap): rejected again —
  not the real CM italic; math needs the genuine math-italic alphabet and the MATH face
  regardless, so the honest faces must be vendored.
- **One giant pre-merged font** combining roman+italic+math glyphs: rejected — fights the
  upstream faces, loses the OpenType MATH table NewCMMath carries (ADR-0033 needs it), and
  is a maintenance burden vs. vendoring the stock GFL faces.

## Verification
- **Mixed-font render (teeth):** build one overlay with a roman run and an italic/math run,
  render headless; assert both runs produce non-`.notdef` glyphs and the italic/math glyph
  ids differ from the roman mapping of the same characters. Teeth: drop the per-face rebase
  (use face 0's base for every face) → the second face samples the wrong atlas region →
  glyphs render as garbage/`.notdef` vs. the reference → red; restore → green.
- **Merge invariant:** unit-check that, for a two-face build, each glyph's `glyphLoc` equals
  its face base plus its `encodeGlyph` offset, and that a face with zero glyphs adds zero
  atlas bytes.
- **Backward-compat invariant (teeth):** a single-face overlay yields a glyph stream +
  atlas byte-identical to the pre-ADR `appendText` output (guards "additive only"). Teeth:
  if the merge path perturbs single-face offsets, the byte-compare goes red.
- **Build/license:** the text build embeds both new GFL faces; the text-free build is
  unaffected; both license records present under `third_party/fonts/`.
