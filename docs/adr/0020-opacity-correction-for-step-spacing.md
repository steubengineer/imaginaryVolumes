# ADR-0020: Opacity Correction for Ray Step Spacing

- **Status:** Accepted
- **Date:** 2026-06-19
- **Supersedes:** none
- **Extends:** ADR-0013 (opacity transfer function)

## Context
ADR-0013 maps a sample's magnitude to a per-sample opacity `a` (linear/log, scaled
by `densityScale`) and the shader composites front-to-back. **D-0030 / B-0008**
found that `a` is applied **independent of the ray step spacing `dt`**: the loop adds
the same `a` per sample regardless of how finely the ray is sampled, so the
accumulated opacity — hence the displayed density and the early-termination point —
**depends on `stepCount`** (more steps → denser image) and ignores how much material
a ray actually traverses (a long, oblique ray accumulates the same as a short one at
equal `stepCount`). For a *quantitative* tool the rendered density must be invariant
to the sampling rate and reflect path length. Cites ADR-0011/0012 (the compute
ray-marcher; `dt = (t1 − enter)/N` is a world-space distance since `dir` is
normalized) and ADR-0007 (deterministic inputs).

## Decision
**Standard direct-volume opacity correction.** Replace the per-sample opacity with
the spacing-corrected value

  `α_i = 1 − (1 − a_i)^(dt / dt_ref)`

where `a_i` is the ADR-0013 opacity (unchanged: linear/log × `densityScale`), `dt` is
the current sample's world-space spacing, and **`dt_ref`** is a fixed reference
spacing recorded here. `dt_ref` is chosen so that opacities are interpreted as
"authored" for a reference sampling rate: `dt_ref = 1 / kReferenceSteps` with
`kReferenceSteps` a documented constant (initially **256**, the default `stepCount`),
i.e. opacities authored for a unit-length path sampled 256×. The composite (`over`)
and early-ray termination (ADR-0012) are unchanged; only `a_i → α_i` changes.

Edge cases: `a_i = 1 ⇒ α_i = 1` (`pow(0, x)=0`); `dt_ref > 0` by construction;
`densityScale` keeps its meaning (scales `a_i` before correction). The new constant
(or a uniform) is added to the std140 UBO.

## Contract Specification
- Per-sample opacity is corrected: `α = 1 − (1 − a)^(dt / dt_ref)`, `a` per ADR-0013.
- The rendered density of a fixed field + camera is **invariant to `stepCount`**
  within a stated tolerance (above a minimum adequate rate), and now scales with a
  ray's path length through the volume (oblique rays accumulate more).
- `dt_ref = 1 / kReferenceSteps`, `kReferenceSteps = 256` (documented; a change is an
  ADR note). `densityScale` semantics preserved.

## Consequences
- Density becomes a property of the field + transfer function, not of an arbitrary
  sampling budget — a prerequisite for quantitative plots (and for the M7 colorbar to
  mean something).
- More physically faithful (path-length aware).
- With correction, raising `stepCount` no longer darkens the image (only refines
  sampling), so the ADR-0019 perf teeth must keep using `--no-early-term` (D-0030);
  the contract bound itself is unaffected.

## Alternatives Considered
- **Leave opacity uncorrected:** rejected — non-quantitative; the image depends on a
  knob unrelated to the data (B-0008).
- **Pre-integrated transfer functions:** deferred — higher fidelity for sharp
  transfer functions, but heavier; the analytic correction suffices here.
- **Expose `dt_ref` / reference steps as a public knob:** deferred — a fixed,
  documented constant keeps the contract simple; revisit if callers need it.

## Verification
- An invariance test: render a fixed field + camera at `stepCount` 128 and 512 (or
  compare representative accumulated-opacity pixels) — the results match within
  tolerance with correction on.
- **Teeth:** revert to the uncorrected `a` (or set the exponent to 1) → the 128-step
  and 512-step renders **diverge** (the finer one is denser) → the invariance test
  goes red; corrected → green.
