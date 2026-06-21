# DECISIONS.md — imaginaryVolumes

This is the decision journal (DEV_PROCESS §2.8): architecturally significant
choices and the rationale that drove them, plus the Backlog (roads not taken and
review concerns not yet addressed). It is lighter-weight than an ADR; choices
with public-contract impact (§1.1) *also* get an ADR, referenced here.

## Decision Log
(Newest first. The founding set, D-0001…D-0008, was all decided on 2026-06-18
during project initiation; D-0009…D-0010 were added during M1's CONTRACT phase
the same day. Future entries prepend above.)

### D-0048 — M9 math typesetting via an owned subset engine over OpenType MATH (not a TeX engine)
- **Date / milestone:** 2026-06-21 / M9 (CONTRACT) — maintainer-approved
- **Choice:** How to render inline LaTeX math in labels (`"Wave $f(x)=\frac{1}{2}$"`) between two
  rejected poles: linking/calling a **TeX engine** (heavy runtime dep) vs. **owning a TeX
  processor** (heavy ownership). Options surveyed: shell out to LaTeX (dvisvgm); embed a C++
  math-subset lib (microTeX/cLaTeXMath); own a small subset parser + OpenType-MATH layout;
  hand-rolled offsets.
- **Decision:** **Own a small, spec-backed engine** — a recursive-descent parser for a controlled
  LaTeX subset feeding an OpenType-MATH box-layout engine, taking **every positioning constant
  from the font** via HarfBuzz's `hb_ot_math_*` API (confirmed compiled into our vendored
  HarfBuzz: `src/harfbuzz.cc` includes `hb-ot-math.cc`, no `HB_NO_MATH`) and the bundled
  **NewCMMath** face, emitting glyphs through a new **mixed-font** overlay channel. Split into
  **two ADRs**: ADR-0032 (mixed-font/multi-atlas substrate + vendor the GFL italic & math faces;
  resolves the B-0012 prerequisite) then ADR-0033 (the `$…$` model, subset grammar & layout).
- **Rationale:** The OpenType MATH table externalizes exactly the font-specific constants classic
  TeX hardcodes, and MathML Core specifies the finite consuming algorithm — so "own the math
  layout" shrinks to a bounded, reference-documented box-assembly over a subset we control, reusing
  what we already vendor (HarfBuzz + Slug) with **zero new third-party code**. Embedding microTeX
  was rejected as tens of kLOC + its own font/graphics world (convention-leak burden, ADR-0004/§8)
  for a small subset; a TeX engine/shell-out rejected as heavy + resolution-locked.
- **Contract impact:** ADR-0032 + ADR-0033 (both Accepted 2026-06-21). Subset redlines (maintainer):
  add `\hat`/`\dot`/`\overline` + full bra–ket; no matrices; `fieldName` default → `"$f$"`;
  legend scientific-notation deferred → B-0016.
- **Deferred alternatives:** TeX engine / LaTeX shell-out; microTeX embed; hand-rolled offsets —
  not re-litigated absent a subset outgrowing the owned engine.

### D-0047 — Legend field name (additive; default upright "f"; true italic deferred)
- **Date / milestone:** 2026-06-20 / post-M8 — maintainer decision
- **Choice:** How the caller names the field shown on the color legend (its `|·|` / `arg(·)`
  captions), distinct from the verbose plot title, and how to render the math-variable italic.
- **Decision:** **Additive** — keep `LegendSpec`/`PlotOptions` `magnitudeLabel`/`phaseLabel` as
  explicit overrides (now **default empty**) AND add `fieldName` (default `"f"`); the caption is
  the override if set, else derived `"|"+fieldName+"|"` / `"arg("+fieldName+")"` (via `LegendSpec`
  methods). Default `"f"` → `|f|`, `arg(f)`. Rendered **upright** for now: true (CM) italic needs
  a second font face + mixed-font **multi-atlas** glyph support (the overlay is single-atlas,
  ADR-0023/0025), deferred to the visual-polish pass (**B-0012**); an oblique-sheared roman `f`
  was rejected as a fake. The upright→italic switch will be rendering-only (no API change).
- **Rationale:** Naming the field once is the least error-prone API; the override keeps full
  flexibility; deferring true italic avoids an architectural change inside a small feature.
- **Contract impact:** ADR-0031 (Accepted); additive amendment of the ADR-0028/0029 caption
  defaults (`magnitudeLabel`/`phaseLabel` now default empty).
- **Deferred alternatives:** replace (non-additive); default `"z"`; oblique slant; **true italic
  now → B-0012**.

### D-0046 — Thickness-corrected legend opacity (tunable reference thickness + label)
- **Date / milestone:** 2026-06-20 / post-M8 — maintainer decision
- **Choice:** The legend (ADR-0028) plots the per-sample opacity `a = transferOpacity(m)`, but
  the volume accumulates opacity along each ray (ADR-0012/0020), so the legend reads far more
  transparent than the render — a low-magnitude field shows a near-black legend beside a visibly
  opaque volume. How to correct the legend for this "thickness" effect, given the per-ray path
  length varies and a true full-cube correction saturates the swatch.
- **Decision:** Plot `A = 1 − (1−a)^(256·ℓ_ref)` (the ADR-0020 accumulation over a reference
  thickness; `256 = kReferenceSteps`) via a new host evaluator `iv::accumulatedOpacity`. `ℓ_ref`
  is a **tunable** reference thickness with a **soft default 0.1** (a readable gradient, not the
  saturating full-cube 1.0) — new fields `LegendSpec::referenceThickness`,
  `PlotOptions::legendThickness`, and a **render-inert** `RenderParams::legendThickness` (the
  one live channel the ADR-0029 closure reads; ignored by the ray-march). The legend draws a
  **text label of `ℓ_ref`** so the displayed `A` stays analytically invertible to `a`. Viewer
  `[` / `]` adjust it live (additive 0.02, clamp `[0,2]`, `0` = uncorrected per-sample legend).
- **Rationale:** Reuses the ADR-0020 model so legend↔render consistency extends to thickness;
  tunability + the label give an honest, readable, quantitative legend without a per-ray "true"
  thickness that doesn't exist. The soft default avoids the full-cube saturation the maintainer
  rejected.
- **Contract impact:** ADR-0030 (Accepted). Adds `iv::accumulatedOpacity`/`kReferenceSteps` +
  the three `*Thickness` fields; no existing signature changes; `0` reproduces ADR-0028.
- **Deferred alternatives:** full-cube (`ℓ_ref=1`) default; a fixed thickness with no knob; a
  separate viewer live-param channel (vs the render-inert RenderParams field); retuning the
  default after eyeballing real data.

### D-0045 — Legend swatch is truly transparent (no opaque backing); corrects ADR-0028 detail
- **Date / milestone:** 2026-06-20 / M8 (post-completion polish) — maintainer feedback
- **Choice / finding:** ADR-0028's Decision described the swatch as "composited over an opaque
  backing quad ... so partial opacity reads against a known tone." In the viewer the maintainer
  observed this makes the legend an opaque object whose backing tone (0.12 gray) differs from
  the plot background and **desaturates** partial-opacity cells (mixing every hue toward gray).
- **Decision:** Remove the opaque backing. The swatch composites straight over the scene with
  its real alpha, so the legend **truly reflects transparency** and matches the plot's
  saturation (a low-opacity cell blends toward the same background as the data). Border, ticks,
  and labels are unchanged.
- **Rationale:** A colorbar with alpha must blend over the same background as the data to read
  correctly; any opaque backing tone contaminates the hues. True transparency is also exactly
  what the legend communicates (magnitude → opacity).
- **Contract impact:** none. ADR-0028's binding contract (host evaluators, screen-space
  channels, the legend drawing through the evaluators) is unchanged; this corrects the
  non-binding "opaque backing" detail in its Decision prose — a refinement like D-0016/D-0020,
  not a reversal, so no superseding ADR. The `[vk][legend]` teeth still hold (empty volume over
  black: top opaque cyan, bottom transparent → black).
- **Deferred alternatives:** an optional faint backing for readability over busy data (a future
  LegendSpec field if it proves needed).

### D-0044 — ADR-0028 screen-space overlay is Vulkan clip space (y-down), not y-up
- **Date / milestone:** 2026-06-20 / M8 (IMPLEMENT, ADR-0028)
- **Choice / finding:** ADR-0028's Contract Specification noted screen-space overlay geometry
  as "clip-space NDC, y-up" (and `rectNdc` "with y up"). Implementing against the existing
  overlay shader (`overlay.vert` applies `transform * inPos` directly) and `appendText`'s
  pixel→clip mapping (`ndcY = py/halfH − 1`, top-left origin) showed the identity-transform
  screen space is **Vulkan clip space, y-DOWN** (y = −1 top, +1 bottom) — the same convention
  as all post-transform overlay geometry (ADR-0021/0026) and the baked glyphs.
- **Decision:** Use **y-down** for the new `Overlay::screenLines/screenTriangles` and for
  `LegendSpec::rectNdc` (= {left, top, right, bottom}, top < bottom), documented in both
  headers. The legend's "magnitude increases upward" is realized by mapping the normalized
  position v=1 to the top (the smaller y). No code relies on the y-up reading.
- **Rationale:** Consistency with the established overlay/glyph convention; a y-up screen
  channel would need a hidden flip in the renderer and disagree with the world geometry.
- **Contract impact:** none to ADR-0028's binding decision (legend content, host evaluators,
  the screen-space channel mechanism). Corrects only the ADR's coordinate narrative — a
  refinement like D-0016/D-0020, not a reversal, so no superseding ADR.

### D-0043 — M8 high-level plot facade: return a configured Viewer + headless renderPlot
- **Date / milestone:** 2026-06-20 / M8 (CONTRACT) — maintainer decision
- **Choice:** The shape of the one-call convenience API over the multi-step
  Volume/Viewer/axes/legend setup (`examples/view_demo.cpp` does it by hand): a blocking
  `plot()` that opens the window and runs to close, vs. returning a configured Viewer the
  caller drives.
- **Decision:** **Return a configured (not-yet-running) `Viewer`** from
  `iv::makePlot(field, dims, options)` (caller does `->run()`, may edit `params()` first),
  plus a headless `iv::renderPlot(field, dims, w, h, options) → ImageReadback` for
  scripts/CI. A `PlotOptions` aggregate holds the transfer state **once** (colormap/opacity/
  density/decades) and the facade fans it out to **both** the `RenderParams` and the
  `LegendSpec`, so the legend can't disagree with the image. `makePlot` installs a
  `setOnFrame` closure that owns its `Shaper`+`PlotAxes`+`LegendSpec` (via a copyable
  `shared_ptr`) and rebuilds box/axes (ADR-0026) + legend (ADR-0028) from the **live**
  params each frame, keeping `Viewer` text-agnostic. The facade lives **outside core `iv`**:
  `renderPlot` needs `IV_BUILD_TEXT`, `makePlot` needs `IV_BUILD_VIEWER && IV_BUILD_TEXT`;
  both isolation gates stay green.
- **Rationale:** Returning a configured Viewer is strictly more flexible than a blocking
  call (tweak params, drive `runFrames`, embed) at one extra line; the single-source
  transfer state structurally prevents legend/render mismatch; keeping the facade out of
  core preserves the GLFW-free / HarfBuzz-free core (D-0001/D-0033).
- **Contract impact:** ADR-0029 (Proposed).
- **Deferred alternatives:** a blocking `plot()` (writable as `makePlot(...)->run()`); a
  PNG-writing `savePlot()`; embedding a full `RenderParams` in `PlotOptions`.

### D-0042 — M8 legend: 2-D phase×magnitude swatch via shared host transfer evaluators
- **Date / milestone:** 2026-06-20 / M8 (CONTRACT) — maintainer decision
- **Choice:** What the legend depicts and how it is kept consistent with the active
  transfer function/colormap (ADR-0013/0014/0020/0027). Options for the form: a discrete
  phase color wheel + a separate opacity bar, vs. a unified 2-D swatch.
- **Decision:** A single **rectangular 2-D gradient swatch** to the right of the volume —
  **color horizontal with phase** (−π…+π), **opacity vertical with magnitude** — with phase
  ticks **−π, 0, +π** on the bottom and **nice-number** magnitude ticks (`ticksFor`,
  ADR-0024) on the right. Consistency is guaranteed by factoring the shader's two mappings
  into **pure-host core-`iv` evaluators** — `iv::phaseColor` (mirrors `sampleColor`; mode 0
  shares the committed `kTwilightLut`, so host==GPU by construction), `iv::transferNormalized`
  (magnitude→position, no density) and `iv::transferOpacity` (= `clamp(normalized·density)`,
  mirrors `sampleOpacity` pre-dt-correction). The legend draws **only** through these. The
  `Overlay` gains screen-space `screenLines`/`screenTriangles` (identity transform, extends
  ADR-0021) so the 2-D legend and the 3-D box/axes coexist in one Overlay, drawn identically
  headless and in the viewer. `iv::text::buildLegend` (in `iv_text`, needs a `Shaper`)
  appends the swatch/ticks/labels.
- **Rationale:** The 2-D swatch shows the *joint* (phase×magnitude) transfer in one figure
  (maintainer's design); shared host evaluators make "legend matches the render" a
  constructive guarantee + a direct GPU cross-check teeth (perturb an evaluator → legend and
  a uniform-field pixel diverge → red); per-vertex color/alpha through the existing pipelines
  adds no new pipeline/feature (keeps the ADR-0019 perf contract + pixel-exact tests).
- **Contract impact:** ADR-0028 (Proposed; extends ADR-0021, mirrors ADR-0013/0014/0027).
- **Deferred alternatives:** a discrete color wheel + opacity bar; a GPU-LUT-textured legend
  quad; log-spaced (decade) magnitude ticks in log mode; the ADR-0027 dt-corrected α in the
  legend (rejected — the legend depicts the per-sample transfer, not ray integration).

### D-0041 — Log-scale decade window for the opacity transfer function
- **Date / milestone:** 2026-06-19 / post-M7 (standalone) — maintainer decision
- **Choice:** Let the caller control how many decades of magnitude the logarithmic
  opacity scale displays (ADR-0013 windowed it over the data's full
  `[minPositive, max]` range, so the decade count was fixed by the data).
- **Decision:** Add public `RenderParams::logDecades` (float, default 0). In log mode,
  `> 0` windows the ramp to the top `logDecades` decades below `max`
  (`mn = clamp(1 + log10(m/max)/logDecades, 0, 1)`); `0` keeps the unchanged ADR-0013
  mapping (backward-compatible). Packed into a spare `modes` UBO slot (no std140/size
  change); live (no re-upload). Demo: `--decades N`; viewer: `[`/`]` hotkeys (repeat),
  which also switch to log mode.
- **Rationale:** High-dynamic-range fields compress the interesting top end under the
  full-range log map; a data-independent decade window is the maintainer's mental model.
- **Contract impact:** ADR-0027 (Accepted; extends ADR-0013).
- **Deferred alternatives:** per-decade gamma; a movable window center (anchored at
  `max` for now); absolute-floor windowing.

### D-0040 — Anti-aliased overlay lines via VK_EXT_line_rasterization (smooth)
- **Date / milestone:** 2026-06-19 / post-M7 graphics polish
- **Choice:** How to eliminate aliasing on the overlay's box/axis/tick lines (the
  maintainer flagged them as jagged) without an MSAA pipeline.
- **Decision:** `Context::create` **opportunistically** enables
  **`VK_EXT_line_rasterization`** + the **`smoothLines`** feature when the device
  supports it (both the RTX 4070 and llvmpipe here do), exposed via
  `Context::smoothLinesAvailable()`. The renderer then builds the overlay **line**
  pipeline with `lineRasterizationMode = eRectangularSmooth` (coverage→alpha,
  composited by the existing alpha blend); triangles/glyphs already AA (fill / Slug
  coverage). **Graceful fallback** to aliased lines when unavailable — no public API
  change, no MSAA, no perf cost on the volume.
- **Rationale:** Targeted, device-agnostic line AA; avoids the architectural cost of an
  MSAA color attachment + resolve over the single-sample compute output (which would
  also break the pixel-exact renderer tests and the ADR-0019 perf contract). A Vulkan
  device extension is part of the already-sanctioned Vulkan dependency (ADR-0001), so
  no new-dependency ADR; it preserves ADR-0021's overlay contract (refinement only).
- **Contract impact:** none (internal quality refinement; ADR-0021/0026 preserved).
- **Deferred alternatives:** MSAA / supersampling (heavier, breaks pixel tests + perf);
  hand-rolled AA-line quads (more code).

### D-0039 — M7 scene annotations: projection, outer-edge labels, through-axes
- **Date / milestone:** 2026-06-19 / M7 (CONTRACT) — maintainer-approved
- **Choice:** How the box/axes/ticks/labels are placed and which box edges carry
  labels so they never overlap the data.
- **Decision:** A host **view-projection matching the ADR-0012 ray camera** drives
  world-space box/tick/through-axis lines via `Overlay::transform`; **labels are
  screen-space glyphs** (upright, fixed size). A cube edge is **outer/silhouette** iff
  its two faces differ in facing (`sign(n₁·d) ≠ sign(n₂·d)`); `Outer` box ticks and the
  per-axis **label edge** (chosen by a **down-and-out** screen preference, labels
  offset outward) both use it, so labels lie outside the projected box. The silhouette
  is used for label anchoring even when the box isn't drawn. Through-axes draw line +
  ticks but **no labels**. A host builder fills an `Overlay` from `PlotAxes` + camera +
  `Shaper`, identical headless + viewer.
- **Rationale:** Outer-edge labels are the publication convention and provably avoid
  data overlap; the exact silhouette test is cheaper than a convex hull.
- **Contract impact:** ADR-0026 (Accepted). Deferred refinement: label-edge hysteresis
  to damp swap-flicker on camera motion.

### D-0038 — M7 present-path glyph rendering: persist + split by change rate
- **Date / milestone:** 2026-06-19 / M7 (CONTRACT) — maintainer-approved
- **Choice:** How viewer text (B-0010) reaches the present path without per-frame
  allocation churn.
- **Decision:** Persist the glyph resources in the `Renderer` across the in-flight
  frame (ADR-0017, one frame in flight → overwrite is safe). **Split by change rate:**
  the **atlas + descriptor** rebuild only when text content changes; the **vertex
  buffer** is grown-on-demand and overwritten every frame (quad positions move with the
  camera). `recordFrame()` then draws `Overlay::glyphs` byte-identically to `render()`,
  closing the ADR-0023/D-0036 headless-only scope and resolving **B-0010**.
- **Rationale:** Avoids per-frame descriptor/buffer-view creation; glyph outlines are
  camera-independent so only positions need re-upload.
- **Contract impact:** ADR-0025 (Accepted).

### D-0037 — M7 plot model: declarative axes, data-unit placement, nice ticks
- **Date / milestone:** 2026-06-19 / M7 (CONTRACT) — maintainer decision
- **Choice:** The public model for mapping the `[0,1]³` grid to physical coordinates
  and declaring the plot's annotations.
- **Decision:** A **declarative `iv::PlotAxes`** (pure host, core `iv`): per-axis
  `{min, max, label, unit, majorCount?, minorCount?}` + title; **every element
  optional** (box, box ticks, tick labels, axis labels, title). Ticks are **auto nice
  numbers** (`{1,2,5}·10ᵏ`, Heckbert) — **major + minor**, optional per-axis counts,
  **only major ticks labeled** (value only; unit on the axis label). **Reference axes
  through the volume** (`ThroughAxis`) are placed in the caller's **data units** (never
  world `[0,1]`); they carry ticks but **no labels**. Box ticks default to **`Outer`**
  (silhouette edges) for clarity, with **`AllFaces`** available.
- **Rationale:** Lowest caller effort; callers stay in their own units; per-element
  optionality spans minimalist→fully-annotated. Maintainer chose declarative over
  caller-supplied ticks, data-unit over world-unit locations, and outer-only default.
- **Contract impact:** ADR-0024 (Accepted). Deferred: log axes, custom formatters,
  π-multiple ticks.

### D-0036 — M6 Slug glyph rendering: integration specifics + headless-only scope
- **Date / milestone:** 2026-06-19 / M6 (IMPLEMENT, ADR-0023)
- **Choice:** How libharfbuzz-gpu's experimental Slug encoder/shaders bind to our
  Vulkan overlay, and how much of the path M6 ships.
- **Decision / findings:**
  - **Atlas format:** the Slug encoder emits **RGBA16I** texels (4×int16, 8 bytes;
    the source of the ±8000-unit coordinate range). The atlas is stored int16 and
    uploaded as an **R16G16B16A16_SINT uniform texel buffer**; the shader's
    `isamplerBuffer hb_gpu_atlas` auto-binds to **set 0 / binding 0**.
  - **Vendored GLSL → Vulkan:** the HB GLSL is OpenGL-3.3 style (its only uniform is
    the opaque atlas sampler). It compiles to Vulkan SPIR-V via our `glslc` with
    **`-fauto-bind-uniforms -fauto-map-locations`** and `-I third_party/harfbuzz/src`
    (`#include`d into our `shaders/glyph.frag`). Glyph outlines are encoded in **font
    units** (a separate upem-scaled `hb_font`) to satisfy the quantization.
  - **No dilation:** glyph quad corners are pre-projected to clip space (NDC) on the
    CPU and padded ~5% em, so we skip the HB half-pixel `hb_gpu_dilate` vertex helper;
    the vertex stage is a pass-through and the analytic Slug coverage gives the AA.
  - **Scope (deviation):** glyphs render on the **headless `render()` path** — the
    ADR-0023 verification path (coverage readback + zoom). The **present/viewer path
    draws lines/triangles only**; per-frame Slug glyph plumbing is **deferred to M7**
    (the annotation layer that decides the viewer's text). This narrows ADR-0021's
    "identical headless and windowed" for the glyph subset only — lines/triangles
    stay identical in both paths. **B-0010** tracks the present-path work.
  - **Glyph pipeline lives in core `iv`** (a graphics pipeline over an int texel
    buffer — no HarfBuzz link); only the *data* (atlas + quads) comes from `iv_text`
    (`appendText`). So the glyph shaders compile even with `IV_BUILD_TEXT=OFF`.
- **Rationale:** Confirmed the experimental Slug path works in Vulkan (no MSDF
  fallback needed, ADR-0023); headless rendering proves the technique and fully
  satisfies the ADR while keeping the renderer changes bounded.
- **Contract impact:** Implements ADR-0023; the headless-only scope is the recorded
  deviation (DEV_PROCESS §; cf. D-0030). MSDF fallback remains documented, unused.

### D-0035 — M6 HarfBuzz acquisition: concrete pin, vendored shape, bundled face
- **Date / milestone:** 2026-06-19 / M6 (IMPLEMENT, ADR-0022)
- **Choice:** The concrete facts ADR-0022 deferred "to acceptance": which commit,
  which files, which font face, and how the default font is delivered.
- **Decision:**
  - **Pin:** HarfBuzz `main` @ **`ac0979b6f44b41894c73fd208a0b4f5a8c6dc6ff`**
    (2026-06-18). A commit, not a tag, because `libharfbuzz-gpu` (the Slug renderer,
    D-0034) exists **only on `main`** — no tagged release ships it yet.
  - **Vendored shape:** a near-verbatim mirror of upstream `src/` (auditable by
    `diff -r`), excluding only non-compiled extras — `*.py` generators, the
    `wasm/`/`rust/`/`ms-use/` integration dirs, and the raw non-GLSL GPU shader
    sources. The kept set is exactly the transitive compile closure of
    `src/harfbuzz.cc` + `src/hb-gpu*.cc`, verified to build with our GCC at C++23.
    Dropping `ms-use/` also keeps the drop single-licensed (top-level Old MIT only).
  - **Build:** one static lib `harfbuzz_core` from the amalgamated `src/harfbuzz.cc`,
    **no `HAVE_*` defines** (built-in OpenType + UCD; no ICU/GLib/Cairo/FreeType),
    `SYSTEM` includes, not under `-Werror`. Gated by `IV_BUILD_TEXT` (default ON);
    `OFF` is the text-free isolation gate (mirrors `IV_BUILD_VIEWER`).
  - **Font:** bundle **`NewCM10-Book.otf`** (NCM 8.1.0, the GFL Roman "Book" weight —
    the package default; ~0.68 MB), **embedded** as a byte array
    (`tools/embed_bytes.cmake` → `iv::text::bundledFont()`) so the default face needs
    no runtime path. The canonical **GUST Font License** text (absent from the NCM
    package, which ships only the GPL3 text for its GPL subset) was fetched from the
    GUST e-foundry and bundled. The GPL3+FE+DE subset is excluded.
- **Rationale:** Pinning a `main` commit is the only way to get the experimental GPU
  API while staying reproducible (D-0033/D-0034). Embedding the face matches the
  project's no-runtime-asset convention (shaders, colormap LUT).
- **Contract impact:** Implements ADR-0022 (no new public contract beyond it).
- **Provenance recorded in:** `third_party/harfbuzz/VENDORING.md`,
  `third_party/fonts/README.md`.

### D-0034 — M6 GPU glyph rendering: libharfbuzz-gpu (Slug)
- **Date / milestone:** 2026-06-19 / M6 (CONTRACT) — maintainer decision
- **Choice:** How shaped glyphs are rendered crisply on the GPU.
- **Decision:** Use HarfBuzz's experimental **`libharfbuzz-gpu`** — it encodes glyph
  outlines for GPU rasterization via the **Slug algorithm** (patent dedicated to the
  public domain) and ships **GLSL** shaders, compiled through our `glslc`→SPIR-V→embed
  toolchain (ADR-0011/D-0022). Glyph encodings cached per `(font, glyphId)`; drawn as
  quads in the overlay pass (ADR-0021), alpha-blended; resolution-independent.
  Experimental risk mitigated by the pinned vendored commit (D-0033); **MSDF
  (msdfgen-style) is the documented fallback**.
- **Rationale:** True outline coverage → publication-quality at any zoom; reuses the
  font project's own GPU path, matched to our shaper. Corrects the author's earlier
  framing — Slug is not "part of HarfBuzz" historically, but `libharfbuzz-gpu` now
  provides exactly this (confirmed from the HarfBuzz README, 2026-06-19).
- **Contract impact:** ADR-0023 (Proposed).
- **Deferred alternatives:** MSDF atlas (fallback); Glyphy (deprecated upstream);
  CPU rasterization (not resolution-independent).

### D-0033 — M6 windowing of text: vendor HarfBuzz (pinned) for Unicode shaping
- **Date / milestone:** 2026-06-19 / M6 (CONTRACT) — maintainer decision
- **Choice:** How to acquire the shaper (new dependency, §1.1) and bound its surface.
- **Decision:** **Vendor HarfBuzz** into `third_party/` at a **pinned commit**
  (deliberately unlike GLFW's system path, D-0026 — pinning freezes the experimental
  `libharfbuzz-gpu` API, D-0034). Build minimal (no ICU/GLib/Cairo/FreeType; `hb-draw`
  supplies outlines), `SYSTEM` includes, **not** under `-Werror`. Wrap behind
  `iv::text::Shaper` (UTF-8 + font + size → positioned glyphs); no HarfBuzz type in
  `iv` public headers (ADR-0004). Default font: **New Computer Modern** (the CM/TeX
  look + a future math face), bundling **GFL-licensed faces only** (vetted from NCM
  8.1.0; the GPL3+FE+DE subset — `NewCM10-Regular`, `NewCMUncial*`, `*Devanagari` —
  is excluded). **LaTeX deferred**; M6 labels are text/Unicode.
- **Rationale:** Industry-standard shaping + the outline source for D-0034; vendoring
  pins the experimental GPU API and keeps builds reproducible.
- **Contract impact:** ADR-0022 (Proposed). License (HarfBuzz "Old MIT" + font)
  recorded at acceptance per §1.1.
- **Deferred alternatives:** system `find_package`; FreeType; stb_truetype/bitmap
  fonts (no real shaping); **LaTeX math rendering (deferred to a later milestone)**.

### D-0032 — M6 annotation/overlay substrate: a graphics pass over the compute output
- **Date / milestone:** 2026-06-19 / M6 (CONTRACT)
- **Choice:** How 2D/3D annotations (lines, glyph quads, legend) reach the image.
- **Decision:** Add the project's **first graphics pipeline**: the volume render
  target also gets `eColorAttachment`; after the compute volume pass, a vertex+fragment
  pipeline draws the overlay **into the same image** in a **classic `VkRenderPass`**
  (`loadOp = eLoad`, standard alpha blend). Primitives: lines + (textured/encoded)
  quads; 3D points projected with the ADR-0012 camera, 2D elements in screen space.
  Identical headless (`render()` → readback) and windowed (`recordFrame` → blit);
  classic 1.0 barriers (D-0016). M6 proves it with a test line + glyph quad; box/axes/
  legend are M7.
- **Rationale:** Lines + resolution-independent glyph coverage want rasterization/blend
  hardware; drawing into the volume image keeps compositing and downstream readback/
  blit trivial; classic render pass adds no device-feature toggle.
- **Contract impact:** ADR-0021 (Proposed).
- **Deferred alternatives:** compositing in compute; a separate overlay attachment;
  `VK_KHR_dynamic_rendering` (to cut boilerplate later).

### D-0031 — M6 opacity correction for ray step spacing (B-0008)
- **Date / milestone:** 2026-06-19 / M6 (CONTRACT)
- **Choice:** Make displayed density independent of the sampling rate.
- **Decision:** Correct per-sample opacity to `α = 1 − (1 − a)^(dt / dt_ref)` (`a` per
  ADR-0013), with `dt_ref = 1 / kReferenceSteps`, `kReferenceSteps = 256` documented.
  Composite + early termination unchanged; resolves B-0008 / the D-0030 finding.
- **Rationale:** Density must be a property of the field + transfer function, not of an
  arbitrary `stepCount`; also makes accumulation path-length-aware (physically
  correct) and is a prerequisite for the M7 colorbar to be meaningful.
- **Contract impact:** ADR-0020 (Proposed; extends ADR-0013).
- **Deferred alternatives:** pre-integrated transfer functions; exposing `dt_ref` as a
  public knob.

### D-0030 — M5 benchmark result + teeth: early-ray termination caps cost (stepCount-insensitive)
- **Date / milestone:** 2026-06-19 / M5 (IMPLEMENT)
- **Result:** The ADR-0019 contract is **met**: median **15.3 ms (~65 FPS)** for one
  512³ → 1280×720 `render()` on the RTX 4070 (Release, validation off); min 12.8,
  max 21.4 ms over N=30.
- **Finding:** ADR-0019's literal teeth — "8× the default `stepCount` → median >
  33.3 ms" — does **not** bite: 8× left the median ≈ 13 ms unchanged. Cause: the
  per-sample opacity (ADR-0013) is **independent of the step spacing `dt`**, and
  early-ray termination breaks at accumulated α ≥ `alphaTermination`. So any ray
  with nonzero opacity saturates after a roughly fixed *number of steps* regardless
  of N — raising `stepCount` only refines `dt`, adding no iterations before
  termination. Only (measure-zero) exactly-zero-opacity rays run all N. Hence render
  cost is insensitive to `stepCount` alone.
- **Decision:** Demonstrate the benchmark's teeth by **disabling early-ray
  termination** (`iv_bench --no-early-term`, `alphaTermination` set unreachable) so
  every ray marches all `stepCount` samples; then `--no-early-term --step-mult 8`
  takes the median **11.7 ms → 34.3 ms (29.2 FPS) → assertion FAIL (red)**. This
  faithfully shows the benchmark constrains the per-ray march budget; the default
  contract (early-term on) passes. (The `--frames`/`--step-mult`/`--no-early-term`/
  `--advisory` knobs live in `tools/bench.cpp`.)
- **Contract link:** ADR-0019 — the bound itself is unchanged and met; only its
  Verification "8× stepCount" recipe is refined (needs `--no-early-term`). The
  underlying `dt`-independent opacity is logged as **B-0008**.

### D-0029 — M5 performance contract: ≥30 FPS @ 512³, 720p, RTX 4070-class
- **Date / milestone:** 2026-06-19 / M5 (CONTRACT) — maintainer decision
- **Choice:** The operating point that defines "interactive framerates for several
  hundred voxels per side", and how to enforce it.
- **Decision:** Contract = median ≤ 33.3 ms (≥30 FPS) for one 512³ → 1280×720
  `Renderer::render()` on an RTX 4070-class GPU. Enforced by a headless, **opt-in**
  benchmark (`iv_bench` / `[!benchmark]`): warm-up + N≥30 timed renders, assert the
  median bound. Excluded from the default `ctest` (hardware-dependent).
- **Rationale:** Turns the founding goal into a checkable number + regression
  guard; headless render-time is a clean, conservative proxy for the vsync-capped
  windowed present.
- **Contract impact:** ADR-0019 (Proposed).
- **Deferred alternatives:** GPU-timestamp timing; a default perf gate (flaky
  across hardware).

### D-0028 — M5 interaction: OrbitCamera (host) + Viewer (orbit/zoom/keys)
- **Date / milestone:** 2026-06-19 / M5 (CONTRACT)
- **Choice:** The camera-control model and public viewer API.
- **Decision:** `iv::OrbitCamera` (pure host: target/distance/yaw/pitch, clamped
  pitch + distance, `eye()` per ADR-0012) drives `RenderParams`; `iv::vk::Viewer`
  maps left-drag→orbit, scroll→zoom, keys (Esc/L/C/R), with `run()` /
  `runFrames(n)`. Pan/roll/trackball deferred.
- **Rationale:** Orbit+zoom suffices to inspect a volume; the camera math is
  unit-testable without a window; toggles exercise M4 live.
- **Contract impact:** ADR-0018 (Proposed). Realizes D-0001 (thin viewer).
- **Deferred alternatives:** trackball/arcball; configurable bindings; pan.

### D-0027 — M5 swapchain & present: UNORM/FIFO, blit compute output, 1 frame in flight
- **Date / milestone:** 2026-06-19 / M5 (CONTRACT)
- **Choice:** How M4's compute-rendered image reaches the screen, and the present
  loop's format/sync/resize policy.
- **Decision:** UNORM surface format (prefer `B8G8R8A8_UNORM`) + `FIFO`; swapchain
  images `eTransferDst`; per frame acquire → `Renderer::recordFrame` (dispatch to an
  internal storage image) → **blit** into the swapchain image → present, with
  `imageAvailable`/`renderFinished` semaphores + an in-flight fence (**one** frame
  in flight); recreate on out-of-date/suboptimal/resize (device-idle first). The
  offscreen `render()`+readback path is unchanged. Classic 1.0 barriers (D-0016).
- **Rationale:** A compute pipeline can't draw to the swapchain (D-0021), so blit;
  FIFO is universal; single-frame-in-flight is simple/correct and ample for ≥30 FPS.
- **Contract impact:** ADR-0017 (Proposed).
- **Deferred alternatives:** MAILBOX; frames-in-flight pipelining; dynamic
  render-resolution (blit already enables it).

### D-0026 — M5 windowing: GLFW via system find_package; presentation-capable Context
- **Date / milestone:** 2026-06-19 / M5 (CONTRACT) — maintainer decision
- **Choice:** How to acquire GLFW (the new windowing dependency, D-0002), and how
  presentation support reaches our Vulkan setup.
- **Decision:** GLFW via **system `find_package(glfw3)`** (maintainer installed
  `libglfw3-dev`), used **only** by a separate `iv_viewer` target gated by
  `IV_BUILD_VIEWER`; the core `iv` stays GLFW-free (D-0001). `iv::vk::Context` gains
  an opt-in **presentation mode** (extends ADR-0005): enable GLFW-required instance
  extensions + `VK_KHR_swapchain`; require the graphics queue family to also
  present (verified vs the surface). Separate present queue deferred.
- **Rationale:** Reuses Context (no duplication); keeps the headless core/tests
  GLFW-free; system find_package matches ADR-0001 and is simplest.
- **Contract impact:** ADR-0016 (Proposed); new dependency per ADR-0001 §1.1;
  extends ADR-0005.
- **Deferred alternatives:** vendoring GLFW (submodule); SDL3 / direct xcb-Wayland
  (B-0001/B-0002); separate present queue.

### D-0025 — Volume stores (re, im); magnitude/phase derived in-shader (supersedes ADR-0009)
- **Date / milestone:** 2026-06-19 / post-M4 (defect fix, CONTRACT)
- **Choice / finding:** M4 rendering exposed a phase-seam artifact — the GPU
  linearly interpolates the stored phase *angle*, and interpolating across the ±π
  branch cut averages +π/−π to ≈0, painting a thin wrong-color seam on the
  negative-real axis (cyan in HSV, dark in twilight). Confirmed by a face-on
  phase-wheel **linear-vs-nearest A/B** (linear → seam; nearest → clean).
- **Decision:** Store the **raw complex value** (R=Re, G=Im) instead of
  `(magnitude, phase)`, and derive magnitude/phase **per-sample in the shader**.
  Interpolating the continuous complex value is correct across the cut. Supersedes
  **ADR-0009**; **revises D-0004** (store derived) and **D-0005** (derive in input
  precision).
- **Conversion point (double input):** each voxel's `re`/`im` is narrowed
  `double → float` **on the host in `deriveField`** when written to the fp32
  staging buffer; magnitude/phase are derived **in-shader in fp32**; the magnitude
  range (ADR-0010) is still computed **on the host in input precision** then
  narrowed.
- **Rationale:** correctness (eliminates the seam); also simplifies the M3 upload
  (store the value directly, no host abs/arg for storage); physically-correct
  reconstruction of a sampled complex field.
- **Contract impact:** ADR-0015 (Proposed → supersedes ADR-0009).
  `VolumeReadback::Texel` changes `{magnitude, phase}` → `{re, im}`. ADR-0013/0014
  contracts unchanged (they consume shader-derived magnitude/phase).
- **Deferred alternatives:** nearest sampling; manual cos/sin trilinear; a
  `(magnitude, cosθ, sinθ)` RGB32F layout — all rejected in ADR-0015.

### D-0024 — PNG demo export via an owned minimal encoder (no new dependency)
- **Date / milestone:** 2026-06-19 / post-M4 (tooling)
- **Choice:** How to save rendered frames as PNG for demos — add a PNG library
  (libpng / lodepng / stb_image_write) vs. write the encoder ourselves.
- **Decision:** Hand-roll a minimal RGBA8 PNG writer (uncompressed DEFLATE
  "stored" blocks; CRC-32 + Adler-32) in `examples/png.hpp`, used by
  `examples/render_demo`. No new dependency; files are larger than
  zlib-compressed PNGs but valid and viewable anywhere. Demo images go to a
  gitignored `gallery/` (regenerable, not committed).
- **Rationale:** Matches the minimal-deps / own-the-boilerplate stance; avoids a
  dependency ADR (§1.1) for example-only tooling; the encoder is ~150 lines and
  self-contained.
- **Contract impact:** none — examples are not part of `libiv` and not on the test
  gate; no public-contract or dependency change (hence no ADR).
- **Deferred alternatives:** a real (zlib-compressed) PNG or stb_image_write if
  demo image size ever matters — would then warrant a dependency decision.

### D-0023 — M4 coordinate frame & DVR convention (RH, +Y up, unit-cube = texcoord)
- **Date / milestone:** 2026-06-19 / M4 (CONTRACT)
- **Choice:** The §5 conventions for the renderer: handedness/up-axis, where the
  volume lives, how rays are generated, and the compositing/march model.
- **Decision:** Right-handed world, **+Y up**; the volume is the unit cube
  `[0,1]³` and a world position **is** the normalized 3D texture coordinate;
  rendered image origin **top-left** (matches ADR-0006). Fixed pinhole camera
  (eye/target/up/vfov/aspect) passed as `eye + topLeftDir + horizontal + vertical`
  spanning vectors; **front-to-back `over`** compositing with a **fixed
  `stepCount`** (no opacity correction in M4) and early-ray termination.
- **Rationale:** World = texcoord makes sampling trivial and ties rendering to the
  M3 x-fastest layout (a wrong axis is visible); fixed steps are deterministic and
  exactly testable; front-to-back enables early-out (a perf lever for M5).
- **Contract impact:** ADR-0012 (Proposed). Realizes D-0008.
- **Deferred alternatives:** model matrix / non-unit volume, opacity (step-size)
  correction, adaptive/jittered sampling — all deferred (would refine ADR-0012).

### D-0022 — Shader toolchain: build-time glslc → embedded SPIR-V (extends ADR-0001)
- **Date / milestone:** 2026-06-19 / M4 (CONTRACT) — maintainer decision
- **Choice:** How GLSL becomes the SPIR-V the library uses: build-time `glslc`
  (embedded) vs vendored precompiled `.spv` vs runtime `libshaderc`.
- **Decision:** Compile `shaders/*.{comp,vert,frag}` with `glslc` (found via CMake
  `find_program`; **fatal at configure if missing**, like the GCC≥13 check) and
  **embed** the SPIR-V into `libiv` as a generated array. This **extends
  ADR-0001's dependency policy**: `glslc` (Vulkan SDK) is a required **build
  tool** — not a linked/runtime dependency; no build-time network. ADR-0001 is
  **not** superseded.
- **Rationale:** Shaders always match source; the library stays self-contained and
  tests are path-independent; the SDK is installed. Reproducible/offline.
- **Contract impact:** ADR-0011 (Proposed); amends ADR-0001 §dependency policy
  (journaled here, not a supersession).
- **Deferred alternatives:** vendored precompiled SPIR-V (the fallback if requiring
  `glslc` at build becomes painful) — would be a superseding build ADR.

### D-0021 — M4 rendering substrate: compute shader → R8G8B8A8 storage image
- **Date / milestone:** 2026-06-19 / M4 (CONTRACT) — maintainer decision
- **Choice:** Compute pipeline (storage image) vs graphics pipeline (fragment
  shader into the M2 color attachment) for the ray-marcher.
- **Decision:** A **compute** pipeline casts one ray per pixel and `imageStore`s to
  a dedicated 2D **`R8G8B8A8_UNORM` storage image** (a storage-mandatory format),
  read back via the ADR-0006 staging path. No render pass / framebuffer / vertex
  stage; no new device feature (D-0016 retained).
- **Rationale:** Compute is the natural fit for per-pixel ray casting, avoids
  render-pass boilerplate and the dynamic-rendering choice, and blits cleanly to
  M5's swapchain; a dedicated storage image leaves M2's target untouched.
- **Contract impact:** ADR-0011 (Proposed). Realizes D-0008.
- **Deferred alternatives:** graphics-pipeline DVR (rejected; more boilerplate).

### D-0020 — Precision paths are not bit-identical; each is bit-exact to its own precision
- **Date / milestone:** 2026-06-19 / M3 (IMPLEMENT/VERIFY)
- **Choice / finding:** ADR-0009's *Verification* narrative claimed the `float`
  and `double` input paths produce identical fp32 texels. Implementation testing
  disproved it: for some voxels `arg` (atan2) computed in `double` then narrowed
  differs from the `float` computation by 1 ULP — e.g. `z = (12, -5)`:
  `-0.394791126f` (double) vs `-0.394791096f` (float).
- **Decision:** This divergence is correct and intended (D-0005): derive in input
  precision, then narrow; the double path is the more accurate. The **binding
  Contract Specification** of ADR-0009 (bit-exact round-trip vs a *same*-precision-
  then-narrow expectation) is unchanged and holds. Correct only ADR-0009's
  *Verification* narrative (drop the "identical fp32 texels" claim); each test now
  asserts a path against its own-precision expectation, and the float/double
  divergence is itself evidence of input-precision derivation.
- **Contract impact:** none to ADR-0009's Contract Specification; refines its
  Verification narrative (journaled here, per the D-0016 precedent of refining an
  ADR's narrative without touching its binding contract).
- **Deferred alternatives:** none.

### D-0019 — Magnitude-range metadata: exclude zeros from min, allow caller override
- **Date / milestone:** 2026-06-19 / M3 (CONTRACT)
- **Choice:** What magnitude statistics to expose for M4's opacity normalization,
  and whether the caller can pin them.
- **Decision:** During the ingestion host pass, compute `max` (greatest `abs`)
  and `minPositive` (least *strictly-positive* `abs`; `0` if the field is all-
  zero), in input precision then narrowed to fp32. Expose `magnitudeRange()`
  (override-if-given else auto) and `autoMagnitudeRange()` (always auto). A
  caller may override via `VolumeOptions::magnitudeRange` (validated
  `minPositive>=0 && max>=minPositive`). The normalization *formula* (linear/log,
  degenerate handling) stays in M4.
- **Rationale:** The host already visits every sample (D-0004/0009), so the range
  is free; log opacity needs a positive floor, so zeros are excluded from
  `minPositive`; an override pins normalization across animation frames/series.
- **Contract impact:** ADR-0010 (Proposed). Range is metadata, not stored in the
  texture (keeps raw magnitude per D-0004).
- **Deferred alternatives:** GPU-side reduction; mean/percentile stats — not
  needed for the M4 contract.

### D-0018 — M3 ingestion API & GPU-upload contract (realizes D-0004/0005/0006)
- **Date / milestone:** 2026-06-19 / M3 (CONTRACT)
- **Choice:** Concrete shape of the public ingestion API and the GPU upload that
  realizes the founding field decisions (D-0004 derived storage, D-0005
  precision, D-0006 x-fastest layout).
- **Decision:** Input is `std::span<const std::complex<float|double>>` +
  `iv::GridDims{nx,ny,nz}` (x-fastest 0-based `index`/`count`, 64-bit arithmetic);
  entry is `iv::vk::Volume::create(Context&, span, dims, VolumeOptions)`. The span
  is borrowed for the call only. Upload derives `(abs, arg)` in input precision →
  narrowed fp32, fills a tightly-packed staging buffer in x-fastest order, and
  `copyBufferToImage` into a device-local 3D **RG32F** image (R=magnitude raw,
  G=phase rad), resting in `eShaderReadOnlyOptimal` with a sampling view (sampler
  → M4). A move-only `Volume` owns image/memory/view and borrows the Context
  device; round-trip readback is **bit-exact** (TRANSFER copy, no filter/convert).
  Classic 1.0 barriers (D-0016); raw allocation via a generalized `findMemoryType`
  helper (D-0017).
- **Rationale:** Mirrors M2's `create`/RAII/staging-fence patterns; bit-exact
  RG32F readback gives strong teeth (transposed fill, R/G swap, `norm`-vs-`abs`,
  `atan2`-arg-swap all go red); raw magnitude keeps M4's linear/log toggle free.
- **Contract impact:** ADR-0008 (ingestion API + layout, Proposed), ADR-0009
  (texture/precision/upload, Proposed). Closes the "ADR pending in M3" notes on
  D-0004/0005/0006.
- **Deferred alternatives:** caller-specified strides; half/8-bit storage; VMA
  (B-0006) — all rejected for M3 in the ADRs.

### D-0017 — M3 keeps raw allocation; VMA deferred
- **Date / milestone:** 2026-06-19 / M3 (CONTRACT)
- **Choice:** Adopt the Vulkan Memory Allocator (VMA) now, or keep hand-rolled
  `vkAllocateMemory` for M3?
- **Decision:** Keep raw allocation for M3; generalize M2's
  `findMemoryType`/allocate helper into a small shared utility. Defer VMA.
- **Rationale:** M3's needs are modest (one 3D image + a staging buffer); matches
  the project's minimal-deps / own-the-boilerplate stance; no new dependency now.
- **Contract impact:** none (no new dependency). Adopting VMA later requires a
  dependency ADR (§1.1).
- **Deferred alternatives:** VMA stays open at Backlog B-0006 — revisit at M4/M5
  when images, uniforms, and buffers proliferate.

### D-0016 — M2 uses core 1.0 pipeline barriers (no synchronization2 feature)
- **Date / milestone:** 2026-06-19 / M2 (IMPLEMENT)
- **Choice:** ADR-0006's narrative mentioned synchronization2 barriers, but
  `vkCmdPipelineBarrier2` requires enabling the `synchronization2` device feature,
  which conflicts with ADR-0005's "no special features." Use sync2 (enable the
  feature) or classic core-1.0 `vkCmdPipelineBarrier`?
- **Decision:** Use classic core-1.0 `vkCmdPipelineBarrier` for M2's layout
  transitions. No device feature/extension is enabled.
- **Rationale:** Neither ADR-0005 nor ADR-0006's *binding* Contract Specification
  mandates sync2 (0006's binding contract is the readback layout/usage/color;
  "sync2" was narrative). Classic barriers are correct and keep ADR-0005's "no
  features" intent. Avoids a feature-chain in device creation for no M2 benefit.
- **Contract impact:** none to either ADR's binding contract; refines ADR-0006's
  narrative. Journaled here. sync2 can be adopted later under an ADR if a
  milestone needs it.
- **Deferred alternatives:** synchronization2 (and dynamic rendering) when M4's
  pipeline work makes them worthwhile.

### D-0015 — LSan scoping for third-party Vulkan loader/driver leaks
- **Date / milestone:** 2026-06-19 / M2 (IMPLEMENT)
- **Choice:** How to keep §7's "sanitizers clean" gate meaningful when the Vulkan
  loader + validation layer make ~240 process-exit allocations that never free
  and do not symbolize (identical on NVIDIA and llvmpipe; routed through our
  `Context::create()` only because it first initializes the loader). Options:
  blanket-disable LSan; suppression file; or scope LSan off for Vulkan tests only.
- **Decision:** The full ASan+UBSan run sets `ASAN_OPTIONS=detect_leaks=0` (UAF,
  overflow, and UBSan stay active); a second CTest entry leak-checks the
  non-Vulkan suite (`~[vk]`) with `detect_leaks=1`; and the **validation layer is
  the authoritative Vulkan-object-leak gate** — strengthened with a pNext
  create/destroy messenger so undestroyed objects are caught at vkDestroyInstance.
- **Rationale:** A suppression file can't match the unsymbolized driver frames;
  blanket-disabling loses coverage on our code. This keeps real leak coverage on
  our code and a Vulkan-native gate on Vulkan objects, without rationalizing a
  benign leak in code we own (§8.8).
- **Contract impact:** refines how ADR-0001's sanitizer gate (§7) is run; no
  consumer-facing contract change. Journaled here; supersede ADR-0001 only if we
  decide to make this a formal gate-policy contract.
- **Deferred alternatives:** revisit a suppression file if a future driver
  symbolizes its leaks.

### D-0014 — Concurrency baseline: single-threaded, not thread-safe
- **Date / milestone:** 2026-06-18 / M2 (CONTRACT)
- **Choice:** Thread-safe public API now vs. a single-threaded baseline vs.
  leaving concurrency unstated.
- **Decision:** Public API is single-threaded and not thread-safe; no internal
  threads in M2; Debug thread-affinity asserts; host reads gated on fences; the
  atomic assert handler is the sole cross-thread exception. Output is bitwise
  deterministic per device.
- **Rationale:** §6 requires an explicit declaration; races are made
  unrepresentable by sharing nothing; locking would be premature cost.
- **Contract impact:** ADR-0007 (Proposed).
- **Deferred alternatives:** future async/multi-thread work gets its own ADR.

### D-0013 — Offscreen target format & host-readback convention
- **Date / milestone:** 2026-06-18 / M2 (CONTRACT)
- **Choice:** Readback target format and pixel layout (UNORM vs sRGB vs float;
  origin/packing).
- **Decision:** `R8G8B8A8_UNORM`, tightly packed, top-left origin, row-major,
  pixel `(x,y)` at byte `(y*w+x)*4`, channels R,G,B,A; image
  `eColorAttachment|eTransferSrc|eTransferDst`; staging buffer for readback.
- **Rationale:** UNORM-linear gives bit-exact, implementation-independent pixel
  verification (real teeth); top-left is Vulkan-native.
- **Contract impact:** ADR-0006 (Proposed).
- **Deferred alternatives:** float/HDR readback if M4 needs it; → Backlog B-0006
  (VMA allocator).

### D-0012 — Instance, physical-device & queue selection
- **Date / milestone:** 2026-06-18 / M2 (CONTRACT)
- **Choice:** Device requirements and selection strategy; validation policy;
  API baseline.
- **Decision:** Vulkan 1.3 baseline; rank discrete>integrated>virtual>cpu (accept
  software — only llvmpipe exists here), require a graphics queue family,
  `IV_VULKAN_DEVICE_INDEX` override; validation layer + debug messenger in Debug
  (best-effort), captured so tests can assert cleanliness; one graphics queue.
- **Rationale:** Must run on the software-only dev host yet prefer real GPUs;
  capturing validation messages makes "validation-clean" testable.
- **Contract impact:** ADR-0005 (Proposed).
- **Deferred alternatives:** dedicated transfer/compute queue later if needed.

### D-0011 — Vulkan binding & object-ownership model
- **Date / milestone:** 2026-06-18 / M2 (CONTRACT)
- **Choice:** C API + own RAII vs. Vulkan-Hpp (no-exceptions) + own RAII vs.
  vk::raii. (Maintainer decision.)
- **Decision:** Vulkan-Hpp (`vulkan.hpp`) with `VULKAN_HPP_NO_EXCEPTIONS`,
  result-returning, wrapped in our own move-only single-owner RAII types; default
  dispatch; a boundary helper maps `vk::Result`→`iv::Errc`.
- **Rationale:** Type-safe and exception-free (fits ADR-0003); explicit, auditable
  ownership; vk::raii's exception-coupled ctors conflict with ADR-0003.
- **Contract impact:** ADR-0004 (Proposed).
- **Deferred alternatives:** dynamic dispatcher if a future extension needs it.

### D-0010 — Build/toolchain/dependency policy
- **Date / milestone:** 2026-06-18 / M1 (CONTRACT)
- **Choice:** Warning strictness, build configs, sanitizer wiring, and how third-
  party deps are acquired (C++23/system-GCC were maintainer-fixed givens).
- **Decision:** CMake (≥3.25), ISO C++23 (extensions off), GCC≥13; a strict
  mandatory warning set with `-Werror`; Debug/Release with `IV_ASSERT` always on;
  ASan+UBSan gate; deps via `find_package` (system) or vendored+pinned (small
  libs), no build-time network; new dep ⇒ ADR.
- **Rationale:** Satisfies §7 gates; vendoring keeps builds offline/reproducible;
  strict warnings surface conversions at the Vulkan boundary; CMake is the
  least-friction path to `find_package(Vulkan)`/GLFW.
- **Contract impact:** ADR-0001 (Proposed).
- **Deferred alternatives:** Clang/CI-matrix and FetchContent-for-all — not
  backlogged (supersedable later if multi-compiler CI is wanted).

### D-0009 — Error & failure model: std::expected + always-on IV_ASSERT, no exceptions
- **Date / milestone:** 2026-06-18 / M1 (CONTRACT)
- **Choice:** Exceptions vs. `std::expected` for recoverable failures; how to keep
  boundary contracts alive in Release; the assertion mechanism.
- **Decision:** Recoverable failures return `std::expected<T, iv::Error>`; no
  exceptions as a control channel (only `std::bad_alloc` may propagate, fatal);
  contract violations abort via an always-on, overridable `IV_ASSERT` (so
  precondition misuse is defined-abort, not UB, and is unit-testable).
- **Rationale:** Exception-free propagation suits the GPU/perf hot paths and is
  C++23-native; `assert()` would vanish under `-DNDEBUG`, so a custom always-on
  macro is required; an overridable handler makes abort paths testable without
  death tests.
- **Contract impact:** ADR-0003 (Proposed).
- **Deferred alternatives:** none material.

### D-0001 — Viewer model: offscreen core + optional interactive viewer
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M2 & M5
- **Choice:** Offscreen-only, library-owns-window, or both.
- **Decision:** Both — the offscreen renderer is the core; a thin GLFW-based
  interactive viewer layers on top.
- **Rationale:** Offscreen rendering is deterministic and embeddable and gives
  contract tests real teeth via pixel readback (§2.4); a separate viewer keeps
  the windowing dependency out of the core and off the test path.
- **Contract impact:** ADR pending — public API surface partitioned across
  M2 (offscreen core) and M5 (viewer).
- **Deferred alternatives:** none (the alternatives are subsumed by "both").

### D-0002 — Windowing/surface backend: GLFW
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M5
- **Choice:** GLFW, SDL3, or direct xcb/Wayland for window + Vulkan surface + input.
- **Decision:** GLFW.
- **Rationale:** Tiny, battle-tested, cross-platform Vulkan-surface + input
  support; lets us spend our owned-boilerplate budget on Vulkan rather than
  windowing and the X11/Wayland split.
- **Contract impact:** ADR pending in M5 (new third-party dependency, §1.1).
- **Deferred alternatives:** → Backlog B-0001 (direct xcb/Wayland), B-0002 (SDL3).

### D-0003 — Test framework: Catch2 (single-header)
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M1
- **Choice:** Catch2 single-header, a minimal custom harness, or GoogleTest.
- **Decision:** Catch2 (single-header), dev-only dependency.
- **Rationale:** Good ergonomics for the red→green / fault-injection discipline
  with a minimal footprint; consistent with the minimal-deps ethos while not
  reinventing assertion/reporting machinery.
- **Contract impact:** ADR-0002 (Proposed) — dev dependency + teeth-evidence convention.
- **Deferred alternatives:** → Backlog B-0003 (minimal custom harness).

### D-0004 — Texture stores derived (magnitude, phase); transfer function in-shader
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M3 & M4
- **Choice:** Upload raw complex and derive in-shader, vs. upload precomputed
  `(magnitude, phase)` and apply opacity scaling + colormap in-shader.
- **Decision:** Upload precomputed `(magnitude, phase)` as `RG32F`; apply
  scaling and colormap in the fragment/compute path.
- **Rationale:** Single upload; linear/log opacity and colormap selection become
  free runtime toggles (uniforms) with no re-upload; keeps the GPU-resident
  representation independent of the input precision.
- **Contract impact:** ADR pending in M3 (texture format/contents) and M4
  (transfer function).
- **Deferred alternatives:** none material.

### D-0005 — Precision policy: derive in input precision, store fp32, no fp64 GPU path
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M3
- **Choice:** Keep a double-precision path on the GPU vs. downconvert.
- **Decision:** Compute magnitude/phase in the input's precision on the host,
  store `fp32` in the texture; rendering is `fp32`. No fp64 GPU path.
- **Rationale:** Vulkan cannot sample fp64 textures; opacity/color resolution
  does not need fp64. Computing the reduction (abs/arg) in input precision before
  downconversion preserves accuracy where it matters.
- **Contract impact:** ADR pending in M3 (precision policy).
- **Deferred alternatives:** → Backlog B-0004 (fp64 compute path).

### D-0006 — Flat-array memory layout: x-fastest, 0-based
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M3
- **Choice:** Index ordering and base for the flat `(nx, ny, nz)` input array.
- **Decision:** `idx = x + nx*(y + ny*z)`, x-fastest, 0-based.
- **Rationale:** The overwhelmingly common convention for an "nx × ny × nz" flat
  buffer; matches natural 3D-texture row ordering on upload.
- **Contract impact:** ADR pending in M3 (a §5 convention with public surface).
- **Deferred alternatives:** none; caller-overridable strides may be revisited
  if needed (not committed).

### D-0007 — Phase colormap: perceptually-uniform cyclic by default, HSV selectable
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M4
- **Choice:** Classic HSV hue wheel vs. a perceptually-uniform cyclic map.
- **Decision:** Default to a perceptually-uniform cyclic map (twilight/phase
  style); keep the classic HSV hue wheel selectable (traditional domain coloring).
- **Rationale:** Perceptually-uniform cyclic maps avoid the misleading
  brightness banding of HSV while preserving the cyclic phase mapping; HSV
  retained for familiarity/compatibility with domain-coloring conventions.
- **Contract impact:** ADR pending in M4 (exact `arg`→color mapping).
- **Deferred alternatives:** → Backlog B-0005 (additional colormaps).

### D-0008 — Rendering technique: ray-marched DVR, verified headlessly first
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M2–M4
- **Choice:** Direct volume rendering technique and verification strategy.
- **Decision:** Ray-marched DVR with front-to-back alpha compositing; verify the
  renderer offscreen via deterministic pixel readback before adding the window.
- **Rationale:** DVR maps cleanly onto the abs→opacity / arg→color transfer
  function and onto interactive framerates; headless-first keeps contract tests
  deterministic and decoupled from windowing.
- **Contract impact:** ADR pending in M4 (compositing model).
- **Deferred alternatives:** none material at this stage.

---

## Backlog

### B-0001 — Direct xcb/Wayland windowing (zero windowing dependency)
- **Origin:** D-0002 (road not taken).
- **What:** Own the platform windowing/surface code directly instead of GLFW.
- **Why deferred:** Substantial platform-specific boilerplate and the X11/Wayland
  split; not worth it while GLFW satisfies the minimal-deps goal.
- **Revisit when:** A hard zero-third-party-dependency requirement emerges.
- **Contract link:** would supersede the M5 windowing-dependency ADR.

### B-0002 — SDL3 windowing/input backend
- **Origin:** D-0002 (road not taken).
- **What:** Use SDL3 instead of GLFW.
- **Why deferred:** Heavier than GLFW; its extra subsystems (gamepad, audio)
  aren't needed.
- **Revisit when:** Broader input/multimedia needs appear.
- **Contract link:** would supersede the M5 windowing-dependency ADR.

### B-0003 — Minimal custom test harness (zero test dependency)
- **Origin:** D-0003 (road not taken).
- **What:** Replace Catch2 with an owned minimal assertion/reporting harness.
- **Why deferred:** Catch2's footprint is acceptable and its ergonomics aid the
  teeth discipline.
- **Revisit when:** Catch2's build cost or footprint becomes objectionable.
- **Contract link:** would supersede the M1 test-framework ADR.

### B-0004 — fp64 GPU compute path
- **Origin:** D-0005 (road not taken).
- **What:** Carry double precision onto the GPU for derived quantities.
- **Why deferred:** Vulkan can't sample fp64 textures; no demonstrated need.
- **Revisit when:** Rendering reveals precision artifacts traceable to `fp32`
  derived storage.
- **Contract link:** would supersede the M3 precision-policy ADR.

### B-0005 — Additional colormaps beyond default + HSV
- **Origin:** D-0007 (road not taken).
- **What:** Offer further cyclic colormaps (and/or caller-supplied maps).
- **Why deferred:** Two maps cover the immediate need; more is scope creep now.
- **Revisit when:** Users request specific additional maps — **now requested**: on the
  graphics-polish agenda (2026-06-21); still open.
- **Contract link:** would extend the M4 colormap ADR (ADR-0014).

### B-0006 — Vulkan Memory Allocator (VMA) for device memory
- **Origin:** D-0013 / ADR-0006 (road not taken).
- **What:** Adopt AMD's VMA (single-header) instead of raw `vkAllocateMemory`.
- **Why deferred:** M2 needs only one image + one staging buffer; raw allocation
  is adequate and avoids a new dependency before it pays off. Still deferred through M8 —
  raw allocation via the shared `findMemoryType`/allocate helper has remained adequate
  (D-0017); the viewer adds only swapchain images (driver-managed) + a few small per-frame
  buffers.
- **Revisit when:** device-memory management outgrows the current handful of images/buffers
  (e.g. many volumes resident, streaming, or defragmentation needs).
- **Contract link:** a new dependency ADR (§1.1) + would amend ADR-0006's memory
  section.

### B-0010 — Present-path (viewer) Slug glyph rendering
- **Origin:** M6 ADR-0023 implementation (D-0036), 2026-06-19.
- **What:** Draw Slug glyph quads on the **present path** (`recordFrame`), not just
  headless `render()`, so the interactive viewer shows text. Needs the glyph vertex
  buffer + atlas texel buffer/view + descriptor set managed across the in-flight
  frame (like `frameOverlayBuf_`), rather than the transient per-render
  `buildGlyphResources`. Also: cache the atlas/encoding instead of rebuilding it
  per frame.
- **Why deferred:** ADR-0023's verification is headless coverage readback; the
  viewer's *live* labels are an M7 concern (the annotation layer decides what text to
  draw, where, and when it changes). Shipping headless-only kept the M6 renderer
  changes bounded.
- **Revisit when:** M7 — bounding box / ticked axes / legend need on-screen labels in
  the viewer.
- **Contract link:** extends ADR-0023 (rendering path) under ADR-0021's overlay; no
  new public contract.
- **Resolved:** M7 / ADR-0025 (8f19540) — `recordFrame` draws `Overlay::glyphs` via
  persistent, grown-on-demand resources; the viewer shows text.

### B-0009 — Declare a top-level project LICENSE
- **Origin:** M6 font vetting (ADR-0022), 2026-06-19 — the repo has no
  `LICENSE`/`COPYING` of its own.
- **What:** Choose and add the project's own license. A usable library should declare
  one; it also governs whether GPL-with-Distribution-Exception assets (e.g. NCM's
  `NewCM10-Regular`) could ever be bundled (they require a GPL-compatible program
  license — we currently sidestep this by bundling **GFL** faces only).
- **Why deferred / open:** the license choice is the maintainer's; not blocking M6
  (the bundled NCM faces are GFL, which does not constrain our code license).
- **Revisit when:** before a public release, or if bundling any GPL+DE asset.
- **Contract link:** none yet (project-governance, would touch ADR-0001).

### B-0008 — Opacity correction for sample spacing (dt-independent α)
- **Origin:** M5 benchmark teeth investigation (D-0030), 2026-06-19.
- **What:** The per-sample opacity (ADR-0013) does not account for the ray step
  spacing `dt`, so the accumulated opacity — and thus the displayed density and the
  early-termination saturation point — depends on `stepCount`: more steps render a
  *denser* image. For a quantitative tool the rendered density should be invariant
  to the sampling rate.
- **Proposed approach:** Standard DVR opacity correction
  `α_dt = 1 − (1 − a)^(dt / refStep)`, so `stepCount` controls only sampling
  fidelity, not displayed density (and makes the ADR-0019 "8× stepCount" teeth bite
  directly, without `--no-early-term`).
- **Why deferred:** M5 is the viewer + performance contract; this is a
  rendering-model refinement, not a viewer concern.
- **Revisit when:** the "usable scientific data plotting library" milestone
  (quantitative correctness of opacity / transfer function).
- **Contract link:** would extend ADR-0013 (opacity transfer function).
- **Resolved:** M6 / ADR-0020 (Accepted, 53d7c84) / D-0031 — `ray_march.comp` applies the
  dt-correction `α = 1 − (1 − a)^(dt · kReferenceSteps)` (`kReferenceSteps = 256`), so the
  displayed density is invariant to `stepCount`; the ADR-0019 perf teeth then bite via
  `--no-early-term` (D-0030). It is also the basis for the M8 legend thickness model
  (ADR-0030 `iv::accumulatedOpacity`).

### B-0007 — Volume bounding box with axis ticks/labels
- **Origin:** ADR-0012 review (maintainer, 2026-06-19).
- **What:** Optionally draw the volume's `[0,1]³` bounding box with axis
  ticks/labels for spatial reference in the rendered image.
- **Why deferred:** M4 renders the field itself; annotations/overlays are a
  separable presentation feature.
- **Revisit when:** A milestone adds scene annotation / overlays (likely with or
  after M5's viewer).
- **Contract link:** would extend the M4 rendering ADRs (camera/compositing,
  ADR-0012) and/or a future overlay ADR.
- **Resolved:** M7 / ADR-0026 (d305381) — `iv::text::buildAnnotations` draws the box +
  ticked, labeled axes (+ through-volume axes) over the volume, both paths.

### B-0011 — Log-spaced (decade) magnitude ticks on the legend in log mode
- **Origin:** ADR-0028 (M8 legend), 2026-06-20.
- **What:** In log opacity mode the legend's right-edge magnitude ticks use the linear
  nice-number generator (`ticksFor`, ADR-0024) placed at their `transferNormalized` heights,
  so they read as round numbers (the maintainer's request, D-0042) but distribute
  non-uniformly along a log bar (clustering near `max`). A log-aware generator (decade ticks
  `…, 0.01, 0.1, 1` and 1·2·5·10ᵏ minors) would space them evenly on the bar.
- **Why deferred:** the round-number values are what the maintainer asked for; even spacing is
  a refinement, not a correctness issue.
- **Revisit when:** the legend visual-polish pass, or if log-mode legends look sparse.
- **Contract link:** would extend ADR-0028 (legend tick generation) / ADR-0024 (`ticksFor`).
- **Resolved:** 2026-06-20 (post-M8) — `buildLegend` now emits **decade ticks** (powers of 10,
  labeled `1e<exp>`) in log mode over the active window `[max·10^-decades, max]`, evenly spaced
  on the log bar and tracking the window (thinned to ≤ ~11 for huge ranges); linear mode keeps
  `ticksFor`. Prompted by the legend looking unresponsive to `--decades` (linear ticks barely
  move when loBound ≈ 0). A presentation refinement of ADR-0028's tick generation (no binding
  contract change), so a recorded resolution — no superseding ADR (§2.8).

### B-0012 — True italic field name (mixed-font / multi-atlas glyph overlay)
- **Origin:** ADR-0031 (D-0047), 2026-06-20.
- **What:** Render the legend's field name (and, generally, math variables) in true CM italic.
  The bundled NCM-Book face has no italic glyphs; the matching GFL `NewCM10-BookItalic.otf` is
  available locally. Needs mixed-font text: a **second glyph atlas channel** in the `Overlay` +
  renderer (the overlay is single-atlas today, ADR-0023/0025), so roman structure (`|`, `arg`,
  `(`, `)`, ticks, numbers) and the italic field name can coexist in one overlay.
- **Why deferred:** an architectural change (second atlas + a bundled font asset) out of scope
  for the field-name feature; the field name renders **upright** meanwhile (D-0047).
- **Revisit when:** the legend visual-polish pass (or when units / LaTeX-ish math labels want
  mixed fonts).
- **Contract link:** would extend ADR-0023/0025 (glyph overlay) + amend D-0035 (bundle
  BookItalic); no `LegendSpec`/`PlotOptions` API change (upright → italic is rendering-only).
- **Resolved:** M9 / **ADR-0032** (Accepted 2026-06-21) — the mixed-font glyph substrate landed:
  `iv::text::FontSet` (roman/italic/math) + `iv::text::MixedGlyphs` merge multiple faces into one
  overlay via a single rebased Slug atlas, and `NewCM10-BookItalic` is now bundled
  (`bundledFontItalic()`). True-italic variables/field name are rendered by the ADR-0033 math path
  (the legend `fieldName` default is now `"$f$"`, D-0048), so the upright→italic switch is realized
  there. The architectural blocker is gone.

### B-0013 — Legend & label visual polish (placement, label placement, sizes)
- **Origin:** maintainer review, 2026-06-20 (graphics-polish pass).
- **What:** Tune the color legend's **placement** (the default `LegendSpec::rectNdc` panel
  position/size), the **placement of its labels** (the `|·|` / `arg(·)` captions, the −π/0/π
  phase labels, the magnitude tick values, the `L` label), and **various label sizes** across the
  plot (title, axis labels, tick labels, legend). Pure presentation.
- **Why deferred:** appearance refinement, batched into the polish pass; no contract impact.
- **Revisit when:** the next session (the visual-polish pass).
- **Contract link:** none (presentation; ADR-0026 annotation constants + ADR-0028 legend
  constants — journaled refinements, not contract changes).

### B-0014 — Display the legend thickness `L` in the data's physical units
- **Origin:** maintainer review, 2026-06-20.
- **What:** The legend's `L = …` label (ADR-0030) shows the reference thickness in unit-cube
  path-length units (`[0,1]³`, where 1 = a full traversal). Show it instead in the **data's
  physical units** (the volume maps `[0,1]³` to the `PlotAxes` ranges). Open question: the
  per-axis data extents differ, so "thickness in data units" needs a convention (a chosen axis /
  the geometric mean of the extents / per-axis).
- **Why deferred:** needs a units convention + the `PlotAxes` extents plumbed to the legend;
  batched into polish.
- **Revisit when:** the visual-polish pass.
- **Contract link:** would refine ADR-0030's thickness label (and likely add a data-extent input
  to `LegendSpec` / the facade) — a contract change ⇒ a short ADR.

### B-0015 — Mathematical typesetting in labels (LaTeX-style math)
- **Origin:** maintainer review, 2026-06-20 (the "big item"); LaTeX math deferred since M6
  (D-0033).
- **What:** Render mathematical notation in labels (title, axis labels, legend captions) —
  superscripts/subscripts, fractions, radicals, symbols, proper math italic — i.e. LaTeX-style
  math typesetting. Builds on mixed-font/multi-atlas glyph support (**B-0012**, a prerequisite)
  and the NewCM math face.
- **Why deferred:** a substantial feature — the founding arc (M1–M8) delivered the usable
  plotting library; math typesetting is the next major capability, likely its own milestone (M9).
- **Revisit when:** after the appearance polish; scope as a milestone with its own ADRs.
- **Contract link:** extends the text layer (ADR-0022/0023) substantially; new math-layout ADRs.
- **Resolved:** M9 (2026-06-21) — **ADR-0032** (mixed-font substrate, resolved the B-0012
  prerequisite) + **ADR-0033** (inline `$…$` math model, LaTeX subset & OpenType-MATH layout), both
  Accepted. Caller labels (title/axis/unit/legend captions) now render publication-quality math
  (scripts, `\frac`, `\sqrt`, accents, stretchy delimiters, bra–ket, Greek/symbol macros) via an
  owned parser + box layout over the bundled NewCMMath face (no TeX engine; D-0048). Follow-on:
  legend scientific-notation tick labels (B-0016).

### B-0016 — Scientific/superscript notation for the legend magnitude axis (`1×10⁻³`)
- **Origin:** maintainer review, 2026-06-21 (M9 CONTRACT, redline on ADR-0033 point 4).
- **What:** Render the legend's **magnitude (vertical) axis** tick labels in
  scientific/superscript form — e.g. `1×10⁻³` instead of `1e-3` / `1.2` — using the M9 math
  layout. These are **auto-generated** numeric labels (`legend_builder` decade/linear ticks),
  not caller strings, so they fall outside ADR-0033's "caller-supplied labels are math-aware"
  scope; promoting them is a generated-label formatting change layered on the math engine.
- **Why deferred:** the maintainer wants it, but it is cleaner to land **after** the M9 math
  engine exists (ADR-0033) — it reuses the math layout to typeset a generated mantissa×10^exp
  rather than parsing a caller string. Keeping it out of M9 keeps the M9 backward-compat
  invariant (a `$`-free label is byte-identical) clean.
- **Revisit when:** once ADR-0033's math layout lands; a small follow-on (M9 tail or a polish
  pass).
- **Contract link:** refines the ADR-0028 legend tick generation (and ADR-0024 `formatTick`
  policy) to emit math; presentation-layer, likely a journaled refinement once the engine exists.
