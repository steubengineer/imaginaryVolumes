# ADR-0033: Inline Math in Labels — `$…$` Model, LaTeX Subset & OpenType-MATH Layout

- **Status:** Accepted
- **Date:** 2026-06-21
- **Supersedes:** none

## Context
M9's goal is publication-quality **math in labels** without a TeX engine. The maintainer
scoped it precisely: a label is ordinary text that may carry **inline LaTeX math** in
`$…$` islands (e.g. `"Wave $f(x)=\frac{1}{2}$"` ⇒ `Wave ` as text, the rest typeset),
supporting a **small but real** subset — variables, scripts, `\frac`, `\sqrt`, stretchy
`\left…\right`, a curated symbol set. We avoid both poles (linking a TeX engine; owning a
TeX processor) because the **OpenType MATH table externalizes** the positioning constants
TeX hardcodes, and **MathML Core** specifies the finite layout algorithm that consumes
them. Our vendored HarfBuzz already compiles `hb-ot-math.cc` (verified: `src/harfbuzz.cc`
includes it, no `HB_NO_MATH`), exposing `hb_ot_math_*`; ADR-0032 vendors the
**NewCMMath-Book** face that carries the table and provides a **mixed-font** overlay
channel. So we **own a small subset parser + an OpenType-MATH box-layout engine** over
those metrics. Cites ADR-0032 (mixed-font substrate + the math/italic faces), ADR-0022/
0023/0025 (vendored HarfBuzz behind `iv::text`; Slug glyph rendering, headless + present),
ADR-0024/0026 (the label/annotation consumers), ADR-0028/0031 (legend captions / field
name), ADR-0003 (no-throw failure semantics), ADR-0004 (HarfBuzz behind the boundary),
B-0015 (this item), B-0012 (the prerequisite, resolved by ADR-0032).

## Decision
**Interpret every caller-supplied label as text with `$…$` math islands; typeset the math
with an owned LaTeX-subset parser + an OpenType-MATH box-layout engine whose every
constant comes from the font, emitting glyphs through the ADR-0032 mixed-font channel.**

### 1. Label model (`$…$`)
- A label string is a sequence of alternating **text** and **math** spans split on
  **unescaped `$`**. Text spans render through the existing roman path (`appendText`);
  math spans render through the new math layout. `\$` is a **literal dollar** in text mode.
- An **unmatched** `$` is non-fatal (ADR-0003): the trailing run renders as **literal
  text** and a diagnostic is recorded; nothing throws and nothing silently vanishes (§8).
- A label containing **no `$`** is rendered by the **unchanged** text path — math is
  strictly opt-in (the backward-compat invariant, §Contract).

### 2. Supported LaTeX subset (math mode)
The controlled grammar (the maintainer's "small subset, but `\frac`-class expressions"):
- **Atoms:** ASCII letters → **math italic**; digits and punctuation → upright; binary
  operators / relations (`+ - * = < > / | , . ; : ! ( ) [ ]`) → upright with math spacing.
- **Scripts:** `^{…}` superscript and `_{…}` subscript on the preceding atom/group
  (singletons may omit braces: `x^2`); both together; nesting allowed.
- **`\frac{num}{den}`** — built fraction with the rule on the math axis.
- **`\sqrt{radicand}`** and **`\sqrt[index]{radicand}`** — radical with a stretched surd.
- **Accents / overline:** `\hat{·}` and `\dot{·}` (single, non-stretchy accent glyphs
  centered over the nucleus) and `\overline{·}` (a rule spanning the argument's width).
- **`\left<d> … \right<d>`** — auto-sized (stretchy) delimiters; `<d>` ∈
  `( ) [ ] | \{ \} \langle \rangle \lvert \rvert \lVert \rVert .` (`.` = null delimiter).
  The delimiter primitives `\langle \rangle \lvert \rvert \vert \lVert \rVert \mid` are
  also usable as ordinary (non-stretched) symbols.
- **Bra–ket (built on the delimiter machinery):** `\bra{·}`, `\ket{·}`, `\braket{·}`
  (a `·|·` divider auto-sized), and `\ketbra{·}{·}` — i.e. `\ket{\psi}` → `|ψ⟩`,
  `\braket{\phi|\psi}` → `⟨φ|ψ⟩`, sizing to tall content via `\left…\right`. These are
  convenience expansions over `\langle`/`\rangle`/`\lvert`/`\rvert`/`\mid`; the primitives
  remain available for hand-built variants.
- **Grouping `{ … }`** — an invisible group (one atom for scripts/args).
- **Style switches:** `\mathrm{…}` (upright roman, via the roman face), `\mathbf{…}`
  (bold, via the math face's bold alphabet), `\mathit{…}` (italic; the math default).
- **Spacing:** `\,` `\;` `\quad` `\qquad` `\!`.
- **Macro table (curated, the extension point):** Greek lower/upper (`\alpha`…`\omega`,
  `\Gamma \Delta \Theta \Lambda \Xi \Pi \Sigma \Upsilon \Phi \Psi \Omega`); common
  symbols/relations/operators (`\times \cdot \pm \mp \leq \geq \neq \approx \equiv \to
  \rightarrow \leftarrow \infty \partial \nabla \forall \exists \in \langle \rangle \hbar
  \ell`); large operators (`\sum \int \prod`, taking `^`/`_` as limits/scripts).
- **Unknown control sequence / malformed input** (e.g. `\foo`, a `\frac` missing an
  argument): the **defined fallback** is to render the offending source **literally** in
  upright roman and record a non-fatal diagnostic — never throw, never silently drop
  (ADR-0003; §8 forbids dropping a concern). *(Maintainer may choose "skip" or "hard
  error" instead at acceptance.)*

### 3. Layout = OpenType MATH box model (no hardcoded constants)
Lay out each math span with the MathML-Core / OpenType-MATH algorithm for the subset
above, building boxes (horizontal list, fraction, scripts, radical, stretchy delimiter,
accent, overline) whose leaves are positioned glyphs:
- **All positioning constants from the font** via `hb_ot_math_get_constant` (axis height,
  fraction rule thickness + numerator/denominator shifts & gaps, super/subscript shifts &
  gaps, radical rule thickness/vertical gap/extra ascender, **accent base height & top-
  accent attachment**, **overbar vertical gap / rule thickness / extra ascender**,
  `Script`/`ScriptScript` percent scale-downs, …); **italic correction** via
  `hb_ot_math_get_glyph_italics_correction`; **math cut-ins** via
  `hb_ot_math_get_glyph_kerning`; **stretchy** delimiters/radicals via
  `hb_ot_math_get_glyph_variants` / `hb_ot_math_get_glyph_assembly`.
- A **style chain** (display → text → script → scriptscript) selects the scale for
  scripts, fraction parts, and roots. **No TeX magic numbers are hardcoded** — this is the
  invariant that keeps the engine "not a TeX processor" (and makes it correct for any
  OpenType MATH font).
- Output is positioned glyph quads emitted through the **ADR-0032 mixed-font** channel
  (math face for symbols/math-italic/bold; roman face for `\mathrm` and text spans), in
  the same `Overlay` the labels already use.

### 4. Public surface & consumers
- Label fields keep their type (`std::string`); only their **interpretation** changes.
  Math-aware fields are the **caller-supplied** label strings: `PlotAxes` title / per-axis
  label / unit; `LegendSpec` `fieldName` / `magnitudeLabel` / `phaseLabel`. `fieldName` is
  treated as a general label string like the others; its **default becomes `"$f$"`** (so
  the field renders in math italic by default — fulfilling ADR-0031's deferred italic;
  a caller wanting upright sets `"f"`, or a Greek field `"$\Phi$"`). This **amends the
  ADR-0031 default** (`"f"` → `"$f$"`). The caption derivation is otherwise unchanged
  (`"|"+fieldName+"|"`, `"arg("+fieldName+")"`): the structure (`|`, `arg(`, `)`) stays
  upright text and the field renders as math, e.g. `|𝑓|`, `arg(𝑓)`. A fully-math caption
  (e.g. `\lvert·\rvert`) remains available via the `magnitudeLabel`/`phaseLabel` override.
- **Out of scope for M9 (unchanged, literal Unicode):** auto-generated numeric **tick**
  labels and the fixed legend phase ticks (`-π`/`0`/`π`). The maintainer **does** want
  scientific/superscript notation (`1×10⁻³`) on the **legend's magnitude axis**; it is a
  generated-label change deferred as a clean follow-on → **B-0016** (not M9 scope).
- The parser + layout live in **`iv::text`** (need HarfBuzz + the faces). **No HarfBuzz
  type in any `include/` header** (ADR-0004). The renderer is unchanged beyond ADR-0032.

## Contract Specification
- **Split rule (assertable):** label → spans by unescaped `$`; `\$` → literal `$`; `$`
  count odd ⇒ last span literal + diagnostic. Text spans ≡ `appendText`.
- **Backward-compat invariant (assertable):** a label with no `$` yields a glyph stream
  **byte-identical** to the pre-M9 path. The only intended incompatibility: a literal `$`
  now requires `\$` (documented).
- **Subset (assertable):** exactly the constructs in §2 are recognized; an unrecognized
  control sequence triggers the §2 fallback (literal + diagnostic), deterministically.
- **Metrics-from-font invariant (assertable):** every super/subscript shift, fraction/
  radical metric, axis position, script scale, and delimiter size is derived from
  `hb_ot_math_*` for the active face — **not** a hardcoded constant. (Tested by zeroing a
  MATH constant and observing the layout move.)
- **Failure semantics (ADR-0003):** malformed/unknown input never throws and never
  silently drops; allocation failure propagates as `std::bad_alloc` (fatal), as elsewhere
  in `iv::text`.
- **Isolation/boundary:** math is `iv::text`; the text-free build is unaffected; no
  HarfBuzz type crosses an `iv` public signature.

## Consequences
- Real math (`f(x)=\frac{1}{2}`, roots, scripts, stretchy delimiters, Greek/symbols) in
  every caller label, headless and in the viewer — the publication-quality payoff of the
  M1–M8 arc — with **no** TeX engine and **no** new third-party code (HarfBuzz + the
  ADR-0032 faces suffice).
- We own a bounded, spec-backed engine (~the milestone's main implementation): a small
  subset parser + the OpenType-MATH box layout. Because the font supplies the constants,
  it stays finite and is correct for any OpenType MATH font.
- A documented source incompatibility (`$` ⇒ `\$`); the subset is explicitly partial —
  unknown macros fall back rather than typeset. Extending the macro table is additive.
- Forecloses nothing: paragraph/multiline math, alignment, or a wider symbol set can be
  added later behind the same model.

## Alternatives Considered
- **Link a TeX engine / shell out to LaTeX (dvisvgm):** rejected by the maintainer — heavy
  runtime dependency, process spawning, resolution-locked raster; betrays owning the stack.
- **Embed a C++ math-subset library (microTeX / cLaTeXMath):** rejected — tens of kLOC
  bringing its own font world + graphics backend; an adapter + convention-leak burden
  (ADR-0004/§8) far larger than the subset warrants, when HarfBuzz already gives the MATH
  metrics.
- **Hand-rolled super/subscript offsets, no engine:** rejected — cannot honestly do
  `\frac`/radicals/stretchy delimiters, which the maintainer explicitly requires.
- **Make all labels (incl. generated tick numbers) math:** deferred — keeps the M9 scope
  bounded and the backward-compat invariant clean; a possible follow-on.

## Verification
- **Split/escape (teeth):** `"a $x$ b"` → text `"a "`, math `"x"`, text `" b"`; `"\$5"` →
  literal `$5`. Teeth: treat `$` as literal (skip the split) → a text run contains `$` /
  the math span never forms → red.
- **Parser structure (teeth):** `\frac{1}{2}`, `x^2_i`, `\sqrt[3]{x}`, `\left(\frac a b
  \right)` parse to the expected box trees. Teeth: swap `\frac` numerator/denominator, or
  mis-scope a script, → the tree (and the rendered positions) diverge from the reference →
  red.
- **Layout from font metrics (the core teeth):** assert a superscript is raised, a
  subscript lowered, the fraction rule sits at the math axis with the numerator above /
  denominator below, the radical rule clears the radicand, and `\left(…\right)` around a
  tall sub-box is taller than a plain `(`. **Teeth:** zero a `hb_ot_math` constant (axis
  height / fraction rule thickness) **or** skip the italic correction → the recorded
  reference positions move → red; restore → green. This pins "metrics come from the font."
- **Backward-compat invariant (teeth):** a `$`-free axis/title/legend label yields a glyph
  stream identical to the pre-M9 output. Teeth: math-path leakage into a plain label →
  stream differs → red.
- **Mixed-face usage (teeth):** a rendered math span uses ≥ 2 faces (math + roman for a
  `\mathrm`), all non-`.notdef`. Teeth (with ADR-0032): wrong-atlas binding → garbage.
- **Fallback (teeth):** `\foo` renders visible literal glyphs + records a diagnostic (does
  not throw, does not vanish). Teeth: a silent drop → the run is empty → red.
- **End-to-end:** a math-labeled plot renders headless validation-clean (`renderPlot`) and
  in the viewer (`makePlot`); the title/legend math is crisp at two scales (zoom
  invariance, reusing the ADR-0023 check).
