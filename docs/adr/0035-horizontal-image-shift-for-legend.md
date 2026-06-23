# ADR-0035: Horizontal Image Shift — Frame the Plot Left to Make Room for the Legend

- **Status:** Accepted
- **Date:** 2026-06-22
- **Supersedes:** none

> Amends the ADR-0012 camera (adds an optional, render-inert-at-zero horizontal image shift applied
> consistently to the ray camera and the overlay view-projection); supersedes neither. Builds on the
> D-0049 facade framing and ADR-0034 legend placement.

## Context
ADR-0034 places the legend just right of the projected box, clamped to keep its value labels on
screen. But at the D-0049 framing (the cube at orbit distance 3.3, filling ~75% of the frame) the
box's right edge projects to NDC-x ≈ **0.79** — *past* where the legend can sit. With the box
centered and filling the frame there is simply **no horizontal room** on the right for a legend, so
`makePlot`/`iv_view` shows the box overlapping the swatch (the `renderPlot` eyeball missed it only
because that camera angle put the box's widest point at a corner, clear of the swatch's vertical
mid-line; the interactive OrbitCamera angle puts it dead center). Placement alone cannot fix this —
**the framing must reserve space** for the legend.

Maintainer decision (2026-06-22): **shift the plot left** and split the frame **75% plot / 25%
legend** — keeping the figure compact for publication while leaving the legend clear and readable.
A symmetric zoom-out (centered, empty left margin) and a shrink-the-legend approach were rejected.

The shift must move the **rendered volume, the world-space box/axes, and the screen anchoring**
together, or they desync. The ADR-0012 ray camera is built host-side (`fillUbo`: `eye`, `topLeft`,
`horizontal`, `vertical`) and the matching `viewProjection` (ADR-0026) is host-side too — so a
consistent shift is a pure host change, **no shader edit**.

Read: ADR-0012 (ray camera + the `topLeft/horizontal/vertical` formula and the
`viewProjection`↔ray consistency), ADR-0026 (`viewProjection`, `projectToPixel`), ADR-0034
(`placeLegendRight`), ADR-0029 (facade), D-0049 (facade framing distance).

## Decision
Add a single camera parameter — a horizontal image shift in NDC — applied **identically** to the
ray camera and the overlay projection, and have the facade set it (with a reframed distance) to put
the plot in the left 75% and the legend in the right 25%.

### (1) The camera parameter (amends ADR-0012)
`iv::vk::RenderParams` gains `float imageShiftNdcX{0.0f}` — a horizontal shift of the rendered image
in clip/NDC units (negative = left). It shifts **what the camera frames**, not the world:

- **Ray camera** (`fillUbo`, renderer.cpp): `topLeft += (−imageShiftNdcX)·halfW·u`. A world point
  that rendered at screen NDC-x `X` now renders at `X + imageShiftNdcX`. (`horizontal`/`vertical`/
  `eye` unchanged.)
- **Overlay** (`viewProjection`): `row0 += imageShiftNdcX · row3`, i.e. `clip.x' = clip.x +
  imageShiftNdcX·clip.w` ⇒ `ndc_x' = ndc_x + imageShiftNdcX`.

Both are the *same* NDC translation of the image, so the ADR-0012 invariant — a world point projects
(via `viewProjection`/`projectToPixel`) to the exact pixel its camera ray passes through — **still
holds at any shift**. At `imageShiftNdcX = 0` both formulas reduce to today's (render-inert default;
existing renders/tests unchanged).

### (2) Facade framing for the legend (presentation; D-0049 sibling)
When a legend is shown, the facade frames the plot into the **left 75%** of the frame:
`imageShiftNdcX = splitFrac − 1 = −0.25` (centers the plot in `x ∈ [−1, 0.5]`), and the cube is
framed at a slightly larger distance `kPlotFrameDistanceLegend` (tuned so the box **and its left
axis labels** fit within the left 75% without clipping). With no legend, `imageShiftNdcX = 0` and the
D-0049 distance (3.3) are kept. `placeLegendRight` (ADR-0034) is unchanged: it projects the
**shifted** box (its `viewProjection` honors the shift), finds the now-smaller right extent, and
places the legend in the freed right 25%.

The split (`0.75`) and the legend-case distance are presentation constants (tunable, journaled), not
contract; `imageShiftNdcX` is the contract.

## Contract Specification
- **Signature/layout:** `RenderParams` gains `float imageShiftNdcX{0.0f}` (after the camera fields).
  std140 UBO layout is unchanged — the shift is folded into `topLeft` host-side, adding no UBO field
  and no shader change.
- **Semantics:** `imageShiftNdcX` translates the rendered image horizontally by that NDC amount
  (negative = left), applied identically by `fillUbo` (ray) and `viewProjection` (overlay).
- **Consistency invariant (binding):** for any `imageShiftNdcX`, a world point in front of the eye
  projects through `viewProjection`/`projectToPixel` to the same pixel the ray camera samples it at
  (the ADR-0012 collinearity property) — because the shift is the same NDC translation on both
  sides. `imageShiftNdcX = 0` is byte-identical to the pre-ADR camera.
- **Ranges/degenerate:** finite shift; `halfW = aspect·tan(vfov/2)` as before. No clamp (the facade
  picks sane values); an extreme shift simply moves content off-frame.
- **Ownership/threading:** unchanged (ADR-0007) — pure host, `RenderParams` is a value; no GPU
  resource/pipeline change.
- **Error semantics:** total; no failure path.

## Consequences
- The legend never overlaps the box: the plot lives in the left 75%, the legend in the right 25%,
  at every aspect and orbit angle. Balanced (no empty side margin), matplotlib-style.
- A general, reusable camera image-shift (e.g. future multi-panel / inset layouts) at the cost of one
  `RenderParams` field; render-inert by default so all existing renders/tests are unaffected.
- The plot is modestly smaller than the full-frame D-0049 framing (it shares the frame with the
  legend) — the compact/clear trade the maintainer chose.
- Forecloses nothing: vertical shift / asymmetric frustum could be added the same way if ever needed.

## Alternatives Considered
- **Symmetric zoom-out (box centered, smaller):** rejected by the maintainer — wastes the left margin
  and unbalances the figure.
- **Shrink the legend to fit the sliver:** rejected — the legend becomes unreadable and still cannot
  fit at framing distance 3.3.
- **Asymmetric (anamorphic) x-scale of clip.x only:** rejected — distorts the box aspect; a true
  shift (translate, both axes' scale intact) plus a reframed distance keeps the box undistorted.
- **A lens shift in the shader (new UBO field):** rejected — folding it into the host-computed
  `topLeft` needs no shader/UBO change and keeps the ray↔overlay derivation in one place.
- **Pan via the OrbitCamera target:** rejected — orbiting about an off-center target swings the box
  around as you rotate; the image shift is orbit-independent.

## Verification
- **Ray↔overlay consistency at a nonzero shift (`[viewproj]`):** extend the collinearity check —
  with `imageShiftNdcX = −0.25`, a set of world points still projects (via `viewProjection`) to the
  pixel the `fillUbo` ray camera samples them at (reconstruct the ray for that pixel and confirm it
  passes through the point), within tolerance. **Teeth:** applying the shift to only one side
  (overlay OR ray) breaks collinearity → red; `imageShiftNdcX = 0` reproduces the unshifted matrix.
- **Shift direction & magnitude (`[viewproj]`):** a world point at the cube center projects to
  `ndc_x` with shift 0, and to `ndc_x − 0.25` with `imageShiftNdcX = −0.25` (exactly). **Teeth:**
  wrong sign/scale (e.g. `+0.25`, or `0.125`) diverges.
- **Facade reserves the right 25% (`[vk][plot]` / `[legend]`):** with a legend, the projected box's
  right extent sits left of the 75/25 split (`≈ 0.5`) and the legend is placed in `x ∈ [0.5, ~1]`;
  the box does not overlap the swatch. **Teeth:** dropping the shift (shift 0) leaves the box right
  extent ≈ 0.79, overlapping the legend → red. Eyeballed before/after at the `makePlot` angle.
- **Render-inert default:** the existing `[viewproj]`, `[renderer]`, `[annot]`, `[plot]` tests are
  unchanged at `imageShiftNdcX = 0` (no regression).
- **Sanitizers:** ASan+UBSan clean.
