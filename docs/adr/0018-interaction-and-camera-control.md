# ADR-0018: Interaction & Camera-Control API

- **Status:** Accepted
- **Date:** 2026-06-19
- **Supersedes:** none

## Context
The viewer must let the user inspect a volume by orbiting and zooming, driving the
existing camera parameters (`RenderParams.eye`/`target`/`up`, ADR-0012) from GLFW
input (ADR-0016). Cites D-0001 (thin viewer over the offscreen core), ADR-0012
(right-handed, +Y up), ADR-0007 (single-threaded).

## Decision
**`iv::OrbitCamera` (host-only; no Vulkan, no GLFW).** State: `target` (default the
cube centre `(0.5, 0.5, 0.5)`), `distance`, `yaw`, `pitch`. Operations:
- `orbit(dYaw, dPitch)` — adds to yaw/pitch; **pitch clamped** to
  `(−π/2 + ε, π/2 − ε)` to avoid the pole flip; yaw wraps freely;
- `dolly(factor)` — multiplies `distance`, **clamped** to `[minDistance,
  maxDistance]`;
- `eye()` / `applyTo(RenderParams&)` — `eye = target + distance · (cos pitch·cos yaw,
  sin pitch, cos pitch·sin yaw)`, with `up = +Y` (right-handed; matches ADR-0012).

This type is pure and **unit-testable** without a window.

**`iv::vk::Viewer` (the windowed application).** Owns the GLFW window, the
presentation `Context` (ADR-0016), the swapchain + frame loop (ADR-0017), a
`Renderer`, an `OrbitCamera`, and the current `Volume` + `RenderParams`. Input
mapping:
- **left-mouse drag → orbit** (pixel deltas scaled to radians);
- **scroll → zoom** (`dolly`);
- keys: `Esc` quit · `L` toggle linear/log opacity · `C` toggle colormap
  (LUT ↔ HSV) · `R` reset camera.

API: `Viewer::create(...) -> Result<Viewer>`; `run()` enters the present loop until
the window closes; `runFrames(std::uint32_t n)` renders `n` frames then returns
(for verification, ADR-0017). Each frame: poll events, apply the camera + toggles
to `RenderParams`, render + present.

**Headless camera control (no window required).** The canonical camera
specification is the ADR-0012 `RenderParams` (`eye`, `target`, `up`,
`vfovRadians`), consumed by the **headless** `Renderer::render(volume, w, h,
params)`. Any caller — including other software embedding the library — has full
control of the viewpoint and view direction headlessly by populating `RenderParams`
directly (view direction = `target − eye`); no window, surface, or `Viewer` is
involved. `OrbitCamera` is **optional**: being pure host code it produces a
`RenderParams` from orbit/zoom state with or without a window, so a headless caller
may use it (e.g. scripted turntable views) or bypass it entirely. Only the `Viewer`
is GLFW/display-coupled. Pan and arbitrary up-axis are deferred.

## Contract Specification
- `OrbitCamera`: pitch clamped to `±(π/2 − ε)`; distance clamped to `[minDistance,
  maxDistance]`; `eye()` per the formula above (right-handed, +Y up, ADR-0012);
  pure host code.
- `Viewer`: left-drag orbits, scroll zooms, the listed keys; `run()` /
  `runFrames(n)`; single-threaded (ADR-0007); maps the camera onto the
  ADR-0012 `RenderParams` consumed by the `Renderer` (unchanged).
- **Headless camera control:** `RenderParams` (ADR-0012) is the camera contract for
  the offscreen `Renderer::render()`; a caller sets viewpoint/direction with no
  window. `OrbitCamera` and `Viewer` are optional layers above it — neither is
  required to render headlessly with an arbitrary camera.

## Consequences
- Orbit + zoom is enough to inspect a volume from any direction at any distance;
  the camera math is verified without a window.
- The `L`/`C` toggles exercise the M4 transfer-function / colormap live (good demo
  value and a manual cross-check of ADR-0013/0014).
- **Headless / embedding callers get full camera control via `RenderParams`
  (ADR-0012) with no window** — the "embed the library and script viewpoint /
  direction" use case is the default offscreen path, not a special case;
  `OrbitCamera` and `Viewer` are optional conveniences on top.
- Pan, roll, and trackball interaction are deferred.

## Alternatives Considered
- **Trackball / arcball camera:** deferred — orbit (yaw/pitch/distance) is simpler
  and sufficient for inspection.
- **Configurable input bindings:** overkill for M5; fixed bindings suffice.
- **Driving the camera with a model matrix instead of moving the eye:** rejected —
  ADR-0012 fixes the volume at `[0,1]³`; moving the eye is the natural inverse.

## Verification
- **Host unit tests** for `OrbitCamera`: `orbit` moves `eye()` as expected; the
  pitch and distance clamps hold at their limits; `eye()` matches the closed form
  for known `(yaw, pitch, distance)` (e.g. yaw=0,pitch=0 ⇒ `eye = target +
  (distance,0,0)`). Teeth: break a clamp or perturb the `eye()` formula → red.
- The `Viewer` input handling is exercised by `runFrames` on the display (smoke /
  manual); validation cleanliness is covered by ADR-0017.
