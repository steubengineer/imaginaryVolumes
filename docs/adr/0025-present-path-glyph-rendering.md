# ADR-0025: Present-Path (Viewer) Glyph Rendering

- **Status:** Accepted
- **Date:** 2026-06-19
- **Supersedes:** none

## Context
ADR-0023 renders Slug glyph quads only on the **headless `render()`** path; the
present path (`recordFrame`, ADR-0017) passes `nullptr` glyphs, so the **interactive
viewer shows no text** (recorded as the D-0036 scope deviation and **B-0010**). M7's
labels (tick values, axis labels, title) must show in the viewer too — its "Done
when" requires labels in **both** paths. This ADR completes the glyph path on the
present side. Cites ADR-0017 (present loop: one frame in flight, the viewer waits the
in-flight fence before re-recording), ADR-0021 (overlay pass), ADR-0023 (the glyph
pipeline, the RGBA16I Slug atlas, `buildGlyphResources`), D-0036/B-0010. The labels'
*content and placement* are ADR-0026's concern; this ADR is the **GPU plumbing** only.

## Decision
**Persist the glyph resources across the in-flight frame in the `Renderer`, and draw
`Overlay::glyphs` in `recordFrame()` exactly as `render()` does.**

- The present path gains persistent glyph resources (vertex buffer, atlas uniform
  texel buffer + its `R16G16B16A16_SINT` view, descriptor pool/set) held in the
  `Renderer` alongside the existing `frameOverlay*` members.
- **Split by change rate** (the camera moves every frame, the text content rarely):
  - the **atlas** + its buffer view + descriptor set are (re)built **only when the
    atlas changes** (track its byte size / a content key) — glyph *outlines* are
    camera-independent;
  - the **glyph vertex buffer** is grown-on-demand and **overwritten every frame**
    (quad NDC positions are recomputed by ADR-0026 as the camera orbits), mirroring
    the persistent `frameOverlayBuf_` pattern.
- Overwriting persistent buffers each frame is safe because the viewer waits the
  in-flight fence before re-recording (ADR-0017; one frame in flight).
- `drawOverlay(...)` is called with the persistent glyph resources (not `nullptr`) on
  the present path; glyph rasterization (pipeline, atlas binding, blend) is **byte-for
  -byte the ADR-0023 path** — no new shader or pipeline.

## Contract Specification
- `Renderer::recordFrame(... , const Overlay* overlay)` draws `overlay->glyphs` using
  the ADR-0023 glyph pipeline + atlas, identical to `render()`’s output for the same
  overlay/camera. ADR-0021's "identical headless and windowed" now holds for glyphs
  too (closing the D-0036 deviation).
- New persistent `Renderer` members for the present-path glyph vertex/atlas buffers,
  view, and descriptor; lifetimes follow the existing `frame*` members (declared so
  deleters run before the device teardown). The atlas descriptor is rebuilt only on
  atlas change; the vertex buffer is overwritten per frame.
- A glyphs-only overlay (no lines/triangles) is valid on the present path (no
  zero-size buffers; mirrors the `render()` guard).
- No public API change beyond behavior (the viewer now shows `Overlay::glyphs`).
- Validation-clean (ADR-0005); one frame in flight (ADR-0017) — no extra
  synchronization introduced.

## Consequences
- The viewer shows text; M7 labels (ADR-0026) work in both paths from one code path.
- A few persistent members + a small "atlas changed?" check. Per-frame vertex
  re-upload is cheap (label glyph counts are small); the atlas upload is amortized.
- Resolves B-0010; closes the ADR-0023/D-0036 headless-only scope.

## Alternatives Considered
- **Transient per-frame `buildGlyphResources` (allocate+free every frame):** rejected
  — re-creating a descriptor pool + buffer view each frame churns allocations at
  framerate; persisting is cleaner and matches `frameOverlayBuf_`.
- **Rebuild the atlas every frame too:** rejected — glyph outlines don't change with
  the camera; only quad positions do. Splitting by change rate avoids needless
  re-encode/upload.
- **A second "text" command buffer / pass:** rejected — glyphs already share the
  ADR-0021 overlay pass; no reason to split it.

## Verification
- Extend the viewer smoke path: `iv_view --frames N` with an overlay carrying glyphs
  renders **validation-clean** across frames incl. a swapchain recreation; a headless
  cross-check (the existing `[glyph][vk]` coverage on `render()`) stays green so both
  paths agree.
- **Teeth:** skip the present-path glyph draw (as the ADR-0023 teeth did for
  `render()`) → the viewer/headed readback shows no text → a present-path coverage
  check goes red; restore → green. Re-uploading a *stale* atlas after the text
  changes (skip the "atlas changed" rebuild) → wrong glyphs → red.
