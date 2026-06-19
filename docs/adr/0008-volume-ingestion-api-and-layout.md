# ADR-0008: Volume Ingestion API & Data-Layout Convention

- **Status:** Accepted
- **Date:** 2026-06-19
- **Supersedes:** none

## Context
M3 ingests the field a caller wants to plot: a flat array of complex samples
plus a grid size. This ADR fixes the **public input signature**, the
**memory-layout/indexing convention**, and the **shape-validation semantics** —
the host-facing half of M3. The GPU resource it produces is ADR-0009; the
optional range metadata it accepts is ADR-0010.

Founding decisions realized here: **D-0006** (flat layout is x-fastest, 0-based)
and the input half of **D-0004** (a single upload of a derived field). Error
semantics per **ADR-0003** (`Result<T>`/`Errc`, no exceptions, `IV_ASSERT`);
context/threading per **ADR-0005/0007**. Constraint: the project targets volumes
of several hundred voxels per side, and a ~1290³ field already has a linear
element count > 2³¹ — so **linear index/size arithmetic must be 64-bit**, while
per-axis dimensions stay `uint32` (Vulkan image extents are `uint32`).

## Decision
**Input.** Two precision overloads accept a contiguous, tightly-packed sequence
of complex samples plus dimensions:
- `std::span<const std::complex<float>>`
- `std::span<const std::complex<double>>`

**Dimensions / indexing.** A value type in namespace `iv`:
```cpp
struct GridDims {
  std::uint32_t nx{}, ny{}, nz{};
  constexpr std::size_t count() const noexcept;        // size_t(nx)*ny*nz
  constexpr std::size_t index(std::uint32_t x,
                              std::uint32_t y,
                              std::uint32_t z) const noexcept; // x-fastest, 0-based
};
```
`index(x,y,z) = size_t(x) + size_t(nx)*(size_t(y) + size_t(ny)*size_t(z))`
(x-fastest, 0-based; **D-0006**). All arithmetic in `count()`/`index()` is
64-bit (`std::size_t`).

**Entry point (M3).** A static factory on the GPU resource (ADR-0009),
mirroring M2's `Context::create` shape:
```cpp
iv::Result<iv::vk::Volume>
iv::vk::Volume::create(Context&, std::span<const std::complex<float>>,
                       GridDims, const VolumeOptions& = {});
iv::Result<iv::vk::Volume>
iv::vk::Volume::create(Context&, std::span<const std::complex<double>>,
                       GridDims, const VolumeOptions& = {});
```
`VolumeOptions` is ADR-0010.

**Validation** (host-side, before any GPU work; first failure wins):
1. `nx ≥ 1 && ny ≥ 1 && nz ≥ 1`, else `Errc::invalid_argument`.
2. `span.size() == dims.count()`, else `Errc::invalid_argument`.

Device-capability limits (e.g. `maxImageDimension3D`) and allocation are
ADR-0009's checks, applied after these.

**The layout convention is library-wide.** Any future flat-field API uses the
same x-fastest, 0-based ordering; `GridDims` is the one indexing authority.

## Contract Specification
- **Types:** `iv::GridDims{uint32 nx,ny,nz; size_t count(); size_t index(x,y,z);}`.
  `index` requires `x<nx && y<ny && z<nz` (precondition; `IV_DEBUG_ASSERT`); it
  is x-fastest, 0-based, 64-bit.
- **Signatures:** the two `Volume::create` overloads above; `T ∈ {float,double}`.
- **Invariants:** a successfully created Volume reports `dims()` equal to the
  requested dims with `dims().count() == span.size()`.
- **Ownership / lifetime:** the span is **borrowed for the duration of the call
  only**. Ingestion copies what it needs into a staging buffer; the library
  retains **no** reference to caller memory after `create` returns. Input is
  `const` and never mutated.
- **Concurrency:** called on the Context's owner thread (ADR-0007); Debug
  affinity check; not thread-safe.
- **Conventions:** x-fastest, 0-based indexing; `nx,ny,nz` are voxel counts
  (≥ 1); element counts and byte offsets are 64-bit.
- **Errors:** zero dimension or shape mismatch → `Errc::invalid_argument`;
  downstream device/allocation/Vulkan failures per ADR-0009/ADR-0004. No
  exceptions (ADR-0003). Out-of-range `index` is UB guarded by `IV_DEBUG_ASSERT`.

## Consequences
- The caller may free or reuse its array the instant `create` returns.
- 64-bit `count()`/`index()` prevent overflow for device-limited dimensions (a
  uint32 product would wrap well before `maxImageDimension3D` is reached).
- A single fixed convention removes per-call stride configuration; caller-
  supplied strides remain a future, supersedable option (D-0006 deferred).
- The GPU resource carries the ingestion entry point, so the data contract and
  the resource that embodies it stay in one place for M3 (no separate host
  mirror; D-0004 uploads once).

## Alternatives Considered
- **z-fastest / Fortran order:** rejected — D-0006; x-fastest matches 3D-texture
  row order on upload (ADR-0009), making the staging fill a tight copy.
- **Caller-specified arbitrary strides now:** rejected as premature; the flat-
  array contract needs none. Revisit on demand (would extend this ADR).
- **`size_t` dimensions:** rejected — Vulkan image extents are `uint32` and are
  bounded by `maxImageDimension3D`; only the *linear* arithmetic needs 64-bit,
  which `count()`/`index()` provide.
- **A standalone host `iv::Volume` distinct from the GPU resource:** rejected for
  M3 — the milestone's deliverable is the GPU-resident texture; a host mirror is
  unused (D-0004).

## Verification
- **Static:** `static_assert` pins the convention at compile time — for
  `GridDims{3,4,5}`: `count() == 60` and `index(1,2,3) == 43`
  (`1 + 3*(2 + 4*3)`).
- **Unit:** zero-dimension and shape-mismatch inputs return
  `Errc::invalid_argument`. Teeth (fault injection): remove the size check → a
  wrong-sized span is accepted (later diverges/over-reads) → red; restore →
  green.
- The indexing convention's behavioral teeth live in ADR-0009's round-trip: a
  transposed fill makes readback diverge.
