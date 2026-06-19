# ADR-0017: Swapchain & Present Loop

- **Status:** Accepted
- **Date:** 2026-06-19
- **Supersedes:** none

## Context
The viewer must present each M4-rendered frame to the window. The renderer is a
**compute** pipeline writing an `R8G8B8A8_UNORM` storage image (ADR-0011/0012,
D-0021) — it does not draw to the swapchain directly, so frames reach the screen
by **blitting** the storage image into the acquired swapchain image. Cites
ADR-0016 (presentation `Context` + surface), ADR-0006 (UNORM convention), D-0016
(classic barriers), ADR-0007 (single-threaded).

## Decision
**Surface format / present mode.** Choose a **UNORM** surface format (prefer
`B8G8R8A8_UNORM`, else `R8G8B8A8_UNORM`, else the first supported) so the blit from
our linear-UNORM render target preserves values (no sRGB-encoding surprise);
present mode **`FIFO`** (vsync; universally supported). Colorspace
`SrgbNonlinear` (the only universally available).

**Swapchain.** Image count `min(minImageCount + 1, maxImageCount)`; usage includes
**`eTransferDst`** (blit destination); extent = the current GLFW framebuffer size;
pre-transform = current; opaque alpha.

**Renderer present path (extends M4).** Add
`Renderer::recordFrame(cmd, volume, params, dstImage, dstExtent)` that updates a
per-frame uniform buffer, dispatches the compute shader into an **internal storage
image sized to `dstExtent`** (lazily (re)created when the extent or volume view
changes), then records a **`vkCmdBlitImage`** from that storage image into
`dstImage`. The offscreen `render()` + readback path (M4) is unchanged (tests /
benchmark); the present path performs **no host readback**.

**Frame loop (one frame in flight).** Per frame, on the owner thread:
1. wait the in-flight fence; `acquireNextImageKHR` (signal `imageAvailable`);
2. record one command buffer: `recordFrame(...)` (dispatch → internal storage
   image, left in `eTransferSrcOptimal`); barrier acquired swapchain image
   `eUndefined → eTransferDstOptimal`; **blit** storage → swapchain; barrier
   swapchain `eTransferDstOptimal → ePresentSrcKHR`;
3. submit (wait `imageAvailable` at the transfer stage, signal `renderFinished`,
   signal the in-flight fence);
4. `presentKHR` (wait `renderFinished`).
Classic 1.0 barriers (D-0016). Frames-in-flight pipelining is deferred (Backlog).

**Resize / recreation.** On `eErrorOutOfDateKHR` / `eSuboptimalKHR` from
acquire/present, or a framebuffer-resize callback: `vkDeviceWaitIdle`, recreate the
swapchain (and the Renderer's internal storage image) at the new extent. A
zero-extent framebuffer (minimized) skips rendering until it is non-zero.

## Contract Specification
- Swapchain: UNORM format preferred; `FIFO`; images usage ⊇ `eTransferDst`; extent
  = current framebuffer size.
- The storage image is **blitted** into the swapchain image (same extent ⇒
  effectively a copy; blit allows future render-resolution ≠ window-size).
- Frame: acquire → record(dispatch + blit + layout transitions) → submit → present,
  synchronized by `imageAvailable` / `renderFinished` semaphores and an in-flight
  fence; **one** frame in flight.
- Recreation on out-of-date / suboptimal / resize, with device-idle first;
  zero-extent frames skipped. Single-threaded (ADR-0007).
- The present path does **no** host readback (distinct from `render()`).

## Consequences
- A simple, correct, vsync-capped interactive loop; pipelining (frames-in-flight)
  is deferred — acceptable for the ≥30 FPS contract (ADR-0019).
- Blit decouples render resolution from window size (enables dynamic-resolution
  later).
- `Renderer` gains a persistent, present-friendly path alongside the offscreen
  readback path.

## Alternatives Considered
- **Compute directly into the swapchain image** (swapchain as storage image):
  rejected — swapchain images are not reliably storage-capable; blit is portable.
- **`MAILBOX` present mode:** deferred — `FIFO` is universal; revisit for lower
  latency.
- **Frames-in-flight pipelining:** deferred (Backlog) — not needed to hit ≥30 FPS.

## Verification
- A `Viewer::runFrames(n)` mode renders **N frames then exits** on the display
  (available here): validation-clean across acquire / dispatch / blit / present and
  across a forced swapchain recreation (resize). Display-required, so skipped in a
  displayless CI. Teeth: drop the `→ ePresentSrcKHR` barrier → validation error;
  blit with mismatched regions → validation error.
- Determinism / correctness of the *rendered content* is already covered by the
  offscreen renderer tests (ADR-0012/0014) and the seam test (ADR-0015).
