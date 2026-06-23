# ADR-0034: Legend Layout v2 — Aspect-Aware Placement & Compact Rotated Magnitude Caption

- **Status:** Accepted
- **Date:** 2026-06-22
- **Supersedes:** none

> Amends the layout details of ADR-0028 §(3) and ADR-0031 (caption placement); supersedes neither.

## Context
The legend (ADR-0028) is a right-side 2-D swatch with phase ticks below, magnitude value ticks
on the right edge, the phase caption `arg(f)` below, the magnitude caption `|f|` **above** the
bar, and the thickness `L` label below. Its placement is a **fixed** `LegendSpec::rectNdc`
default (`{0.60, −0.45, 0.84, 0.45}`, screen NDC). Two maintainer-reported defects from the
visual-polish pass (B-0013):

1. **Placement collides with the plot.** The swatch rect is in screen NDC (aspect-independent),
   but the volume bounding box is projected with the aspect correction (ADR-0012/0026:
   `viewProjection(camera, aspect)`, `[0][0] ∝ 1/aspect`). In a window taller than wide
   (`aspect < 1`) the box magnifies horizontally in NDC and **overruns the fixed legend rect**;
   it only looks right once the window is made wider than tall. The legend must sit **just right
   of the projected box** (clear of the right-side axis label), regardless of aspect.

2. **The magnitude caption is bulky.** `|f|` sits **above** the swatch, widening the legend's
   vertical footprint and reading awkwardly next to a tall, narrow bar. The maintainer wants it
   on the **left** of the swatch, **rotated a quarter-turn counterclockwise** (reading
   bottom-to-top), which is the conventional, compact place for a magnitude/colorbar axis title.

(2) needs a capability the text layer (ADR-0033) does not yet have: emitting a label **rotated**
off its horizontal baseline. The Slug glyph quad (ADR-0023, `pushGlyphQuad`) ties each corner's
screen position to its `texcoord` (the glyph's em render-coord); **rotating the quad's screen
positions while keeping `texcoord` fixed rotates the rendered glyph** — so rotation is a
pixel-space transform of the emitted quad geometry, requiring no change to the math/box layout
and no second atlas.

Read: ADR-0028 (legend spec/builder, swatch), ADR-0031 (field-name captions), ADR-0033 (`$…$`
labels, `MixedGlyphs` emission, `appendLabel`/`measureLabel`), ADR-0023 (`pushGlyphQuad`,
Slug renderCoord), ADR-0026 (`viewProjection`/`projectToPixel`, screen-space labels), ADR-0012
(camera), ADR-0029 (facade fan-out); D-0044 (screen overlay is y-down NDC), D-0049 (the facade
frames the cube at distance 3.3 — the box extent this builds on).

## Decision
Three cohesive parts of one decision — *lay the legend out so it never collides and the
magnitude title is compact*. No change to the public core-`iv` `LegendSpec` struct (its fields
and `rectNdc` semantics are unchanged); all of this is in the `iv_text` layout layer + the
facade that already owns transfer/placement fan-out.

### (1) Rotated label emission primitive (`iv_text`, extends ADR-0033)
Add the ability to emit any label (plain text + inline `$…$` math) rotated about an anchor:

- `iv::text::MixedGlyphs` gains a lightweight marker + a pixel-space rotation of recently
  appended quads:
  ```cpp
  struct Marker { std::array<std::size_t, kFaceCount> n; };   // per-face quad counts
  [[nodiscard]] Marker marker() const noexcept;
  void rotateSince(const Marker& from, float angleRad,
                   float pivotXpx, float pivotYpx,
                   std::uint32_t fbWidth, std::uint32_t fbHeight);  // rotate quads added since `from`
  ```
  `rotateSince` recovers each affected quad corner's pixel position from its NDC `pos`
  (`px = (pos+1)·half`), applies `P' = O + R(angleRad)·(P − O)` about the pivot `O`
  (`R = [[c,−s],[s,c]]`, **pixel frame, y-down**), and writes the NDC back. `texcoord`,
  `glyphLoc`, and `color` are untouched, so each glyph's outline is unchanged and only its
  screen placement rotates.

- `iv::text::math::appendLabelRotated(glyphs, overlay, label, pivotXpx, pivotYpx, fbW, fbH,
  pixelSize, color, angleRad)` — lays the label out horizontally at the pivot (via the existing
  `appendLabel`), then `rotateSince(marker, angleRad, pivot…)`. Returns the advance **along the
  rotated baseline** (px). The pivot is the rotated baseline origin (the left end of the text as
  it reads).

- **Convention for the legend use:** "counterclockwise quarter-turn, reading bottom-to-top" =
  the horizontal baseline that advanced `+x` now advances **up** (`−y` in the y-down pixel
  frame), i.e. `angleRad = −π/2`.

- **Scope/limitation:** `appendLabelRotated` rotates **glyph quads only**. Inline-math *rules*
  (fraction bars / radical vinculums / overlines, which go to `overlay.screenTriangles`) are
  **not** rotated, so a rotated label must not contain them. The legend captions (`|f|`,
  `arg(f)`, and any field name) are bars + a symbol — glyphs only — so this is sufficient; the
  builder uses the rotated path only for the magnitude caption.

### (2) Magnitude caption: left of the swatch, rotated (amends ADR-0028 §(3), ADR-0031)
`buildLegend` draws `spec.magnitudeCaption()` (ADR-0031, unchanged derivation) **rotated −π/2,
vertically centered on the swatch, just left of its left edge** (`rectNdc.left`), instead of
horizontally centered above the top. A pad separates the caption's near edge (the top of its
upright glyphs) from the swatch. The phase caption (`arg(f)`, below), the `−π/0/π` phase ticks,
the right-edge magnitude value ticks/labels, and the `L` label are **unchanged**.

### (3) Aspect-aware horizontal placement (facade, presentation)
A placement helper sets the legend's horizontal position so its **left footprint clears the
projected box**, keeping the whole legend (left caption · swatch · right value labels) on screen:

- `iv::text::placeLegendRight(LegendSpec& spec, const iv::vk::RenderParams& camera,
  std::uint32_t fbWidth, std::uint32_t fbHeight)` projects the 8 unit-cube corners with
  `viewProjection(camera, aspect)` (`aspect = fbW/fbH`), takes the maximum in-front corner NDC-x
  `boxRight`, and sets:
  - `swatchLeft = clamp(boxRight + gap + capAllow, defaultLeft, rightEdge − valAllow − width)`
  - `swatchRight = swatchLeft + width`  (the swatch **width is preserved**),

  where `width = spec.rectNdc[2] − spec.rectNdc[0]` (the incoming width), `defaultLeft` =
  incoming `spec.rectNdc[0]`, and `gap`, `capAllow` (room for the left rotated caption),
  `valAllow` (room for the right value labels), `rightEdge` (≈ 0.98) are NDC amounts derived
  from pixel constants via `halfW` (so the gaps are aspect-consistent). The vertical extent
  (`rectNdc[1]`, `[3]`) is unchanged. Behind-camera/degenerate projection → leave `rectNdc`
  unchanged (the default).

  Monotone & clamped: a **wide** window (`boxRight` small) yields `swatchLeft = defaultLeft`
  (today's look, unchanged); a **square/tall** window pushes the legend right to clear the box,
  never past `rightEdge − valAllow − width` (so the value labels stay on screen — in extreme
  portrait the legend hugs the right edge and the residual box overlap is accepted, since no
  on-screen placement avoids a box that fills the frame).

The facade calls `placeLegendRight` before `buildLegend`: `renderPlot` once (fixed camera),
`makePlot`'s per-frame closure every frame (live camera/aspect — so the legend tracks the box as
the user orbits/resizes). `LegendSpec` carries the result in its existing `rectNdc`.

## Contract Specification
- **New `iv_text` surface:** `MixedGlyphs::Marker`, `MixedGlyphs::marker()`,
  `MixedGlyphs::rotateSince(...)` (text_layout.hpp); `iv::text::math::appendLabelRotated(...)`
  (math_layout.hpp); `iv::text::placeLegendRight(...)` (legend_builder.hpp). No change to
  public core-`iv` headers (`legend.hpp`, `plot.hpp`): `LegendSpec`/`PlotOptions` are untouched.
- **Rotation invariant:** for `angleRad = −π/2` and pivot `O`, a glyph corner at pixel offset
  `(dx, dy)` from `O` (x right, y down) maps to `(dy, −dx)` (advance `+x → −y`, i.e. upward);
  `texcoord`/`glyphLoc`/`color` are byte-unchanged. `angleRad = 0` is the identity (the existing
  horizontal path is unaffected). The label's rotated advance equals its horizontal `measureLabel`
  width.
- **Caption placement invariant:** every glyph quad of the magnitude caption lies with NDC-x
  `< spec.rectNdc[left]` (strictly left of the swatch) and is distributed across the swatch's
  vertical span (not above `rectNdc[top]`). The phase caption and all ticks keep their ADR-0028
  positions.
- **Placement invariant:** after `placeLegendRight`, with a valid projection,
  `swatchLeft ≥ min(defaultLeft, rightEdge − valAllow − width)` and
  `swatchRight + valAllow ≤ rightEdge`; and when `boxRight + gap + capAllow ≤ defaultLeft`,
  `rectNdc` is exactly the default (wide-window no-op). Swatch width is preserved
  (`rectNdc[2] − rectNdc[0]` unchanged). Vertical extent unchanged.
- **Coordinates:** screen overlay is y-down NDC (D-0044); rotation is computed in the y-down
  pixel frame; `placeLegendRight` uses the same `viewProjection`/`projectToPixel` as
  `buildAnnotations` (ADR-0026), so box and legend share one projection.
- **Ownership/threading:** unchanged (ADR-0007/0028) — pure host, mutates only the passed
  `Overlay`/`LegendSpec`, reads the `FontSet`/camera; not thread-safe. No GPU/pipeline change
  (the rotated quads draw through the existing glyph pipeline; the swatch is unchanged).
- **Error semantics:** total functions; degenerate/behind-camera projection leaves the default
  rect; a rotated label containing math rules is a **caller error** (unsupported — captions never
  do this).

## Consequences
- The legend no longer overruns the box in tall/square windows and tracks it live as the user
  orbits/resizes; wide windows are byte-identical to today (the clamp's no-op branch).
- The magnitude title is compact and conventional (rotated, left), shrinking the legend's vertical
  footprint and freeing the space above the bar.
- A general **rotated-label** primitive lands in the text layer — reusable for future rotated axis
  titles or the B-0016 magnitude-axis scientific-notation labels.
- Cost: `placeLegendRight` projects 8 corners per build (negligible). The rotated path adds one
  pixel-space pass over the caption's quads.
- Forecloses nothing: horizontal emission is the `angleRad = 0` default; the swatch, transfer
  consistency (ADR-0028), and caption derivation (ADR-0031) are untouched.

## Alternatives Considered
- **Thread a rotation through `layout`/`appendLabel`/`layoutRun`/`pushGlyphQuad`:** rejected —
  invasive across the whole math/box layout for a feature only the caption needs; the post-emission
  pixel-space rotation of `MixedGlyphs` quads is localized and works for text + simple math alike.
- **Special-case 90° (swap/negate) instead of a general angle:** rejected — a general `angleRad`
  is barely more code, is reusable, and keeps the identity (`0`) path exact.
- **Bump the fixed `rectNdc` default / reserve a fixed-pixel right panel:** rejected — a fixed rect
  cannot track the box across aspect ratios (the actual defect); projecting the box is the direct
  fix. Reserving a pixel panel can't move the full-frame ray-marched volume out of it.
- **Aspect-correct the framing instead (zoom the volume to fit the narrow dimension):** rejected
  here — that changes the plot framing for all aspects (a separate concern); the legend should be
  placed correctly regardless of how the volume is framed.
- **Keep the caption above:** rejected by the maintainer in favor of the compact rotated-left form.

## Verification
- **Rotation primitive (`[text]`/`[math]`):** emit a known glyph horizontally and rotated −π/2 about
  a pivot; assert each quad corner maps `(dx,dy) → (dy,−dx)` in pixel space and `texcoord`/`glyphLoc`
  are unchanged. **Teeth:** wrong sign/axis (e.g. `+π/2`, or rotating texcoord too) lands corners in
  the wrong quadrant / corrupts the glyph → red. `angleRad = 0` reproduces the horizontal quad
  byte-for-byte (identity).
- **Caption side (`[legend]`):** build a legend; the magnitude-caption glyphs all have NDC-x
  `< rectNdc.left` and span the swatch's vertical range. **Teeth:** the old "centered above" path
  puts them above `rectNdc.top` and centered in x → the strictly-left assertion goes red.
- **Aspect-aware placement (`[legend]`):** `placeLegendRight` with a **tall** framebuffer (e.g.
  600×1000) yields `swatchLeft ≥ boxRight + gap` (no box overlap) and `swatchRight + valAllow ≤
  rightEdge` (on screen); with a **wide** framebuffer it yields exactly the default rect.
  **Teeth:** ignoring aspect/box (returning the default) leaves `swatchLeft < boxRight` in the tall
  case → overlap predicate red; dropping the right-edge clamp pushes value labels off screen → red.
- **End-to-end (`[vk][plot]`):** a headless `renderPlot` at a tall aspect shows legend pixels
  entirely right of the projected box; `makePlot` runs validation-clean. Eyeballed before/after.
- **Sanitizers:** ASan+UBSan clean over the new builder/primitive paths.
