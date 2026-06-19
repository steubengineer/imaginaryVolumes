# ADR-0009: GPU Volume Texture — Format, Derived Contents, Precision & Upload

- **Status:** Superseded by ADR-0015
- **Date:** 2026-06-19
- **Supersedes:** none

## Context
The ingested field (ADR-0008) must become a GPU-resident 3D texture the M4 ray-
marcher samples. This ADR fixes the **texture format and channel contents**, the
**precision policy**, and the **upload (and verification readback) mechanism**.

Founding/realized decisions: **D-0004** (store derived `(magnitude, phase)`;
transfer function in-shader — so raw, un-normalized magnitude is stored),
**D-0005** (derive in input precision, store fp32, no fp64 GPU path), **D-0006**
(x-fastest layout). Reuses M2 machinery and contracts: **ADR-0004** (binding,
`Unique<>` ownership, `vk::Result→Errc`), **ADR-0005** (device + single graphics
queue), **ADR-0006** (device-local image + host-visible staging + fence-gated
readback; raw allocation), **ADR-0007** (single-threaded, fence-gated host
reads), **D-0016** (classic core-1.0 barriers; no device feature), **D-0017**
(keep raw allocation; generalize `findMemoryType`; VMA deferred, B-0006).

## Decision
**Format.** `vk::Format::eR32G32Sfloat` (RG32F). **R = magnitude `abs(z)`**,
**G = phase `arg(z)`** in radians, principal value in `[−π, π]` (as `std::arg`
returns). Magnitude is stored **raw / un-normalized** (D-0004) so M4's
linear/log opacity toggle and colormap are pure shader uniforms with no re-
upload.

**Precision (D-0005).** For `complex<double>` input, `abs`/`arg` are computed in
`double`, then narrowed to `float` for storage; for `complex<float>`, computed in
`float`. The texture is always fp32; there is no fp64 GPU path. A zero sample
maps to `(0, 0)` (`abs(0) = 0`, `arg(0+0i) = 0`).

**Image.** `e3D`, extent `(nx, ny, nz)`, 1 mip, 1 array layer, `eOptimal`
tiling, `samples = e1`, usage `eSampled | eTransferDst | eTransferSrc`, initial
layout `eUndefined`, device-local memory. An image **view** (`e3D`, color
aspect, full subresource) is created for M4 sampling. The **sampler**
(filtering, addressing) is deferred to M4.

**Upload (staging → copy).** Allocate a host-visible|coherent staging buffer of
`dims.count() * 2 * sizeof(float)` bytes. Fill it on the host in x-fastest order:
texel `(x,y,z)` occupies floats `[idx*2, idx*2+1] = (magnitude, phase)` where
`idx = dims.index(x,y,z)`. Then: transition image `eUndefined → eTransferDst`;
`vkCmdCopyBufferToImage` with one region, `imageOffset {0,0,0}`,
`imageExtent {nx,ny,nz}`, `bufferRowLength = 0`, `bufferImageHeight = 0` (tightly
packed — which **matches** the x-fastest layout, D-0006); barrier
`eTransferDst → eShaderReadOnlyOptimal`; submit on the graphics queue; **wait on
a fence** before returning. Classic core-1.0 `vkCmdPipelineBarrier` (D-0016).

**Resting state.** After a successful `create`, the image is in
`eShaderReadOnlyOptimal`, ready for M4 sampling (no per-frame transition).

**Readback (verification/diagnostic).**
`Volume::readback() -> Result<VolumeReadback>` transitions
`eShaderReadOnlyOptimal → eTransferSrc`, `vkCmdCopyImageToBuffer` into a host-
visible staging buffer (tightly packed), fence-waits, maps, and **restores** the
image to `eShaderReadOnlyOptimal` (so the Volume stays usable). `VolumeReadback`
carries `dims` and `at(x,y,z) -> { float magnitude; float phase; }`, reading the
float pair at `idx*2` (same x-fastest layout). Host reads occur only after the
fence (ADR-0007).

**Ownership.** `Volume` is move-only (like `Context`): it owns
`Unique<vk::Image>`, `Unique<vk::DeviceMemory>`, `Unique<vk::ImageView>` and
**borrows** the Context's `vk::Device` for destruction. Declaration order
guarantees the view and image are destroyed **before** the backing memory is
freed (same load-bearing ordering as M2's `clearAndReadback`). A `Volume` must
not outlive the `Context` it was created from (lifetime precondition).

**Memory helper (D-0017).** M2's `findMemoryType` + allocate are generalized into
a small shared `iv::vk` memory utility used by both `clearAndReadback` and
`Volume`. Raw `vkAllocateMemory`; VMA stays deferred (B-0006).

## Contract Specification
- **Texel:** RG32F; `R = abs(z)`, `G = arg(z) ∈ [−π, π]`; raw magnitude (no
  device-side normalization).
- **Precision:** stored fp32; derived in input precision then narrowed (D-0005).
  The host→texel→readback path is **bit-exact**: a `TRANSFER` copy neither
  filters nor converts, so a readback texel equals the host-stored `float`
  exactly. Tests assert exact `float` equality against an expectation computed by
  the same input-precision-then-narrow path (no tolerance required).
- **Layout:** texel `(x,y,z)` ↔ staging float pair at `dims.index(x,y,z)*2`;
  copy is tightly packed (`bufferRowLength = bufferImageHeight = 0`).
- **Image:** type `e3D`, `samples e1`, usage ⊇
  `eSampled|eTransferDst|eTransferSrc`; resting layout `eShaderReadOnlyOptimal`.
- **Concurrency:** `create`/`readback` run on the Context's owner thread (Debug
  affinity check); host reads only after the submission's fence signals
  (ADR-0007); not thread-safe.
- **Lifetime:** move-only; borrows the Context device; must not outlive the
  Context (UB if violated — lifetime precondition, ADR-0003).
- **Errors:** dims exceeding `maxImageDimension3D` → `Errc::unsupported_configuration`;
  memory allocation failure → `Errc::allocation_failed`; other Vulkan failures
  mapped per ADR-0004. (ADR-0008's shape/zero-dim checks run first.)

## Consequences
- Storing raw `(magnitude, phase)` makes M4's opacity scaling + colormap pure
  shader uniforms — no re-upload on a linear/log toggle (D-0004).
- RG32F is 8 bytes/texel — a several-hundred³ field is hundreds of MB to a few
  GB (e.g. 512³ ≈ 1.07 GB), acceptable on the target discrete GPU; revisit
  compression only under measured pressure.
- RG32F preserves magnitude dynamic range (needed for log opacity) and yields
  exact, testable readback.
- Linear filtering of RG32F at sample time is an M4 concern (format-feature /
  sampler); the resource imposes none here.
- A persistent `Volume` introduces a device-borrowing lifetime contract absent
  from M2's transient `clearAndReadback`.

## Alternatives Considered
- **Upload raw complex `(re, im)` and derive `abs`/`arg` in-shader:** rejected —
  D-0004 (precompute once; keeps runtime toggles free; decouples the GPU
  representation from input precision).
- **16-bit/half or 8-bit-normalized storage:** rejected — loses magnitude
  dynamic range required for log opacity and breaks exact-readback teeth; revisit
  only under measured memory pressure.
- **fp64 storage / GPU double path:** rejected — D-0005, B-0004 (Vulkan cannot
  sample fp64 textures).
- **synchronization2 barriers / dynamic rendering:** rejected for M3 — D-0016
  (no device feature enabled); classic barriers suffice for a copy.
- **Leave the image in `eTransferDstOptimal`:** rejected — resting in
  `eShaderReadOnlyOptimal` is what M4 samples and avoids a per-frame transition.
- **Adopt VMA now:** rejected — D-0017 / B-0006.

## Verification
- **Round-trip teeth.** Upload a small analytic field (e.g. 3×2×2 / 4×4×4 with a
  distinct complex value per voxel — magnitudes spanning a range including a
  zero), read back, and assert each texel's `(magnitude, phase)` equals the
  host-derived fp32 expectation **exactly**. Teeth (fault injection): (a)
  transpose the fill order (z-fastest) → readback diverges → red; (b) swap R/G
  (phase into R) → red; (c) use `std::norm` (|z|²) for magnitude, or swap the
  `atan2` arguments for phase → red. Each reverted.
- **Precision teeth.** Each precision path round-trips **bit-exactly against an
  expectation computed in that same precision then narrowed** (float input vs a
  float-derived expectation; double input vs a double-derived expectation). The
  float and double paths are **not** required to agree: for some voxels they
  differ by ≤1 ULP — the double path, derived in `double` then narrowed, is the
  more accurate — and that very divergence evidences derivation in input precision
  (D-0005). Tooth (fault injection): derive after casting the input to `float`
  regardless of `T` → the double round-trip diverges from its double expectation →
  red. (See D-0020.)
- **Validation / sanitizers.** Create/upload/readback/teardown is validation-
  clean (pNext messenger gate, ADR-0005); ASan+UBSan green; LSan scoped per
  D-0015. The generalized memory helper is exercised by both M2 and M3 tests.
