# ADR-0021: 2D Annotation / Overlay Rendering Substrate

- **Status:** Accepted
- **Date:** 2026-06-19
- **Supersedes:** none

## Context
M6/M7 must draw annotations *over* the volume render: lines (bounding box, axes,
ticks), textured quads (glyphs — ADR-0022/0023), and a legend/colorbar. The renderer
is a **compute** pipeline writing an `R8G8B8A8_UNORM` storage image (ADR-0011,
D-0021); it cannot draw vector/text geometry, and M1–M5 contain **no graphics
pipeline**. Annotations are partly **2D screen-space** (legend, labels) and partly
**3D, camera-projected** (the bounding box corners), so they need the ADR-0012 camera.
This ADR establishes the project's first graphics pipeline and how the overlay
composites — without it M6 has nowhere to put glyphs. Cites ADR-0006 (UNORM
convention, readback), ADR-0012 (camera), D-0016 (classic 1.0 barriers), ADR-0007.

## Decision
**An overlay graphics pass composited over the volume image.**
- The render target gains **`eColorAttachment`** usage (alongside `eStorage` /
  `eTransferSrc`). The compute volume pass writes it as before; then a **graphics
  pipeline** (vertex + fragment) draws the overlay **into the same image** in a
  **classic `VkRenderPass`** with `loadOp = eLoad` (preserve the volume) and
  **standard alpha blending** (`srcAlpha, 1−srcAlpha`), so line AA and glyph coverage
  composite *over* the volume.
- Two primitive kinds: **lines** (box/axes/ticks) and **textured/encoded quads**
  (glyphs — ADR-0023; and solid quads for the legend). The pass receives the camera
  (reuse the ADR-0012 ray-gen basis as a view-projection) to project 3D points;
  pure-2D elements are placed directly in clip/screen space.
- **Same path headless and windowed.** The overlay is recorded after the volume
  dispatch in both `Renderer::render()` (then transition → `eTransferSrc` → readback,
  ADR-0006) and the viewer's `recordFrame` (then blit to the swapchain, ADR-0017).
  Layout flow per frame: compute writes (`eGeneral`) → barrier to
  `eColorAttachmentOptimal` → render pass (overlay) → barrier to `eTransferSrc`.
- **Classic render pass + framebuffer** (created per target extent), consistent with
  the classic-1.0 posture (D-0016) and requiring no new device features.

For M6 the substrate is proven by drawing a **test line + a glyph quad**; the actual
box/axes/legend are M7 (this ADR is the substrate, not the annotations).

## Contract Specification
- The volume render target is also an `eColorAttachment`; a graphics pipeline draws
  the overlay over it in a classic render pass (`loadOp = eLoad`, alpha blend).
- Overlay primitives: lines + (textured/encoded) quads; 3D points projected with the
  ADR-0012 camera, 2D elements in screen space.
- Identical in headless `render()` and the windowed present path; classic 1.0
  barriers; single-threaded (ADR-0007).

## Consequences
- A reusable 2D overlay for all M6/M7 annotations, working for both publication
  (headless) output and interactive inspection.
- Introduces the first graphics pipeline (render pass, framebuffer, blend state) —
  more surface than compute, but standard and contained.
- Drawing into the volume image (vs. a separate layer) keeps compositing trivial and
  readback/blit unchanged downstream.

## Alternatives Considered
- **Composite annotations in compute** (no graphics pipeline): rejected — lines and
  resolution-independent glyph coverage want rasterization/blend hardware.
- **Separate overlay attachment then composite:** rejected for M6 — drawing directly
  into the volume image with `loadOp = eLoad` is simpler; revisit if annotations need
  independent post-processing.
- **`VK_KHR_dynamic_rendering`** (no render-pass/framebuffer objects): deferred — a
  classic render pass matches D-0016 and adds no feature toggle; revisit to cut
  boilerplate.

## Verification
- A drawn test element (line + glyph quad) appears in the **headless readback** at
  the expected pixels, and over the volume in the viewer, validation-clean.
- **Teeth:** omit the `→ eColorAttachmentOptimal` (or `→ eTransferSrc`) transition →
  a validation error; or skip the overlay draw / wrong blend → the expected overlay
  pixels are absent/incorrect in the readback → red.
