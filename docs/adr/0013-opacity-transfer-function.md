# ADR-0013: Opacity Transfer Function (abs → α)

- **Status:** Accepted
- **Date:** 2026-06-19
- **Supersedes:** none

## Context
The ray-marcher (ADR-0012) needs a per-sample opacity `α` from a voxel's
magnitude, with a **live linear/logarithmic toggle** and normalization over the
magnitude range established at ingestion. Cites **D-0004** (raw magnitude stored;
transfer function applied in-shader, so this is a free runtime toggle),
**ADR-0010** (`MagnitudeRange{minPositive, max}`; `minPositive` excludes zeros —
the log floor), **ADR-0012** (consumes the resulting `α`). The colormap (`rgb`) is
ADR-0014; this ADR is **opacity only**.

## Decision
Uniform inputs (in the ADR-0011 buffer): `minPositive`, `max` (from
`Volume::magnitudeRange()`, passed by the host; caller-overridable per ADR-0010),
`opacityMode` (`0` = linear, `1` = logarithmic), `densityScale` (default `1.0`).

From a sampled magnitude `m` (the R channel, raw `|z|`), compute a normalized
`mn ∈ [0,1]`:
- **Linear (`opacityMode == 0`):** `mn = clamp(m / max, 0, 1)`. (`m ≤ 0 → 0`;
  `m = max → 1`.) If `max ≤ 0` (all-zero field) → `mn = 0`.
- **Logarithmic (`opacityMode == 1`):** for `m > minPositive`,
  `mn = clamp( (log(m) − log(minPositive)) / (log(max) − log(minPositive)), 0, 1)`;
  for `m ≤ minPositive` (including `m = 0`) → `mn = 0`. If `max ≤ minPositive`
  (degenerate / all-zero) → `mn = 0`. `log` is the natural logarithm, and the
  `m > minPositive` branch is taken **before** any `log(m)` so `log(0)` is never
  evaluated.

Per-sample opacity: `α = clamp(mn · densityScale, 0, 1)`.

## Contract Specification
- Exactly the formulas above. `m = 0 ⇒ α = 0` in both modes.
- Degenerate ranges (`max ≤ 0`, or log with `max ≤ minPositive`) ⇒ `mn = 0` ⇒
  fully transparent — **no NaN/Inf** is produced (the guard precedes `log`).
- `densityScale` scales `mn` then clamps to `[0,1]`.
- `minPositive > 0` whenever the log branch uses it (it is the least *strictly
  positive* magnitude; an all-zero field has `max = 0` and is handled by the
  degenerate case). Both are fp32 (ADR-0010).
- This ADR outputs `α` only; `rgb` is ADR-0014; ADR-0012 composites
  `(1−A)·α·rgb`.

## Consequences
- Storing raw magnitude (D-0004) makes linear ↔ log a pure uniform flip with **no
  re-upload** — the M4 "live toggle" requirement.
- Log scaling needs a positive floor; `minPositive` (ADR-0010) supplies it, and
  excluding zeros keeps `log` defined.
- Explicit degenerate handling guarantees a finite image for empty/constant
  fields.

## Alternatives Considered
- **Normalize linear over `[minPositive, max]`** (like log): rejected — for linear
  opacity, `0 → transparent` and `max → opaque` over `[0, max]` is the natural,
  least-surprising mapping; the positive floor matters only for log.
- **Gamma / sigmoid / arbitrary curves:** deferred — `densityScale` covers the
  immediate need; richer curves are a future transfer-function extension
  (backlog).
- **Per-sample step-size opacity correction:** deferred (ADR-0012) — orthogonal to
  the magnitude→α mapping.

## Verification
- **Linear vs log teeth:** a field whose magnitudes span ~a decade renders to
  different composited brightness under the two modes; swapping the mode branch
  makes a known pixel diverge → red.
- **Zero handling:** an all-zero field → `α = 0` everywhere → background (ties to
  ADR-0012's empty-field test); a field containing zeros renders **finite** (no
  NaN) in log mode (teeth: remove the `m > minPositive` guard → NaN/garbage pixel
  → red).
- **Predicted α:** for a chosen `m`, the host computes `mn`/`α` for both modes and
  asserts the single-sample contribution (constructible via a thin uniform slab).
