# ADR-0023: GPU Glyph Rendering via libharfbuzz-gpu (Slug)

- **Status:** Accepted
- **Date:** 2026-06-19
- **Supersedes:** none

## Context
Shaped glyphs (ADR-0022) must be rendered **crisply and resolution-independently** on
the GPU — publication-quality at any output resolution and at any viewer zoom — inside
the overlay pass (ADR-0021). HarfBuzz now ships an experimental
**`libharfbuzz-gpu`**: per its README it *"encodes glyph outlines for GPU
rasterization (Slug algorithm)"* and *"provides shader sources in **GLSL**, WGSL, MSL,
and HLSL."* The Slug algorithm (Lengyel/Terathon) computes exact per-pixel coverage
from glyph outlines; its patent has been **dedicated to the public domain** (per the
maintainer), so it is free for our use, and the GLSL shaders drop into our existing
`glslc`→SPIR-V→embed toolchain (ADR-0011/D-0022). Cites ADR-0021 (overlay graphics
pass + alpha blend), ADR-0022 (vendored HarfBuzz, pinned).

## Decision
**Render glyphs with `libharfbuzz-gpu`'s Slug encoder + its GLSL shaders.**
- For each needed `(font, glyphId)`, use `libharfbuzz-gpu` to **encode** the glyph's
  outline into GPU data (buffers/textures), **cached** and uploaded once (like the
  colormap LUT, ADR-0014), reused across frames.
- In the overlay pass (ADR-0021), draw one **quad per glyph** positioned from the
  shaper's advances/offsets; the fragment shader is `libharfbuzz-gpu`'s **GLSL Slug**
  coverage shader, alpha-blended over the volume. Output is resolution-independent
  (coverage from outlines, not a fixed-size atlas).
- **Shaders through the existing toolchain:** the vendored GLSL is compiled by
  `glslc` and embedded (ADR-0011/D-0022); any entrypoint/binding glue to match our
  descriptor model is recorded with the implementation.
- **Experimental-status risk** is accepted, **mitigated** by the pinned vendored
  commit (ADR-0022). **Documented fallback:** if the experimental API proves unworkable
  at the pin, fall back to a **self-baked MSDF atlas** (msdfgen-style from the same
  `hb-draw` outlines) — same overlay/shaper interface, deferred unless needed.

## Contract Specification
- Glyphs are rendered via `libharfbuzz-gpu` Slug encoding + its GLSL shaders in the
  ADR-0021 overlay pass; shaders are compiled/embedded via the ADR-0011 toolchain.
- Glyph encodings are cached per `(font, glyphId)` and uploaded once.
- Rendering is **resolution-independent** (crisp across output sizes and viewer zoom)
  and alpha-blended over the volume.
- The experimental dependency is **pinned** (ADR-0022); the MSDF fallback is the
  documented contingency.

## Consequences
- Best-in-class, resolution-independent text suitable for publication and live zoom,
  reusing the font project's own GPU path (well-matched to HarfBuzz shaping).
- Couples us to an experimental upstream component — bounded by pinning + the MSDF
  fallback.
- Adds glyph-encoding cache management and a second (graphics) shader to the build.

## Alternatives Considered
- **Self-baked MSDF atlas (msdfgen-style):** the documented fallback — simpler and
  battle-tested, but a fixed atlas softens at extreme zoom/sharp corners; chosen
  against only because `libharfbuzz-gpu` gives true outline coverage and pairs
  directly with our shaper.
- **Glyphy (arc-SDF):** rejected — deprecated upstream (maintainer).
- **CPU rasterization to a texture (FreeType/`hb-paint`):** rejected — not
  resolution-independent; re-raster on zoom; adds a dependency.

## Verification
- Render a known string/glyph in the overlay; assert coverage at sampled pixels
  matches a recorded reference (headless readback), and that edges stay sharp when
  rendered at two scales (zoom invariance, thresholded).
- **Teeth:** skip the Slug encode (or swap in a no-op/incorrect shader) → the glyph
  pixels go blank/wrong vs. the reference → red; restore → green. Combined with the
  ADR-0022 shaping teeth, this guards the full text path end-to-end.
