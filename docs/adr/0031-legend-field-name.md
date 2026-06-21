# ADR-0031: Legend Field Name

- **Status:** Accepted
- **Date:** 2026-06-20
- **Supersedes:** none

## Context
The legend (ADR-0028) carries literal caption strings — `LegendSpec::magnitudeLabel` (default
`"|z|"`) and `phaseLabel` (default `"arg z"`) — also on `PlotOptions` (ADR-0029). The maintainer
wants the caller to **name the field** being visualized (e.g. `"Phi"`) and have the legend derive
both captions — `|Phi|` over the magnitude axis and `arg(Phi)` under the phase axis — while
**keeping the explicit caption strings as an override** (escape hatch) for full flexibility. The
field name is **distinct from the verbose plot title** `PlotAxes::title` (ADR-0024).

Italic note: math variables are conventionally italic, but the bundled NCM-Book face has **no
italic glyphs** (no Mathematical Alphanumeric block — the same gap that dropped `ℓ`) and the
glyph overlay is **single-atlas** (ADR-0023/0025), so true italic needs a second font face +
mixed-font (multi-atlas) glyph support — an architectural change **deferred to the visual-polish
pass** (Backlog B-0012). The field name renders **upright** (roman) for now.

Read: ADR-0028 (`LegendSpec`, `buildLegend` captions), ADR-0029 (`PlotOptions` fan-out), ADR-0024
(`PlotAxes::title`), ADR-0023/0025 (single-atlas glyph overlay).

## Decision
**Additive.** On both `iv::LegendSpec` and `iv::PlotOptions`:
- **keep** `magnitudeLabel` / `phaseLabel` as explicit overrides, but **default them empty**
  (were `"|z|"` / `"arg z"`);
- **add** `std::string fieldName` (default `"f"`).

The caption used is the explicit label when non-empty, else derived from the field name:
- magnitude caption = `magnitudeLabel` if set, else `"|" + fieldName + "|"`;
- phase caption = `phaseLabel` if set, else `"arg(" + fieldName + ")"`;
- an empty field name (and empty override) → no caption.

The derivation lives on `LegendSpec` as two pure const methods (`magnitudeCaption()`,
`phaseCaption()`) so it is unit-testable by string equality. Captions render with the roman
shaper (upright). The facade fans `PlotOptions::{magnitudeLabel, phaseLabel, fieldName} →
LegendSpec`. `PlotAxes::title` is unchanged and independent.

## Contract Specification
- `iv::LegendSpec`: `magnitudeLabel{""}`, `phaseLabel{""}` (overrides, now default empty),
  `fieldName{"f"}`; `[[nodiscard]] std::string magnitudeCaption() const` (= `!magnitudeLabel
  .empty() ? magnitudeLabel : fieldName.empty() ? "" : "|"+fieldName+"|"`) and
  `phaseCaption() const` (= `!phaseLabel.empty() ? phaseLabel : fieldName.empty() ? "" :
  "arg("+fieldName+")"`).
- `iv::PlotOptions`: `magnitudeLabel{""}`, `phaseLabel{""}`, `fieldName{"f"}`.
- `buildLegend` draws `spec.magnitudeCaption()` / `spec.phaseCaption()` (skipping empties), with
  the roman shaper (upright).
- No change to the swatch, ticks, thickness correction, or `PlotAxes::title`. True italic of the
  field name is **out of scope** (Backlog B-0012).

## Consequences
- Name the field once → consistent `|·|` / `arg(·)` captions; the explicit labels remain for full
  control (e.g. units in the caption).
- Default field `"f"` → `|f|` / `arg(f)`; the default phase caption gains parentheses vs the old
  `"arg z"`.
- The default `f` is **upright** until mixed-font support (B-0012) lands; then it becomes italic
  with no API change (a rendering-only refinement).

## Alternatives Considered
- **Replace the caption fields (non-additive):** the maintainer chose additive — keep the escape
  hatch.
- **Default field `"z"`:** the maintainer chose `"f"`.
- **True italic now / oblique slant:** true italic needs the deferred multi-atlas change (B-0012);
  an oblique-sheared roman `f` was rejected as a fake that won't match the CM italic letterform.

## Verification
- **Caption derivation (`[legend]`):** `LegendSpec{fieldName="Phi"}.magnitudeCaption() == "|Phi|"`
  and `.phaseCaption() == "arg(Phi)"`; default `fieldName=="f"` → `"|f|"` / `"arg(f)"`; a set
  `magnitudeLabel` overrides the derived one; empty field + empty override → `""` / `""`.
  **Teeth:** dropping the bars / `arg(...)` wrapper, or the override precedence, diverges.
- **Legend uses them (`[legend]`):** building with a non-default `fieldName` changes the legend's
  caption glyphs; an explicit override label changes them differently.
- **Facade fan-out (`[vk][plot]`):** `renderPlot` / `makePlot` with `PlotOptions::fieldName` (or
  an explicit label) set produce the corresponding captions; validation-clean.
