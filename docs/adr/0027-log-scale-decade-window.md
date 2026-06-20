# ADR-0027: Log-Scale Decade Window for the Opacity Transfer Function

- **Status:** Accepted
- **Date:** 2026-06-19
- **Supersedes:** none
- **Extends:** ADR-0013

## Context
ADR-0013's logarithmic opacity mode normalizes over the data's full magnitude range
`[minPositive, max]`, so the number of displayed decades is
`log10(max / minPositive)` — **fixed by the data**. For high-dynamic-range fields
(a tiny `minPositive`, e.g. a noise floor near zero) the log ramp spans many decades
and compresses the interesting top end. The maintainer wants to **specify how many
decades** of the log scale are displayed, independent of the data's floor. This
**extends ADR-0013** (the abs→α transfer function), like ADR-0020 did. Cites
ADR-0010 (`MagnitudeRange{minPositive,max}`), ADR-0012 (composites the resulting `α`),
ADR-0011 (the uniform buffer). Opacity only; color is ADR-0014.

## Decision
Add a public render parameter **`RenderParams::logDecades`** (`float`, default `0`)
that, in logarithmic mode (`opacityMode == 1`), windows the opacity ramp to the top
`logDecades` decades below `max`:

- **`logDecades > 0`:** the ramp spans `[max·10^(−logDecades), max]`. For `m > 0`:
  `mn = clamp( 1 + log10(m / max) / logDecades, 0, 1 )`; for `m ≤ 0` → `mn = 0`. So
  `m = max → mn = 1`, `m = max·10^(−logDecades) → mn = 0`, below → `0`.
- **`logDecades ≤ 0` (default):** the **unchanged ADR-0013 mapping** over
  `[minPositive, max]` — backward-compatible.

Linear mode (`opacityMode == 0`) is **unaffected**. The per-sample opacity remains
`α = clamp(mn · densityScale, 0, 1)` (ADR-0013), and the step-spacing correction
(ADR-0020) is applied after, unchanged. It is a **live uniform** (no re-upload), like
the rest of ADR-0013.

## Contract Specification
- New public field `float RenderParams::logDecades{0.0f}` (`include/iv/vk/renderer.hpp`).
- Shader (`ray_march.comp`), log branch, exactly:
  - `logDecades > 0`: `mn = clamp(1.0 + (log(m) - log(maxM)) / (logDecades · ln10), 0, 1)`
    for `m > 0`, else `0` (`ln10 = 2.302585…`; `log` is natural log, matching ADR-0013).
  - `logDecades ≤ 0`: the existing `(log(m) − log(minP)) / (log(maxM) − log(minP))` form.
- **No NaN/Inf:** the `m > 0` (and `max > 0`) guards precede any `log`; degenerate
  `max ≤ 0` ⇒ `mn = 0` (ADR-0013).
- **Backward compatibility:** `logDecades == 0` reproduces the ADR-0013 log result
  bit-for-bit; all existing renders/tests are unchanged.
- **UBO:** `logDecades` is packed into a spare slot of the existing `modes` vec4 (as
  float bits via `uintBitsToFloat`); the std140 layout and `sizeof(Ubo) == 128` are
  **unchanged**. Threaded through `fillUbo` for both `render()` and `recordFrame()`.

## Consequences
- High-dynamic-range fields get a controllable, data-independent log window — the
  top N decades — instead of a ramp dictated by the noise floor. Reuses raw-magnitude
  storage (D-0004): a pure uniform flip, live in the viewer.
- One more transfer-function knob to document; no layout/ABI change (spare UBO slot).
- Does not address per-decade gamma or a movable window center (still anchored at
  `max`); deferred.

## Alternatives Considered
- **A fixed default decade count (always on):** rejected — would change every existing
  log render; `0 = full data range` keeps ADR-0013's behavior the default.
- **Window by an absolute floor magnitude instead of decades:** rejected — "decades"
  is the maintainer's mental model and is data-scale-independent; an absolute floor is
  recoverable as `max·10^(−logDecades)` anyway.
- **A new UBO field / larger buffer:** rejected — the `modes` vec4 has spare slots, so
  no std140/size change is needed.

## Verification
- **Predicted-α (log decades):** for a chosen `m`, `max`, and `logDecades`, the host
  computes `mn = clamp(1 + log10(m/max)/logDecades, 0, 1)` and the expected
  single-sample `α`; a thin uniform slab renders to the predicted value.
- **Backward compatibility:** `logDecades = 0` reproduces the existing log-mode render
  (the ADR-0013 test stays green unchanged).
- **Teeth:** perturb the window (wrong `logDecades`, or revert the floor to
  `minPositive`) → the predicted pixel diverges → red; a high-dynamic-range slab at
  `logDecades = 2` vs `4` composites to different brightness.
