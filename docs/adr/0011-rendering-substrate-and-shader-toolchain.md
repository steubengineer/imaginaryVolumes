# ADR-0011: Rendering Substrate & Shader Toolchain

- **Status:** Accepted
- **Date:** 2026-06-19
- **Supersedes:** none

## Context
M4 must execute programmable GPU code to ray-march the M3 volume into an image,
and must turn GLSL into the SPIR-V the library consumes. This ADR fixes the
**execution substrate** (how the renderer runs and produces pixels) and the
**shader toolchain** (how SPIR-V is produced and shipped) — the infrastructure
the M4 rendering ADRs (0012–0014) build on.

Realizes/cites: **D-0008** (ray-marched DVR, verified headlessly first),
**ADR-0001** (build/dependency policy: GCC-only, offline, vendored-or-system,
new dep ⇒ ADR), **ADR-0004** (binding & `Unique<>` ownership), **ADR-0005**
(device + single graphics/compute-capable queue; no special features),
**ADR-0006** (`R8G8B8A8_UNORM` offscreen + staging readback), **ADR-0009**
(`Volume`: sampled 3D `R32G32Sfloat`, resting `eShaderReadOnlyOptimal`),
**D-0016** (no device feature beyond core). Two forks were decided with the
maintainer: **compute substrate** and **build-time `glslc` with embedded SPIR-V**
(journaled D-0021, D-0022).

## Decision
**Substrate — compute pipeline.** One ray per pixel via a compute shader
(local size `8×8`), dispatched `ceil(w/8)×ceil(h/8)`. The shader samples the
`Volume` (combined image sampler) and writes the composited result with
`imageStore` to a 2D **`R8G8B8A8_UNORM` storage image** (`rgba8` — a
storage-mandatory format, so no feature/extension or capability check). No render
pass, framebuffer, or vertex stage. The storage image (`usage =
eStorage | eTransferSrc`, device-local, `e2D`) is read back via the ADR-0006
staging pattern, reusing the shared memory helper (ADR-0009/D-0017) and the
`ImageReadback` layout.

**Shader toolchain — build-time `glslc`, embedded SPIR-V.** GLSL sources live
under `shaders/`. CMake locates `glslc` via `find_program`; **if absent,
configure fails** with a clear message (mirroring the GCC ≥ 13 check). Each
source is compiled to SPIR-V at build time and **embedded** into `libiv` as a
generated `std::byte`/`uint32_t` array (a CMake custom command + a small
generator emitting a `.cpp`), so the library is self-contained — no runtime file
lookup, path-independent tests. SPIR-V is reproducible from the GLSL with no
absolute paths baked in. **This extends ADR-0001's dependency policy**: `glslc`
(from the installed Vulkan SDK) joins CMake/GCC/Python as a **required build
tool** — a build tool, not a linked or runtime dependency; no build-time network.
Journaled D-0022; ADR-0001 is **not** superseded.

**Pipeline & descriptors (ADR-0004 ownership).** A compute `vk::Pipeline` +
`vk::PipelineLayout` + `vk::DescriptorSetLayout` (binding 0: combined image
sampler — the volume; binding 1: storage image — output; binding 2: a uniform
buffer of camera/transfer-function/colormap params, ADR-0012/0013/0014), a
`vk::DescriptorPool` + set, and a `vk::Sampler` (linear filter, clamp-to-edge —
the sampler deferred from M3), all owned via `Unique<>`. Submit + fence
(ADR-0007); no new device feature (D-0016 retained).

## Contract Specification
- Output is a 2D `R8G8B8A8_UNORM` image, one compute invocation per pixel,
  written via `imageStore`; read back with the ADR-0006 convention (top-left
  origin, row-major, pixel `(x,y)` at byte `(y*w+x)*4`, channels R,G,B,A). M4's
  public entry returns an `ImageReadback`.
- The volume is bound as a combined image sampler (linear, clamp-to-edge) and is
  read-only sampled input in `eShaderReadOnlyOptimal` (ADR-0009); the renderer
  transitions only the storage image's layouts.
- Build: `glslc` required at configure (fatal if missing). SPIR-V is generated
  from `shaders/*.comp` (and any `.vert/.frag`) and embedded; artifacts are
  reproducible and path-independent.
- Core Vulkan 1.3 only — no device feature/extension beyond ADR-0005.
- Shader-module / pipeline / descriptor / sampler creation failures map to `Errc`
  via ADR-0004; host reads occur only after the dispatch fence (ADR-0007).

## Consequences
- Compute avoids render-pass/framebuffer/vertex boilerplate and the
  render-pass-vs-dynamic-rendering choice (D-0016); the result blits cleanly to
  M5's swapchain.
- A dedicated storage image keeps M2's color target untouched (it lacks
  `eStorage`); one extra image.
- `glslc` becomes a hard build requirement — acceptable (SDK installed;
  documented, with a clear configure-time failure). Embedding keeps `libiv`
  self-contained and tests path-independent.
- `R8G8B8A8_UNORM` storage support is mandatory, so no runtime capability check.

## Alternatives Considered
- **Graphics pipeline (full-screen fragment shader):** rejected — more boilerplate
  and drags in the render-pass/dynamic-rendering decision; compute is the natural
  fit for per-pixel ray casting (maintainer choice, D-0021).
- **Vendored precompiled SPIR-V:** rejected (maintainer choice, D-0022) but
  remains the fallback if requiring `glslc` at build proves painful — would be a
  superseding ADR.
- **Runtime compilation via libshaderc:** rejected — a heavy runtime dependency.
- **Compute into M2's color target:** rejected — it lacks `eStorage`; widening its
  usage couples milestones. A dedicated storage image is cleaner.

## Verification
- **Pipeline/readback teeth:** a trivial compute shader that writes a constant
  color → readback equals that color across all pixels (proves
  shader-module/pipeline/descriptor/dispatch/readback end to end). Fault
  injection: change the constant → red. (This is the M4 analogue of M2's
  clear-and-readback teeth, now through a compute pipeline.)
- **Build-tool gate:** configuring with `glslc` hidden from `PATH` fails at
  configure with the ADR-0011 message (demonstrated like the GCC ≥ 13 check).
- Validation-clean across pipeline create / dispatch / teardown (pNext messenger
  gate, ADR-0005); ASan/UBSan green; LSan scoped (D-0015).
