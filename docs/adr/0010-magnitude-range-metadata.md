# ADR-0010: Magnitude-Range Metadata

- **Status:** Accepted
- **Date:** 2026-06-19
- **Supersedes:** none

## Context
M4's opacity transfer function (`abs` → opacity, linear *or* logarithmic) needs a
**normalization range** for the magnitude channel. Computing it per-frame on the
GPU is wasteful, and a logarithmic mapping needs the smallest *positive*
magnitude (a floor; `log(0)` is undefined). The host already visits every sample
to derive `(magnitude, phase)` (ADR-0009), so the range is a free by-product of
that pass.

This ADR fixes **how the range is computed and exposed**, and a **caller
override** for pinning normalization across an animation/series. It deliberately
does **not** define the normalization formula itself — that (linear vs log,
`log(0)` handling, the degenerate cases) is M4's shader/uniform contract. Cites
D-0004/D-0005 (derive in input precision), ADR-0008 (ingestion entry +
`VolumeOptions`), ADR-0009 (the single host pass). Realizes MILESTONES M3's
"magnitude-normalization contract."

## Decision
During ingestion, in the same host pass that derives `(magnitude, phase)`,
compute (in input precision, then narrow to `float`):
- **`max`** = the greatest `abs(z)` over all voxels.
- **`minPositive`** = the least `abs(z)` over voxels with `abs(z) > 0`; if **no**
  voxel has positive magnitude (all-zero field), `minPositive = 0`.

Expose:
```cpp
namespace iv {
  struct MagnitudeRange { float minPositive; float max; };
  struct VolumeOptions  { std::optional<MagnitudeRange> magnitudeRange; };
}
```
On the Volume (ADR-0009):
- `magnitudeRange()` → the **override** if `VolumeOptions::magnitudeRange` was
  provided, else the auto-computed range.
- `autoMagnitudeRange()` → **always** the auto-computed range (diagnostics /
  comparison), regardless of override.

**Override validation:** if provided, require `minPositive >= 0` and
`max >= minPositive`, else `Errc::invalid_argument` (this allows `minPositive ==
0`, and `max == 0` only when `minPositive == 0`).

The normalization mapping (how `[minPositive, max]` becomes opacity, linear or
log, and the degenerate `minPositive == max` / all-zero handling) is **out of
scope — deferred to M4**.

## Contract Specification
- **Types:** `iv::MagnitudeRange{ float minPositive, max; }`;
  `iv::VolumeOptions{ std::optional<MagnitudeRange> magnitudeRange; }`.
- **Accessors:** `Volume::magnitudeRange()` = override-if-present-else-auto;
  `Volume::autoMagnitudeRange()` = the computed range unconditionally.
- **Auto-range invariants:** `0 <= minPositive <= max`; `minPositive` is the
  least strictly-positive magnitude, or `0` if none; `max` is the greatest
  magnitude (`max == 0` **iff** the field is all-zero). Computed in input
  precision, stored fp32.
- **Override invariants (validated):** `minPositive >= 0 && max >= minPositive`,
  else `Errc::invalid_argument`.
- Concurrency / error model per ADR-0007 / ADR-0003.

## Consequences
- M4 can build a normalized opacity transfer function — including a log mapping
  over `[minPositive, max]` — with no extra GPU pass and no `log(0)`.
- A caller override pins normalization consistently across an animation or a set
  of related volumes.
- Excluding zeros from `minPositive` keeps the log floor well-defined; M4 still
  owns the degenerate `minPositive == max` / all-zero (`max == 0`) case.
- One extra cheap reduction folded into the already-required host pass —
  negligible cost.

## Alternatives Considered
- **Compute the range on the GPU (reduction):** rejected for M3 — the host
  already touches every sample to derive `(magnitude, phase)`; a GPU reduction
  adds a kernel/pass for no benefit now.
- **Expose only `max` (normalize over `[0, max]`):** rejected — a log mapping
  needs a positive floor; `minPositive` supplies it.
- **Bake normalization into the stored texture:** rejected — D-0004 keeps raw
  magnitude so M4 toggles linear/log freely; normalizing at upload forecloses
  that.
- **Include mean/percentile statistics:** deferred — not needed for the M4
  contract; extensible later without breaking this one.

## Verification
- **Unit (host, no GPU):** a known small field → `autoMagnitudeRange()` equals
  the expected `(minPositive, max)`. The field includes zeros plus a unique
  smallest-positive and a unique largest magnitude. Teeth (fault injection):
  include zeros in the `minPositive` reduction (→ wrongly `0`) → red; flip the
  `max` comparison → red. Reverted.
- **Override:** a provided override is returned verbatim by `magnitudeRange()`
  while `autoMagnitudeRange()` still reflects the data; an invalid override
  (`max < minPositive`) → `Errc::invalid_argument`. Teeth: drop the override
  validation → a bad range is accepted → red.
- **All-zero field** → `(0, 0)` (documented degenerate case).
- **Precision:** a `double`-input field's range computed in `double` then
  narrowed matches the expectation.
