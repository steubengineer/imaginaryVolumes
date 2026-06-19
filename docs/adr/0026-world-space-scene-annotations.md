# ADR-0026: World-Space Scene Annotations — Bounding Box, Ticked Axes & Labels

- **Status:** Accepted
- **Date:** 2026-06-19
- **Supersedes:** none

## Context
M7's visible deliverable: draw the volume's **bounding box**, **ticked axes** (on the
box and/or as through-volume reference axes), and **labels** (major tick values, axis
labels, plot title) over the render, **aligned to the rendered volume**, in **both**
`Renderer::render()` and the `Viewer`, with every element optional. This ADR realizes
the **declarative model of ADR-0024** as overlay geometry: it owns the **projection**,
the **view-dependent placement** (which box edges show ticks/labels so labels never
overlap the data), and the label layout. It consumes ADR-0024 (ranges, ticks,
`PlotAxes` visibility + `ThroughAxis` + `BoxTickStyle`), draws via ADR-0021 (overlay
lines) + ADR-0023/0025 (glyphs, both paths), and must match the **ADR-0012 ray
camera** (`eye/target/up/vfov/aspect`, +Y up, world `=[0,1]³`, top-left image origin)
so the box bounds the cube exactly. Cites ADR-0018/OrbitCamera (the viewer camera).

## Decision
**A host annotation builder projects `PlotAxes` into an `Overlay`: world-space lines
for the box/ticks/through-axes (via an ADR-0012-consistent view-projection in
`Overlay::transform`), and screen-space glyph labels anchored to the box silhouette.**

- **View-projection (host):** `viewProjection(camera, aspect) → mat4` built from the
  *same* `{eye, target, up, vfov, aspect}` the renderer rays use (ADR-0012), same
  handedness and **top-left origin** (Vulkan clip, `y` down, `z ∈ [0,1]`), so world
  `p ∈ [0,1]³` maps to the pixel its camera ray passes through. It is the single
  source of truth, set as `Overlay::transform` (column-major) and reused to project
  label anchors. **World geometry** (box, ticks, through-axes) goes through it;
  **labels are screen-space glyph quads** (ADR-0023, NDC-baked — they do not use the
  transform, so text stays upright and a fixed pixel size).

- **Box silhouette (the placement primitive):** a cube edge is an **outer/silhouette
  edge** iff its two adjacent faces differ in facing — `sign(n₁·d) ≠ sign(n₂·d)` for
  face normals `n₁,n₂` and view direction `d` (exact; no convex hull). Recomputed per
  frame from the camera. Used for both outer box ticks and label-edge choice.

- **Bounding box** (`PlotAxes::boundingBox`): the 12 unit-cube edges as overlay lines.

- **Box ticks** (`PlotAxes::boxTicks`): short **world-space** marks perpendicular to
  the axis at each ADR-0024 major/minor world position (major longer than minor),
  pointing **outward** along an adjacent face. `BoxTickStyle::Outer` (default) emits
  them only on **silhouette edges**; `AllFaces` on all 12 edges.

- **Through-axes** (`PlotAxes::throughAxes`): for each, a world-space line parallel to
  `direction` at the data-unit location (ADR-0024 `world(...)` on the two off-axis
  components), spanning the box, with tick marks along it (no labels).

- **Tick labels** (`PlotAxes::tickLabels`): for each axis, choose **one silhouette
  edge** to carry labels — the one whose **outward screen normal** `n = normalize(midₛ
  − cₛ)` (edge midpoint vs the projected-corner centroid, in screen space) best fits a
  **down-and-out** preference (maximize `n·(0,+1)` then distance from `cₛ`). Place each
  major tick's `formatTick` value at its **projected** position, offset **outward** by
  `n·margin_px`; this is on the silhouette and offset away from the box, so it **never
  overlaps the data**. The box silhouette is computed for label anchoring **even when
  `boundingBox` is off** (placement is decoupled from drawing the box).

- **Axis labels** (`PlotAxes::axisLabels`): the ADR-0024 rendered axis label, centered
  along that axis's labeled edge, further outward.

- **Title** (`PlotAxes::showTitle`): `PlotAxes::title`, screen-space top-center.

- **The builder** (host; in the text/annotation layer since labels need a `Shaper`)
  takes `PlotAxes` + camera + aspect + framebuffer size + a `Shaper`, and **fills an
  `Overlay`** `{lines, glyphs, glyphAtlas, transform}` honoring every visibility
  toggle. `render()` and `recordFrame()` (ADR-0025) draw it identically; the viewer
  rebuilds it each frame as the camera orbits.

## Contract Specification
- `viewProjection(camera, aspect)` is correct iff, for world `p ∈ [0,1]³`, `M·(p,1)`
  lands at the same pixel the ADR-0012 ray for that pixel passes through `p`
  (assertable: project the 8 cube corners; their screen bounds match the rendered
  volume silhouette within a pixel tolerance).
- Silhouette predicate as above; box-tick world positions equal `world(axis, v)` for
  ADR-0024 ticks; major mark length > minor. `Outer` ⇒ ticks only on silhouette edges.
- Exactly one labeled edge per axis (a silhouette edge); labels are screen-space glyph
  quads at the projected tick position + outward `margin_px`; text is screen-aligned
  (no 3-D rotation/scale); labels lie outside the projected box.
- Through-axes are world lines at the data-unit location; tick marks, **no labels**
  (ADR-0024).
- The builder fills `Overlay{lines, glyphs, glyphAtlas, transform}`; absent toggles
  emit nothing. Conventions: ADR-0012 frame. Output is deterministic for a fixed
  camera/framebuffer (testable headless via readback).

## Consequences
- A labeled, navigable plot in both the headless image and the live viewer — the M7
  payoff. The shared `viewProjection` guarantees box/label/volume agreement; the
  silhouette rule guarantees labels never cover the data and follow the camera.
- Establishes the world→screen annotation pattern M8 (legend) and future 3-D
  annotations reuse. Adds the project's first view-projection matrix (must match the
  ray camera — the alignment teeth guard it).
- Per-frame silhouette/label recompute on camera motion can make a labeled edge
  *swap* at certain angles (a visible flicker); hysteresis to damp it is deferred
  (noted as a refinement, not M7-blocking).
- Screen-space labels are legible but flat; in-plane 3-D face text is not pursued.

## Alternatives Considered
- **Convex hull of the 8 projected corners for silhouette:** equivalent but heavier;
  the face-facing test is exact and cheaper.
- **Label every outer edge / all four parallel edges:** rejected — redundant, cluttered,
  and risks data overlap on inner edges; one outer edge per axis is the convention.
- **Reuse the ray basis instead of a `mat4`:** rejected — the overlay shader applies a
  `mat4`; a standard view-projection is conventional and reusable (it must still agree
  with the ray basis — verified).
- **World-space (zoom-scaling) labels / 3-D in-plane text:** rejected — labels should
  stay a fixed on-screen size and upright; screen-space anchoring gives that.
- **A separate annotation pass/pipeline:** rejected — annotations are exactly the
  ADR-0021 overlay (lines) + ADR-0023 glyphs.

## Verification
- Headless (`render()` readback), known camera: the **box edges align with the
  rendered volume silhouette**; box ticks sit at ADR-0024 world positions; with
  `Outer`, ticks appear only on silhouette edges; major tick labels render (non-blank)
  **outside** the projected box near their ticks; a through-axis draws its line + ticks
  and **no** labels. An angled and a near-axis-aligned camera both align.
- Viewer (`iv_view --frames`, ADR-0025): box + ticks + labels render validation-clean
  across frames and a resize.
- **Teeth:** (a) perturb `viewProjection` (wrong handedness / transpose / drop the
  top-left `y`-flip) → the box no longer bounds the volume silhouette → red; (b) feed
  wrong ranges so `world(axis,v)` mis-maps → tick marks/labels shift off their expected
  pixels → red; (c) invert the silhouette/down-and-out choice → a labeled edge falls
  *over* the projected box (data overlap) → an "labels outside the box" check goes red;
  (d) skip the label draw → blank → red.
