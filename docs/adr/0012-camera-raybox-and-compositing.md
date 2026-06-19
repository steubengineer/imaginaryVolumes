# ADR-0012: Camera, Ray/Box Intersection & DVR Compositing

- **Status:** Accepted
- **Date:** 2026-06-19
- **Supersedes:** none

## Context
This ADR fixes the **geometry and integration** of the ray-marcher: the
coordinate frame (a §5 convention), how camera rays are generated, how a ray
intersects the volume, and how samples are composited. The per-sample
`(rgb, α)` it consumes are defined by ADR-0013 (opacity) and ADR-0014 (color).
Cites **D-0008** (front-to-back ray-marched DVR), **ADR-0011** (compute substrate;
the uniform buffer), **ADR-0009** (the volume is a 3D texture; texel `(x,y,z)` in
x-fastest layout), **ADR-0006** (top-left image readback origin).

## Decision
**Coordinate frame (§5, library convention).** Right-handed world. The volume
occupies the axis-aligned **unit cube `[0,1]³`** in world space, and a world
position `p ∈ [0,1]³` is used **directly as the normalized 3D texture
coordinate** (so trilinear sampling at `p` interpolates between texel centers at
`((i+0.5)/nx, (j+0.5)/ny, (k+0.5)/nz)`). Field axis `(x,y,z)` → world `(X,Y,Z)`;
**+Y is up**, +X right, +Z toward the viewer. The rendered **image origin is
top-left** (matches ADR-0006): pixel `(0,0)` is the top-left ray, `+px` → right,
`+py` → down.

**Camera (M4: fixed pinhole).** Parameters `{eye, target (default cube center
(0.5,0.5,0.5)), up (default +Y), vfov, aspect = width/height}`. The host builds an
orthonormal basis `w = normalize(eye − target)`, `u = normalize(cross(up, w))`,
`v = cross(w, u)`; with `halfH = tan(vfov/2)`, `halfW = aspect·halfH`, it passes
in the uniform buffer: `eye`, `topLeftDir = −halfW·u + halfH·v − w`,
`horizontal = 2·halfW·u`, `vertical = 2·halfH·v`. Per pixel `(px,py)`:
`s = (px+0.5)/w`, `t = (py+0.5)/h`; **ray** = `{ origin = eye,
dir = normalize(topLeftDir + s·horizontal − t·vertical) }`. (`−t·vertical` makes
`py` increase downward, honoring the top-left origin.) M5 will recompute these
params for orbit/zoom.

**Ray/box intersection.** Slab method against `[0,1]³` → `[t0,t1]`;
`enter = max(t0, 0)`. If `t1 < enter` (or `t1 < 0`) the ray **misses** → the pixel
is the **background** (uniform `background` RGBA, default opaque black).

**Sampling & compositing.** March **`stepCount` fixed steps** (uniform, default
`256`) uniformly across `[enter, t1]` (`dt = (t1 − enter)/stepCount`, sample at
segment midpoints). At each sample position `p`, sample the volume (linear) →
`(magnitude, phase)`; map to `α` (ADR-0013) and `rgb` (ADR-0014). Accumulate
**front-to-back `over`**:
```
C += (1 − A) * α * rgb;
A += (1 − A) * α;
```
**Early-ray termination** when `A ≥ alphaTermination` (uniform, default `0.995`).
Final pixel composites over the background: `rgb_out = C + (1 − A)·background.rgb`,
`a_out = 1` (opaque output). **No step-size opacity correction in M4** (per-sample
`α` used directly); `stepCount` is part of the contract (documented
simplification; correction may be added later).

## Contract Specification
- Frame: right-handed, **+Y up**, volume = `[0,1]³`, **world position = texture
  coordinate**, image origin **top-left**.
- Ray gen: exactly the `eye / topLeftDir / horizontal / vertical`, `s,t` formula
  above; `dir` normalized.
- Intersection: slab vs `[0,1]³`; `enter = max(t0,0)`; `t1 < enter` ⇒ background.
- Compositing: front-to-back `over` with the recurrences above; early-out at
  `A ≥ alphaTermination`; output over background, `a_out = 1`; `stepCount` fixed
  per dispatch (no opacity correction).
- All parameters are supplied via the ADR-0011 uniform buffer; the volume is the
  combined-image-sampler input (ADR-0011).

## Consequences
- World = texture coordinate makes sampling trivial and ties rendering to the M3
  layout (a wrong axis order would be visible).
- Fixed `stepCount` is deterministic and exactly testable, but is not
  opacity-corrected — brightness depends on `stepCount` (documented; correction
  deferrable without changing the public surface).
- Front-to-back enables early-ray termination (a perf lever for M5).
- A fixed camera is enough for headless verification; M5 layers interaction on the
  same uniforms.

## Alternatives Considered
- **Back-to-front compositing:** rejected — precludes early termination and the
  associativity teeth; D-0008 chose front-to-back.
- **Model matrix / volume ≠ `[0,1]³`:** rejected for M4 — unnecessary; M5 may add a
  model transform for framing.
- **Adaptive / jittered / stochastic sampling:** rejected — nondeterministic,
  breaks exact pixel teeth; revisit for quality/perf later.
- **Opacity (step-size) correction now:** deferred — adds a `dt`-dependent
  `1−(1−α)^(dt/dt_ref)` term; not needed for M4's contract and complicates the
  known-case math.

## Verification
- **Empty field** (all-zero) → every pixel equals `background` (α=0 everywhere).
  Teeth: make a miss return non-background → red.
- **Uniform field** (constant magnitude & phase) → rays through the box composite
  to a color the host predicts by replaying the recurrence with the same
  `stepCount`; rays missing the box read `background`. Teeth: shrink/translate the
  intersection box → the silhouette (which pixels are background) changes → red.
- **Compositing-order teeth:** a field whose phase differs front-half vs back-half
  along the view axis composites differently front-to-back vs back-to-front;
  flipping the recurrence makes the known pixel diverge → red.
- Validation-clean; deterministic across runs (ADR-0007).
