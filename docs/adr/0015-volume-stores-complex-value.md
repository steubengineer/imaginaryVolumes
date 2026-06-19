# ADR-0015: Volume Texture Stores the Complex Value (re, im); Magnitude/Phase Derived In-Shader

- **Status:** Accepted
- **Date:** 2026-06-19
- **Supersedes:** ADR-0009

## Context
ADR-0009 stored each voxel as `(magnitude = abs(z), phase = arg(z))` in a 3D
RG32F texture. M4 rendering exposed a **correctness defect**: the GPU samples the
volume with linear (trilinear) filtering, and **a phase *angle* cannot be linearly
interpolated across its ±π branch cut**. Between adjacent voxels with phase ≈ +π
and ≈ −π, interpolation yields ≈ 0 — a spurious value that paints a thin seam
along the negative-real axis (cyan in the HSV map, dark in twilight). This was
confirmed by a controlled A/B on a face-on phase wheel: the seam is present with
linear filtering and **vanishes** with nearest filtering. Magnitude interpolates
correctly; only the angle is discontinuous.

Cites: **ADR-0009** (superseded), **D-0004** (store *derived* (mag,phase) —
revised), **D-0005** (derive in input precision — revised), **D-0006** (x-fastest
layout, unchanged), **ADR-0010** (magnitude range), **ADR-0011/0012** (the renderer
samples the volume with a linear sampler), **ADR-0004** (binding/ownership),
**D-0020** (precision-path ULP differences). Decision journaled at **D-0025**.

## Decision
The 3D RG32F volume texture stores the **raw complex value**: **R = Re(z),
G = Im(z)** (no longer magnitude/phase). **Magnitude and phase are derived
per-sample in the shader** from the hardware-interpolated `(re, im)`:
`magnitude = length(vec2(re, im))`, `phase = atan2(im, re) ∈ [−π, π]`. Because the
complex value is continuous across the negative-real axis, both its interpolation
and the derived magnitude/phase are correct everywhere — the seam is eliminated,
and the renderer keeps linear filtering.

Everything else from ADR-0009 is **unchanged**: format (`eR32G32Sfloat`), 3D image
config, usage (`eSampled|eTransferDst|eTransferSrc`), device-local + staging
upload via `copyBufferToImage`, x-fastest tight packing (D-0006), resting layout
`eShaderReadOnlyOptimal`, sampling view, ownership/lifetime, and the fence-gated
readback path. **Only the channel contents change.** The transfer function
(ADR-0013) and colormap (ADR-0014) consume the shader-derived magnitude/phase
exactly as before — their contracts are untouched.

### Precision — where the double→float conversion occurs
This is stated explicitly because it moves relative to ADR-0009/D-0005:

- **Stored texels.** For `std::complex<double>` input, each voxel's `Re(z)` and
  `Im(z)` are narrowed **`double → float` on the host, inside `deriveField`, at the
  moment they are written into the fp32 staging buffer** (the upload path). For
  `std::complex<float>` input the components are stored unchanged. This per-voxel
  narrowing of the two components is the **only** conversion applied to the stored
  field values; from there everything (store, interpolate) is fp32.
- **Magnitude and phase.** Derived **in the shader, in fp32**, from the
  (interpolated) `(re, im)`. They are **no longer computed on the host** and **not**
  in double precision — a deliberate change from D-0005. The visualization impact
  is ≤ 1 ULP (cf. D-0020).
- **Magnitude range (ADR-0010).** Still computed **on the host in the input
  precision** — `|z|` in `double` for double input — then the two bounds
  (`minPositive`, `max`) are narrowed to fp32. So the normalization range retains
  input-precision accuracy even though per-voxel magnitude is reconstructed in
  fp32 on the GPU. (A voxel's in-shader fp32 magnitude may exceed the
  double-derived `max` by ≤ 1 ULP; the transfer function clamps to `[0,1]`, so this
  is harmless.)

## Contract Specification
- **Texel:** RG32F, **R = Re(z), G = Im(z)** (raw, un-normalized).
- **Derivation (in-shader / consumer):** `magnitude = sqrt(re² + im²)` (≥ 0);
  `phase = atan2(im, re) ∈ [−π, π]`; a zero sample → `magnitude 0`, `phase 0`
  (`atan2(0,0) = 0`).
- **Readback:** `VolumeReadback::Texel` becomes `{ float re; float im; }` (was
  `{magnitude, phase}`); `at(x,y,z)` returns the raw stored components. The
  host→texel→readback round-trip for `(re, im)` is **bit-exact** versus an
  expectation computed by the same input-precision-then-narrow path (a TRANSFER
  copy neither filters nor converts) — the ADR-0009 readback teeth, now on
  `(re, im)`.
- **Layout / format / usage / upload / resting layout / sampling view / lifetime /
  concurrency / errors:** per ADR-0009, unchanged (channels now carry `(re, im)`).
- **Precision:** as the *Precision* section above.
- **Magnitude range (ADR-0010):** semantics unchanged; computed from `|z|` on the
  host in input precision, then narrowed.

## Consequences
- Eliminates the phase-seam artifact; the renderer correctly uses linear
  filtering.
- M3 upload is **simpler**: store the value directly — no host `abs`/`arg` for
  storage (only the magnitude *range* still needs host `abs`).
- Adds a per-sample `length` + `atan2` in the shader (cheap; one of each per step).
  The linear/log opacity toggle and colormap selection remain pure uniforms — no
  re-upload (the D-0004 benefit is preserved, just realized in-shader).
- Interpolating the complex value is the physically-correct reconstruction of a
  sampled complex field (interpolating magnitude/phase never was).
- Per-voxel magnitude/phase are reconstructed in fp32 rather than carried from
  input precision; the *range* keeps input precision. (fp64 GPU work stays at
  Backlog B-0004.) Storage size is unchanged (RG32F).

## Alternatives Considered
- **Keep (magnitude, phase); sample nearest:** rejected — removes interpolation
  everywhere (blocky output) to dodge one seam.
- **Manual angle-aware trilinear in-shader** (8 nearest taps, interpolate
  `cos`/`sin`, then `atan2`): rejected — heavier and slower for no benefit over
  storing `(re, im)`.
- **Store `(magnitude, cosθ, sinθ)` in RGB32F:** rejected — 3 channels (more
  memory) and still needs `atan2`; `(re, im)` is equivalent, smaller, simpler.
- **Keep ADR-0009 unchanged:** rejected — the seam is a real correctness defect.

## Verification
- **Regression / fix proof:** the face-on HSV phase-wheel diagnostic that exposed
  the bug renders **clean** under linear filtering after the fix — no cyan line at
  `θ = ±π`. Teeth: revert to storing/interpolating the angle → the cyan (HSV) /
  dark (twilight) seam returns.
- **Round-trip (M3 tests, updated to `(re, im)`):** readback of `(re, im)` is
  bit-exact versus the host expectation; the magnitude range (from `|z|`) is
  unchanged. Both `float` and `double` input paths verified (the double path
  narrows `re`/`im` on the host as specified).
- **M4 known cases unchanged:** empty → background; uniform-phase → expected hue;
  front-to-back order; linear/log — the shader derives the same magnitude/phase
  those tests expect for their (well-behaved) fields.
- Build warning-clean under `-Werror`; ASan+UBSan `ctest` green; validation-clean.
