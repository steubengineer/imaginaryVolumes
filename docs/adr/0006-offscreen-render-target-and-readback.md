# ADR-0006: Offscreen Render Target, Format & Host-Readback Convention

- **Status:** Accepted
- **Date:** 2026-06-18
- **Supersedes:** none

## Context
M2 renders headlessly (no swapchain). To verify rendering deterministically we
need an offscreen color image we can clear to a known color, copy to the host, and
inspect pixel-by-pixel. This ADR fixes the target's format, configuration, memory,
the GPU sequence, and — the binding part — the **host readback layout**, so tests
(and later milestones) can assert exact pixels. Binding/ownership per ADR-0004;
device per ADR-0005; concurrency per ADR-0007.

## Decision
**Format:** `vk::Format::eR8G8B8A8Unorm` — linear UNORM, 4 bytes/pixel, channel
order R, G, B, A. Chosen for **exact, deterministic integer readback** (no sRGB
encoding, no float rounding ambiguity). Later milestones may render at higher
internal precision, but the *readback target* is R8G8B8A8_UNORM unless a future
ADR adds options.

**Image:** `e2D`, extent `(width, height, 1)`, 1 mip, 1 array layer, `eOptimal`
tiling, `samples = e1`, usage `eColorAttachment | eTransferSrc | eTransferDst`
(color-attachment for M4 rendering; transfer-dst so `vkCmdClearColorImage` may
write it; transfer-src so we may copy it out). Initial layout `eUndefined`,
transitioned by synchronization2 barriers.

**Memory (M2):** the image is **device-local**; a separate **host-visible,
host-coherent staging buffer** of `width*height*4` bytes receives the readback.
Memory is allocated with raw `vkAllocateMemory` for M2's single image+buffer. (A
general allocator — VMA — is **deferred** to M3 when allocations proliferate;
Backlog B-0006.)

**GPU sequence (M2 clear→readback):** transition image `eUndefined→eTransferDst`;
`vkCmdClearColorImage` to the requested color; barrier `eTransferDst→eTransferSrc`;
`vkCmdCopyImageToBuffer` into the staging buffer (`bufferRowLength=0`,
`bufferImageHeight=0` ⇒ tightly packed); submit; **wait on a fence**; map and read.

**Host readback convention (binding):** the result is a tightly-packed, row-major
array of `width*height` pixels:
- pixel = 4 bytes in order **R, G, B, A**;
- rows ordered **top-to-bottom with origin at top-left** (Vulkan framebuffer
  convention);
- pixel stride 4 bytes, **row stride `width*4`** (tight);
- **byte offset of pixel (x, y) = `(y*width + x) * 4`**.
Exposed as an `ImageReadback` value carrying `width`, `height`, the byte span, and
`(x,y)`-indexed pixel access.

**Color/UNORM convention:** the clear color is four floats in `[0,1]`; a stored
byte is the standard Vulkan UNORM encoding of its component. To keep expectations
exact, **tests choose clear-color components of the form `k/255`**, so the expected
byte is exactly `k` (sidestepping tie-rounding).

## Contract Specification
- Readback: R8G8B8A8_UNORM, tightly packed, **top-left origin**, row-major,
  pixel stride 4, row stride `width*4`, pixel `(x,y)` at byte `(y*width+x)*4`,
  channels R,G,B,A.
- Image usage ⊇ `eColorAttachment | eTransferSrc | eTransferDst`; samples `e1`.
- Staging buffer is host-visible + host-coherent, `width*height*4` bytes.
- A clear to `(r,g,b,a)` with each component `= k/255` yields, for **every** pixel,
  bytes `(round(r·255), round(g·255), round(b·255), round(a·255)) = (k_r,k_g,k_b,k_a)`.
- Host reads occur only after the submission's fence is signaled (ADR-0007).
- Allocation/transfer failures map to `Errc` per ADR-0004.

## Consequences
- UNORM-linear gives bit-exact, implementation-independent verification.
- Raw allocation is fine for one image+buffer; VMA deferred to M3 (B-0006).
- Top-left origin is the Vulkan-native convention; M4's camera/coordinate ADR will
  relate world space to this image origin explicitly.

## Alternatives Considered
- **sRGB target (`eR8G8B8A8Srgb`):** rejected for M2 — encoding makes exact pixel
  assertions ambiguous.
- **Float target (`eR16G16B16A16Sfloat`/`32`):** rejected for M2 — unneeded for a
  clear, and float readback complicates exact comparison; revisit if M4 needs HDR
  readback.
- **Map the image directly (linear tiling):** rejected — `eOptimal` + staging copy
  is the portable, performant path; linear-tiled sampling is poorly supported.

## Verification
- Clear to a known color, e.g. `(64/255, 128/255, 192/255, 255/255)` → every pixel
  reads `(64,128,192,255)`; assert across all pixels (teeth: change the clear color
  or break the `(x,y)→offset` formula → red).
- Repeat with a second, distinct color to prove the value is not hardcoded.
- Re-running the clear+readback yields **identical** bytes (determinism, ADR-0007).
