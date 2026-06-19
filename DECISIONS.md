# DECISIONS.md — imaginaryVolumes

This is the decision journal (DEV_PROCESS §2.8): architecturally significant
choices and the rationale that drove them, plus the Backlog (roads not taken and
review concerns not yet addressed). It is lighter-weight than an ADR; choices
with public-contract impact (§1.1) *also* get an ADR, referenced here.

## Decision Log
(Newest first. The founding set, D-0001…D-0008, was all decided on 2026-06-18
during project initiation; D-0009…D-0010 were added during M1's CONTRACT phase
the same day. Future entries prepend above.)

### D-0036 — M6 Slug glyph rendering: integration specifics + headless-only scope
- **Date / milestone:** 2026-06-19 / M6 (IMPLEMENT, ADR-0023)
- **Choice:** How libharfbuzz-gpu's experimental Slug encoder/shaders bind to our
  Vulkan overlay, and how much of the path M6 ships.
- **Decision / findings:**
  - **Atlas format:** the Slug encoder emits **RGBA16I** texels (4×int16, 8 bytes;
    the source of the ±8000-unit coordinate range). The atlas is stored int16 and
    uploaded as an **R16G16B16A16_SINT uniform texel buffer**; the shader's
    `isamplerBuffer hb_gpu_atlas` auto-binds to **set 0 / binding 0**.
  - **Vendored GLSL → Vulkan:** the HB GLSL is OpenGL-3.3 style (its only uniform is
    the opaque atlas sampler). It compiles to Vulkan SPIR-V via our `glslc` with
    **`-fauto-bind-uniforms -fauto-map-locations`** and `-I third_party/harfbuzz/src`
    (`#include`d into our `shaders/glyph.frag`). Glyph outlines are encoded in **font
    units** (a separate upem-scaled `hb_font`) to satisfy the quantization.
  - **No dilation:** glyph quad corners are pre-projected to clip space (NDC) on the
    CPU and padded ~5% em, so we skip the HB half-pixel `hb_gpu_dilate` vertex helper;
    the vertex stage is a pass-through and the analytic Slug coverage gives the AA.
  - **Scope (deviation):** glyphs render on the **headless `render()` path** — the
    ADR-0023 verification path (coverage readback + zoom). The **present/viewer path
    draws lines/triangles only**; per-frame Slug glyph plumbing is **deferred to M7**
    (the annotation layer that decides the viewer's text). This narrows ADR-0021's
    "identical headless and windowed" for the glyph subset only — lines/triangles
    stay identical in both paths. **B-0010** tracks the present-path work.
  - **Glyph pipeline lives in core `iv`** (a graphics pipeline over an int texel
    buffer — no HarfBuzz link); only the *data* (atlas + quads) comes from `iv_text`
    (`appendText`). So the glyph shaders compile even with `IV_BUILD_TEXT=OFF`.
- **Rationale:** Confirmed the experimental Slug path works in Vulkan (no MSDF
  fallback needed, ADR-0023); headless rendering proves the technique and fully
  satisfies the ADR while keeping the renderer changes bounded.
- **Contract impact:** Implements ADR-0023; the headless-only scope is the recorded
  deviation (DEV_PROCESS §; cf. D-0030). MSDF fallback remains documented, unused.

### D-0035 — M6 HarfBuzz acquisition: concrete pin, vendored shape, bundled face
- **Date / milestone:** 2026-06-19 / M6 (IMPLEMENT, ADR-0022)
- **Choice:** The concrete facts ADR-0022 deferred "to acceptance": which commit,
  which files, which font face, and how the default font is delivered.
- **Decision:**
  - **Pin:** HarfBuzz `main` @ **`ac0979b6f44b41894c73fd208a0b4f5a8c6dc6ff`**
    (2026-06-18). A commit, not a tag, because `libharfbuzz-gpu` (the Slug renderer,
    D-0034) exists **only on `main`** — no tagged release ships it yet.
  - **Vendored shape:** a near-verbatim mirror of upstream `src/` (auditable by
    `diff -r`), excluding only non-compiled extras — `*.py` generators, the
    `wasm/`/`rust/`/`ms-use/` integration dirs, and the raw non-GLSL GPU shader
    sources. The kept set is exactly the transitive compile closure of
    `src/harfbuzz.cc` + `src/hb-gpu*.cc`, verified to build with our GCC at C++23.
    Dropping `ms-use/` also keeps the drop single-licensed (top-level Old MIT only).
  - **Build:** one static lib `harfbuzz_core` from the amalgamated `src/harfbuzz.cc`,
    **no `HAVE_*` defines** (built-in OpenType + UCD; no ICU/GLib/Cairo/FreeType),
    `SYSTEM` includes, not under `-Werror`. Gated by `IV_BUILD_TEXT` (default ON);
    `OFF` is the text-free isolation gate (mirrors `IV_BUILD_VIEWER`).
  - **Font:** bundle **`NewCM10-Book.otf`** (NCM 8.1.0, the GFL Roman "Book" weight —
    the package default; ~0.68 MB), **embedded** as a byte array
    (`tools/embed_bytes.cmake` → `iv::text::bundledFont()`) so the default face needs
    no runtime path. The canonical **GUST Font License** text (absent from the NCM
    package, which ships only the GPL3 text for its GPL subset) was fetched from the
    GUST e-foundry and bundled. The GPL3+FE+DE subset is excluded.
- **Rationale:** Pinning a `main` commit is the only way to get the experimental GPU
  API while staying reproducible (D-0033/D-0034). Embedding the face matches the
  project's no-runtime-asset convention (shaders, colormap LUT).
- **Contract impact:** Implements ADR-0022 (no new public contract beyond it).
- **Provenance recorded in:** `third_party/harfbuzz/VENDORING.md`,
  `third_party/fonts/README.md`.

### D-0034 — M6 GPU glyph rendering: libharfbuzz-gpu (Slug)
- **Date / milestone:** 2026-06-19 / M6 (CONTRACT) — maintainer decision
- **Choice:** How shaped glyphs are rendered crisply on the GPU.
- **Decision:** Use HarfBuzz's experimental **`libharfbuzz-gpu`** — it encodes glyph
  outlines for GPU rasterization via the **Slug algorithm** (patent dedicated to the
  public domain) and ships **GLSL** shaders, compiled through our `glslc`→SPIR-V→embed
  toolchain (ADR-0011/D-0022). Glyph encodings cached per `(font, glyphId)`; drawn as
  quads in the overlay pass (ADR-0021), alpha-blended; resolution-independent.
  Experimental risk mitigated by the pinned vendored commit (D-0033); **MSDF
  (msdfgen-style) is the documented fallback**.
- **Rationale:** True outline coverage → publication-quality at any zoom; reuses the
  font project's own GPU path, matched to our shaper. Corrects the author's earlier
  framing — Slug is not "part of HarfBuzz" historically, but `libharfbuzz-gpu` now
  provides exactly this (confirmed from the HarfBuzz README, 2026-06-19).
- **Contract impact:** ADR-0023 (Proposed).
- **Deferred alternatives:** MSDF atlas (fallback); Glyphy (deprecated upstream);
  CPU rasterization (not resolution-independent).

### D-0033 — M6 windowing of text: vendor HarfBuzz (pinned) for Unicode shaping
- **Date / milestone:** 2026-06-19 / M6 (CONTRACT) — maintainer decision
- **Choice:** How to acquire the shaper (new dependency, §1.1) and bound its surface.
- **Decision:** **Vendor HarfBuzz** into `third_party/` at a **pinned commit**
  (deliberately unlike GLFW's system path, D-0026 — pinning freezes the experimental
  `libharfbuzz-gpu` API, D-0034). Build minimal (no ICU/GLib/Cairo/FreeType; `hb-draw`
  supplies outlines), `SYSTEM` includes, **not** under `-Werror`. Wrap behind
  `iv::text::Shaper` (UTF-8 + font + size → positioned glyphs); no HarfBuzz type in
  `iv` public headers (ADR-0004). Default font: **New Computer Modern** (the CM/TeX
  look + a future math face), bundling **GFL-licensed faces only** (vetted from NCM
  8.1.0; the GPL3+FE+DE subset — `NewCM10-Regular`, `NewCMUncial*`, `*Devanagari` —
  is excluded). **LaTeX deferred**; M6 labels are text/Unicode.
- **Rationale:** Industry-standard shaping + the outline source for D-0034; vendoring
  pins the experimental GPU API and keeps builds reproducible.
- **Contract impact:** ADR-0022 (Proposed). License (HarfBuzz "Old MIT" + font)
  recorded at acceptance per §1.1.
- **Deferred alternatives:** system `find_package`; FreeType; stb_truetype/bitmap
  fonts (no real shaping); **LaTeX math rendering (deferred to a later milestone)**.

### D-0032 — M6 annotation/overlay substrate: a graphics pass over the compute output
- **Date / milestone:** 2026-06-19 / M6 (CONTRACT)
- **Choice:** How 2D/3D annotations (lines, glyph quads, legend) reach the image.
- **Decision:** Add the project's **first graphics pipeline**: the volume render
  target also gets `eColorAttachment`; after the compute volume pass, a vertex+fragment
  pipeline draws the overlay **into the same image** in a **classic `VkRenderPass`**
  (`loadOp = eLoad`, standard alpha blend). Primitives: lines + (textured/encoded)
  quads; 3D points projected with the ADR-0012 camera, 2D elements in screen space.
  Identical headless (`render()` → readback) and windowed (`recordFrame` → blit);
  classic 1.0 barriers (D-0016). M6 proves it with a test line + glyph quad; box/axes/
  legend are M7.
- **Rationale:** Lines + resolution-independent glyph coverage want rasterization/blend
  hardware; drawing into the volume image keeps compositing and downstream readback/
  blit trivial; classic render pass adds no device-feature toggle.
- **Contract impact:** ADR-0021 (Proposed).
- **Deferred alternatives:** compositing in compute; a separate overlay attachment;
  `VK_KHR_dynamic_rendering` (to cut boilerplate later).

### D-0031 — M6 opacity correction for ray step spacing (B-0008)
- **Date / milestone:** 2026-06-19 / M6 (CONTRACT)
- **Choice:** Make displayed density independent of the sampling rate.
- **Decision:** Correct per-sample opacity to `α = 1 − (1 − a)^(dt / dt_ref)` (`a` per
  ADR-0013), with `dt_ref = 1 / kReferenceSteps`, `kReferenceSteps = 256` documented.
  Composite + early termination unchanged; resolves B-0008 / the D-0030 finding.
- **Rationale:** Density must be a property of the field + transfer function, not of an
  arbitrary `stepCount`; also makes accumulation path-length-aware (physically
  correct) and is a prerequisite for the M7 colorbar to be meaningful.
- **Contract impact:** ADR-0020 (Proposed; extends ADR-0013).
- **Deferred alternatives:** pre-integrated transfer functions; exposing `dt_ref` as a
  public knob.

### D-0030 — M5 benchmark result + teeth: early-ray termination caps cost (stepCount-insensitive)
- **Date / milestone:** 2026-06-19 / M5 (IMPLEMENT)
- **Result:** The ADR-0019 contract is **met**: median **15.3 ms (~65 FPS)** for one
  512³ → 1280×720 `render()` on the RTX 4070 (Release, validation off); min 12.8,
  max 21.4 ms over N=30.
- **Finding:** ADR-0019's literal teeth — "8× the default `stepCount` → median >
  33.3 ms" — does **not** bite: 8× left the median ≈ 13 ms unchanged. Cause: the
  per-sample opacity (ADR-0013) is **independent of the step spacing `dt`**, and
  early-ray termination breaks at accumulated α ≥ `alphaTermination`. So any ray
  with nonzero opacity saturates after a roughly fixed *number of steps* regardless
  of N — raising `stepCount` only refines `dt`, adding no iterations before
  termination. Only (measure-zero) exactly-zero-opacity rays run all N. Hence render
  cost is insensitive to `stepCount` alone.
- **Decision:** Demonstrate the benchmark's teeth by **disabling early-ray
  termination** (`iv_bench --no-early-term`, `alphaTermination` set unreachable) so
  every ray marches all `stepCount` samples; then `--no-early-term --step-mult 8`
  takes the median **11.7 ms → 34.3 ms (29.2 FPS) → assertion FAIL (red)**. This
  faithfully shows the benchmark constrains the per-ray march budget; the default
  contract (early-term on) passes. (The `--frames`/`--step-mult`/`--no-early-term`/
  `--advisory` knobs live in `tools/bench.cpp`.)
- **Contract link:** ADR-0019 — the bound itself is unchanged and met; only its
  Verification "8× stepCount" recipe is refined (needs `--no-early-term`). The
  underlying `dt`-independent opacity is logged as **B-0008**.

### D-0029 — M5 performance contract: ≥30 FPS @ 512³, 720p, RTX 4070-class
- **Date / milestone:** 2026-06-19 / M5 (CONTRACT) — maintainer decision
- **Choice:** The operating point that defines "interactive framerates for several
  hundred voxels per side", and how to enforce it.
- **Decision:** Contract = median ≤ 33.3 ms (≥30 FPS) for one 512³ → 1280×720
  `Renderer::render()` on an RTX 4070-class GPU. Enforced by a headless, **opt-in**
  benchmark (`iv_bench` / `[!benchmark]`): warm-up + N≥30 timed renders, assert the
  median bound. Excluded from the default `ctest` (hardware-dependent).
- **Rationale:** Turns the founding goal into a checkable number + regression
  guard; headless render-time is a clean, conservative proxy for the vsync-capped
  windowed present.
- **Contract impact:** ADR-0019 (Proposed).
- **Deferred alternatives:** GPU-timestamp timing; a default perf gate (flaky
  across hardware).

### D-0028 — M5 interaction: OrbitCamera (host) + Viewer (orbit/zoom/keys)
- **Date / milestone:** 2026-06-19 / M5 (CONTRACT)
- **Choice:** The camera-control model and public viewer API.
- **Decision:** `iv::OrbitCamera` (pure host: target/distance/yaw/pitch, clamped
  pitch + distance, `eye()` per ADR-0012) drives `RenderParams`; `iv::vk::Viewer`
  maps left-drag→orbit, scroll→zoom, keys (Esc/L/C/R), with `run()` /
  `runFrames(n)`. Pan/roll/trackball deferred.
- **Rationale:** Orbit+zoom suffices to inspect a volume; the camera math is
  unit-testable without a window; toggles exercise M4 live.
- **Contract impact:** ADR-0018 (Proposed). Realizes D-0001 (thin viewer).
- **Deferred alternatives:** trackball/arcball; configurable bindings; pan.

### D-0027 — M5 swapchain & present: UNORM/FIFO, blit compute output, 1 frame in flight
- **Date / milestone:** 2026-06-19 / M5 (CONTRACT)
- **Choice:** How M4's compute-rendered image reaches the screen, and the present
  loop's format/sync/resize policy.
- **Decision:** UNORM surface format (prefer `B8G8R8A8_UNORM`) + `FIFO`; swapchain
  images `eTransferDst`; per frame acquire → `Renderer::recordFrame` (dispatch to an
  internal storage image) → **blit** into the swapchain image → present, with
  `imageAvailable`/`renderFinished` semaphores + an in-flight fence (**one** frame
  in flight); recreate on out-of-date/suboptimal/resize (device-idle first). The
  offscreen `render()`+readback path is unchanged. Classic 1.0 barriers (D-0016).
- **Rationale:** A compute pipeline can't draw to the swapchain (D-0021), so blit;
  FIFO is universal; single-frame-in-flight is simple/correct and ample for ≥30 FPS.
- **Contract impact:** ADR-0017 (Proposed).
- **Deferred alternatives:** MAILBOX; frames-in-flight pipelining; dynamic
  render-resolution (blit already enables it).

### D-0026 — M5 windowing: GLFW via system find_package; presentation-capable Context
- **Date / milestone:** 2026-06-19 / M5 (CONTRACT) — maintainer decision
- **Choice:** How to acquire GLFW (the new windowing dependency, D-0002), and how
  presentation support reaches our Vulkan setup.
- **Decision:** GLFW via **system `find_package(glfw3)`** (maintainer installed
  `libglfw3-dev`), used **only** by a separate `iv_viewer` target gated by
  `IV_BUILD_VIEWER`; the core `iv` stays GLFW-free (D-0001). `iv::vk::Context` gains
  an opt-in **presentation mode** (extends ADR-0005): enable GLFW-required instance
  extensions + `VK_KHR_swapchain`; require the graphics queue family to also
  present (verified vs the surface). Separate present queue deferred.
- **Rationale:** Reuses Context (no duplication); keeps the headless core/tests
  GLFW-free; system find_package matches ADR-0001 and is simplest.
- **Contract impact:** ADR-0016 (Proposed); new dependency per ADR-0001 §1.1;
  extends ADR-0005.
- **Deferred alternatives:** vendoring GLFW (submodule); SDL3 / direct xcb-Wayland
  (B-0001/B-0002); separate present queue.

### D-0025 — Volume stores (re, im); magnitude/phase derived in-shader (supersedes ADR-0009)
- **Date / milestone:** 2026-06-19 / post-M4 (defect fix, CONTRACT)
- **Choice / finding:** M4 rendering exposed a phase-seam artifact — the GPU
  linearly interpolates the stored phase *angle*, and interpolating across the ±π
  branch cut averages +π/−π to ≈0, painting a thin wrong-color seam on the
  negative-real axis (cyan in HSV, dark in twilight). Confirmed by a face-on
  phase-wheel **linear-vs-nearest A/B** (linear → seam; nearest → clean).
- **Decision:** Store the **raw complex value** (R=Re, G=Im) instead of
  `(magnitude, phase)`, and derive magnitude/phase **per-sample in the shader**.
  Interpolating the continuous complex value is correct across the cut. Supersedes
  **ADR-0009**; **revises D-0004** (store derived) and **D-0005** (derive in input
  precision).
- **Conversion point (double input):** each voxel's `re`/`im` is narrowed
  `double → float` **on the host in `deriveField`** when written to the fp32
  staging buffer; magnitude/phase are derived **in-shader in fp32**; the magnitude
  range (ADR-0010) is still computed **on the host in input precision** then
  narrowed.
- **Rationale:** correctness (eliminates the seam); also simplifies the M3 upload
  (store the value directly, no host abs/arg for storage); physically-correct
  reconstruction of a sampled complex field.
- **Contract impact:** ADR-0015 (Proposed → supersedes ADR-0009).
  `VolumeReadback::Texel` changes `{magnitude, phase}` → `{re, im}`. ADR-0013/0014
  contracts unchanged (they consume shader-derived magnitude/phase).
- **Deferred alternatives:** nearest sampling; manual cos/sin trilinear; a
  `(magnitude, cosθ, sinθ)` RGB32F layout — all rejected in ADR-0015.

### D-0024 — PNG demo export via an owned minimal encoder (no new dependency)
- **Date / milestone:** 2026-06-19 / post-M4 (tooling)
- **Choice:** How to save rendered frames as PNG for demos — add a PNG library
  (libpng / lodepng / stb_image_write) vs. write the encoder ourselves.
- **Decision:** Hand-roll a minimal RGBA8 PNG writer (uncompressed DEFLATE
  "stored" blocks; CRC-32 + Adler-32) in `examples/png.hpp`, used by
  `examples/render_demo`. No new dependency; files are larger than
  zlib-compressed PNGs but valid and viewable anywhere. Demo images go to a
  gitignored `gallery/` (regenerable, not committed).
- **Rationale:** Matches the minimal-deps / own-the-boilerplate stance; avoids a
  dependency ADR (§1.1) for example-only tooling; the encoder is ~150 lines and
  self-contained.
- **Contract impact:** none — examples are not part of `libiv` and not on the test
  gate; no public-contract or dependency change (hence no ADR).
- **Deferred alternatives:** a real (zlib-compressed) PNG or stb_image_write if
  demo image size ever matters — would then warrant a dependency decision.

### D-0023 — M4 coordinate frame & DVR convention (RH, +Y up, unit-cube = texcoord)
- **Date / milestone:** 2026-06-19 / M4 (CONTRACT)
- **Choice:** The §5 conventions for the renderer: handedness/up-axis, where the
  volume lives, how rays are generated, and the compositing/march model.
- **Decision:** Right-handed world, **+Y up**; the volume is the unit cube
  `[0,1]³` and a world position **is** the normalized 3D texture coordinate;
  rendered image origin **top-left** (matches ADR-0006). Fixed pinhole camera
  (eye/target/up/vfov/aspect) passed as `eye + topLeftDir + horizontal + vertical`
  spanning vectors; **front-to-back `over`** compositing with a **fixed
  `stepCount`** (no opacity correction in M4) and early-ray termination.
- **Rationale:** World = texcoord makes sampling trivial and ties rendering to the
  M3 x-fastest layout (a wrong axis is visible); fixed steps are deterministic and
  exactly testable; front-to-back enables early-out (a perf lever for M5).
- **Contract impact:** ADR-0012 (Proposed). Realizes D-0008.
- **Deferred alternatives:** model matrix / non-unit volume, opacity (step-size)
  correction, adaptive/jittered sampling — all deferred (would refine ADR-0012).

### D-0022 — Shader toolchain: build-time glslc → embedded SPIR-V (extends ADR-0001)
- **Date / milestone:** 2026-06-19 / M4 (CONTRACT) — maintainer decision
- **Choice:** How GLSL becomes the SPIR-V the library uses: build-time `glslc`
  (embedded) vs vendored precompiled `.spv` vs runtime `libshaderc`.
- **Decision:** Compile `shaders/*.{comp,vert,frag}` with `glslc` (found via CMake
  `find_program`; **fatal at configure if missing**, like the GCC≥13 check) and
  **embed** the SPIR-V into `libiv` as a generated array. This **extends
  ADR-0001's dependency policy**: `glslc` (Vulkan SDK) is a required **build
  tool** — not a linked/runtime dependency; no build-time network. ADR-0001 is
  **not** superseded.
- **Rationale:** Shaders always match source; the library stays self-contained and
  tests are path-independent; the SDK is installed. Reproducible/offline.
- **Contract impact:** ADR-0011 (Proposed); amends ADR-0001 §dependency policy
  (journaled here, not a supersession).
- **Deferred alternatives:** vendored precompiled SPIR-V (the fallback if requiring
  `glslc` at build becomes painful) — would be a superseding build ADR.

### D-0021 — M4 rendering substrate: compute shader → R8G8B8A8 storage image
- **Date / milestone:** 2026-06-19 / M4 (CONTRACT) — maintainer decision
- **Choice:** Compute pipeline (storage image) vs graphics pipeline (fragment
  shader into the M2 color attachment) for the ray-marcher.
- **Decision:** A **compute** pipeline casts one ray per pixel and `imageStore`s to
  a dedicated 2D **`R8G8B8A8_UNORM` storage image** (a storage-mandatory format),
  read back via the ADR-0006 staging path. No render pass / framebuffer / vertex
  stage; no new device feature (D-0016 retained).
- **Rationale:** Compute is the natural fit for per-pixel ray casting, avoids
  render-pass boilerplate and the dynamic-rendering choice, and blits cleanly to
  M5's swapchain; a dedicated storage image leaves M2's target untouched.
- **Contract impact:** ADR-0011 (Proposed). Realizes D-0008.
- **Deferred alternatives:** graphics-pipeline DVR (rejected; more boilerplate).

### D-0020 — Precision paths are not bit-identical; each is bit-exact to its own precision
- **Date / milestone:** 2026-06-19 / M3 (IMPLEMENT/VERIFY)
- **Choice / finding:** ADR-0009's *Verification* narrative claimed the `float`
  and `double` input paths produce identical fp32 texels. Implementation testing
  disproved it: for some voxels `arg` (atan2) computed in `double` then narrowed
  differs from the `float` computation by 1 ULP — e.g. `z = (12, -5)`:
  `-0.394791126f` (double) vs `-0.394791096f` (float).
- **Decision:** This divergence is correct and intended (D-0005): derive in input
  precision, then narrow; the double path is the more accurate. The **binding
  Contract Specification** of ADR-0009 (bit-exact round-trip vs a *same*-precision-
  then-narrow expectation) is unchanged and holds. Correct only ADR-0009's
  *Verification* narrative (drop the "identical fp32 texels" claim); each test now
  asserts a path against its own-precision expectation, and the float/double
  divergence is itself evidence of input-precision derivation.
- **Contract impact:** none to ADR-0009's Contract Specification; refines its
  Verification narrative (journaled here, per the D-0016 precedent of refining an
  ADR's narrative without touching its binding contract).
- **Deferred alternatives:** none.

### D-0019 — Magnitude-range metadata: exclude zeros from min, allow caller override
- **Date / milestone:** 2026-06-19 / M3 (CONTRACT)
- **Choice:** What magnitude statistics to expose for M4's opacity normalization,
  and whether the caller can pin them.
- **Decision:** During the ingestion host pass, compute `max` (greatest `abs`)
  and `minPositive` (least *strictly-positive* `abs`; `0` if the field is all-
  zero), in input precision then narrowed to fp32. Expose `magnitudeRange()`
  (override-if-given else auto) and `autoMagnitudeRange()` (always auto). A
  caller may override via `VolumeOptions::magnitudeRange` (validated
  `minPositive>=0 && max>=minPositive`). The normalization *formula* (linear/log,
  degenerate handling) stays in M4.
- **Rationale:** The host already visits every sample (D-0004/0009), so the range
  is free; log opacity needs a positive floor, so zeros are excluded from
  `minPositive`; an override pins normalization across animation frames/series.
- **Contract impact:** ADR-0010 (Proposed). Range is metadata, not stored in the
  texture (keeps raw magnitude per D-0004).
- **Deferred alternatives:** GPU-side reduction; mean/percentile stats — not
  needed for the M4 contract.

### D-0018 — M3 ingestion API & GPU-upload contract (realizes D-0004/0005/0006)
- **Date / milestone:** 2026-06-19 / M3 (CONTRACT)
- **Choice:** Concrete shape of the public ingestion API and the GPU upload that
  realizes the founding field decisions (D-0004 derived storage, D-0005
  precision, D-0006 x-fastest layout).
- **Decision:** Input is `std::span<const std::complex<float|double>>` +
  `iv::GridDims{nx,ny,nz}` (x-fastest 0-based `index`/`count`, 64-bit arithmetic);
  entry is `iv::vk::Volume::create(Context&, span, dims, VolumeOptions)`. The span
  is borrowed for the call only. Upload derives `(abs, arg)` in input precision →
  narrowed fp32, fills a tightly-packed staging buffer in x-fastest order, and
  `copyBufferToImage` into a device-local 3D **RG32F** image (R=magnitude raw,
  G=phase rad), resting in `eShaderReadOnlyOptimal` with a sampling view (sampler
  → M4). A move-only `Volume` owns image/memory/view and borrows the Context
  device; round-trip readback is **bit-exact** (TRANSFER copy, no filter/convert).
  Classic 1.0 barriers (D-0016); raw allocation via a generalized `findMemoryType`
  helper (D-0017).
- **Rationale:** Mirrors M2's `create`/RAII/staging-fence patterns; bit-exact
  RG32F readback gives strong teeth (transposed fill, R/G swap, `norm`-vs-`abs`,
  `atan2`-arg-swap all go red); raw magnitude keeps M4's linear/log toggle free.
- **Contract impact:** ADR-0008 (ingestion API + layout, Proposed), ADR-0009
  (texture/precision/upload, Proposed). Closes the "ADR pending in M3" notes on
  D-0004/0005/0006.
- **Deferred alternatives:** caller-specified strides; half/8-bit storage; VMA
  (B-0006) — all rejected for M3 in the ADRs.

### D-0017 — M3 keeps raw allocation; VMA deferred
- **Date / milestone:** 2026-06-19 / M3 (CONTRACT)
- **Choice:** Adopt the Vulkan Memory Allocator (VMA) now, or keep hand-rolled
  `vkAllocateMemory` for M3?
- **Decision:** Keep raw allocation for M3; generalize M2's
  `findMemoryType`/allocate helper into a small shared utility. Defer VMA.
- **Rationale:** M3's needs are modest (one 3D image + a staging buffer); matches
  the project's minimal-deps / own-the-boilerplate stance; no new dependency now.
- **Contract impact:** none (no new dependency). Adopting VMA later requires a
  dependency ADR (§1.1).
- **Deferred alternatives:** VMA stays open at Backlog B-0006 — revisit at M4/M5
  when images, uniforms, and buffers proliferate.

### D-0016 — M2 uses core 1.0 pipeline barriers (no synchronization2 feature)
- **Date / milestone:** 2026-06-19 / M2 (IMPLEMENT)
- **Choice:** ADR-0006's narrative mentioned synchronization2 barriers, but
  `vkCmdPipelineBarrier2` requires enabling the `synchronization2` device feature,
  which conflicts with ADR-0005's "no special features." Use sync2 (enable the
  feature) or classic core-1.0 `vkCmdPipelineBarrier`?
- **Decision:** Use classic core-1.0 `vkCmdPipelineBarrier` for M2's layout
  transitions. No device feature/extension is enabled.
- **Rationale:** Neither ADR-0005 nor ADR-0006's *binding* Contract Specification
  mandates sync2 (0006's binding contract is the readback layout/usage/color;
  "sync2" was narrative). Classic barriers are correct and keep ADR-0005's "no
  features" intent. Avoids a feature-chain in device creation for no M2 benefit.
- **Contract impact:** none to either ADR's binding contract; refines ADR-0006's
  narrative. Journaled here. sync2 can be adopted later under an ADR if a
  milestone needs it.
- **Deferred alternatives:** synchronization2 (and dynamic rendering) when M4's
  pipeline work makes them worthwhile.

### D-0015 — LSan scoping for third-party Vulkan loader/driver leaks
- **Date / milestone:** 2026-06-19 / M2 (IMPLEMENT)
- **Choice:** How to keep §7's "sanitizers clean" gate meaningful when the Vulkan
  loader + validation layer make ~240 process-exit allocations that never free
  and do not symbolize (identical on NVIDIA and llvmpipe; routed through our
  `Context::create()` only because it first initializes the loader). Options:
  blanket-disable LSan; suppression file; or scope LSan off for Vulkan tests only.
- **Decision:** The full ASan+UBSan run sets `ASAN_OPTIONS=detect_leaks=0` (UAF,
  overflow, and UBSan stay active); a second CTest entry leak-checks the
  non-Vulkan suite (`~[vk]`) with `detect_leaks=1`; and the **validation layer is
  the authoritative Vulkan-object-leak gate** — strengthened with a pNext
  create/destroy messenger so undestroyed objects are caught at vkDestroyInstance.
- **Rationale:** A suppression file can't match the unsymbolized driver frames;
  blanket-disabling loses coverage on our code. This keeps real leak coverage on
  our code and a Vulkan-native gate on Vulkan objects, without rationalizing a
  benign leak in code we own (§8.8).
- **Contract impact:** refines how ADR-0001's sanitizer gate (§7) is run; no
  consumer-facing contract change. Journaled here; supersede ADR-0001 only if we
  decide to make this a formal gate-policy contract.
- **Deferred alternatives:** revisit a suppression file if a future driver
  symbolizes its leaks.

### D-0014 — Concurrency baseline: single-threaded, not thread-safe
- **Date / milestone:** 2026-06-18 / M2 (CONTRACT)
- **Choice:** Thread-safe public API now vs. a single-threaded baseline vs.
  leaving concurrency unstated.
- **Decision:** Public API is single-threaded and not thread-safe; no internal
  threads in M2; Debug thread-affinity asserts; host reads gated on fences; the
  atomic assert handler is the sole cross-thread exception. Output is bitwise
  deterministic per device.
- **Rationale:** §6 requires an explicit declaration; races are made
  unrepresentable by sharing nothing; locking would be premature cost.
- **Contract impact:** ADR-0007 (Proposed).
- **Deferred alternatives:** future async/multi-thread work gets its own ADR.

### D-0013 — Offscreen target format & host-readback convention
- **Date / milestone:** 2026-06-18 / M2 (CONTRACT)
- **Choice:** Readback target format and pixel layout (UNORM vs sRGB vs float;
  origin/packing).
- **Decision:** `R8G8B8A8_UNORM`, tightly packed, top-left origin, row-major,
  pixel `(x,y)` at byte `(y*w+x)*4`, channels R,G,B,A; image
  `eColorAttachment|eTransferSrc|eTransferDst`; staging buffer for readback.
- **Rationale:** UNORM-linear gives bit-exact, implementation-independent pixel
  verification (real teeth); top-left is Vulkan-native.
- **Contract impact:** ADR-0006 (Proposed).
- **Deferred alternatives:** float/HDR readback if M4 needs it; → Backlog B-0006
  (VMA allocator).

### D-0012 — Instance, physical-device & queue selection
- **Date / milestone:** 2026-06-18 / M2 (CONTRACT)
- **Choice:** Device requirements and selection strategy; validation policy;
  API baseline.
- **Decision:** Vulkan 1.3 baseline; rank discrete>integrated>virtual>cpu (accept
  software — only llvmpipe exists here), require a graphics queue family,
  `IV_VULKAN_DEVICE_INDEX` override; validation layer + debug messenger in Debug
  (best-effort), captured so tests can assert cleanliness; one graphics queue.
- **Rationale:** Must run on the software-only dev host yet prefer real GPUs;
  capturing validation messages makes "validation-clean" testable.
- **Contract impact:** ADR-0005 (Proposed).
- **Deferred alternatives:** dedicated transfer/compute queue later if needed.

### D-0011 — Vulkan binding & object-ownership model
- **Date / milestone:** 2026-06-18 / M2 (CONTRACT)
- **Choice:** C API + own RAII vs. Vulkan-Hpp (no-exceptions) + own RAII vs.
  vk::raii. (Maintainer decision.)
- **Decision:** Vulkan-Hpp (`vulkan.hpp`) with `VULKAN_HPP_NO_EXCEPTIONS`,
  result-returning, wrapped in our own move-only single-owner RAII types; default
  dispatch; a boundary helper maps `vk::Result`→`iv::Errc`.
- **Rationale:** Type-safe and exception-free (fits ADR-0003); explicit, auditable
  ownership; vk::raii's exception-coupled ctors conflict with ADR-0003.
- **Contract impact:** ADR-0004 (Proposed).
- **Deferred alternatives:** dynamic dispatcher if a future extension needs it.

### D-0010 — Build/toolchain/dependency policy
- **Date / milestone:** 2026-06-18 / M1 (CONTRACT)
- **Choice:** Warning strictness, build configs, sanitizer wiring, and how third-
  party deps are acquired (C++23/system-GCC were maintainer-fixed givens).
- **Decision:** CMake (≥3.25), ISO C++23 (extensions off), GCC≥13; a strict
  mandatory warning set with `-Werror`; Debug/Release with `IV_ASSERT` always on;
  ASan+UBSan gate; deps via `find_package` (system) or vendored+pinned (small
  libs), no build-time network; new dep ⇒ ADR.
- **Rationale:** Satisfies §7 gates; vendoring keeps builds offline/reproducible;
  strict warnings surface conversions at the Vulkan boundary; CMake is the
  least-friction path to `find_package(Vulkan)`/GLFW.
- **Contract impact:** ADR-0001 (Proposed).
- **Deferred alternatives:** Clang/CI-matrix and FetchContent-for-all — not
  backlogged (supersedable later if multi-compiler CI is wanted).

### D-0009 — Error & failure model: std::expected + always-on IV_ASSERT, no exceptions
- **Date / milestone:** 2026-06-18 / M1 (CONTRACT)
- **Choice:** Exceptions vs. `std::expected` for recoverable failures; how to keep
  boundary contracts alive in Release; the assertion mechanism.
- **Decision:** Recoverable failures return `std::expected<T, iv::Error>`; no
  exceptions as a control channel (only `std::bad_alloc` may propagate, fatal);
  contract violations abort via an always-on, overridable `IV_ASSERT` (so
  precondition misuse is defined-abort, not UB, and is unit-testable).
- **Rationale:** Exception-free propagation suits the GPU/perf hot paths and is
  C++23-native; `assert()` would vanish under `-DNDEBUG`, so a custom always-on
  macro is required; an overridable handler makes abort paths testable without
  death tests.
- **Contract impact:** ADR-0003 (Proposed).
- **Deferred alternatives:** none material.

### D-0001 — Viewer model: offscreen core + optional interactive viewer
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M2 & M5
- **Choice:** Offscreen-only, library-owns-window, or both.
- **Decision:** Both — the offscreen renderer is the core; a thin GLFW-based
  interactive viewer layers on top.
- **Rationale:** Offscreen rendering is deterministic and embeddable and gives
  contract tests real teeth via pixel readback (§2.4); a separate viewer keeps
  the windowing dependency out of the core and off the test path.
- **Contract impact:** ADR pending — public API surface partitioned across
  M2 (offscreen core) and M5 (viewer).
- **Deferred alternatives:** none (the alternatives are subsumed by "both").

### D-0002 — Windowing/surface backend: GLFW
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M5
- **Choice:** GLFW, SDL3, or direct xcb/Wayland for window + Vulkan surface + input.
- **Decision:** GLFW.
- **Rationale:** Tiny, battle-tested, cross-platform Vulkan-surface + input
  support; lets us spend our owned-boilerplate budget on Vulkan rather than
  windowing and the X11/Wayland split.
- **Contract impact:** ADR pending in M5 (new third-party dependency, §1.1).
- **Deferred alternatives:** → Backlog B-0001 (direct xcb/Wayland), B-0002 (SDL3).

### D-0003 — Test framework: Catch2 (single-header)
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M1
- **Choice:** Catch2 single-header, a minimal custom harness, or GoogleTest.
- **Decision:** Catch2 (single-header), dev-only dependency.
- **Rationale:** Good ergonomics for the red→green / fault-injection discipline
  with a minimal footprint; consistent with the minimal-deps ethos while not
  reinventing assertion/reporting machinery.
- **Contract impact:** ADR-0002 (Proposed) — dev dependency + teeth-evidence convention.
- **Deferred alternatives:** → Backlog B-0003 (minimal custom harness).

### D-0004 — Texture stores derived (magnitude, phase); transfer function in-shader
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M3 & M4
- **Choice:** Upload raw complex and derive in-shader, vs. upload precomputed
  `(magnitude, phase)` and apply opacity scaling + colormap in-shader.
- **Decision:** Upload precomputed `(magnitude, phase)` as `RG32F`; apply
  scaling and colormap in the fragment/compute path.
- **Rationale:** Single upload; linear/log opacity and colormap selection become
  free runtime toggles (uniforms) with no re-upload; keeps the GPU-resident
  representation independent of the input precision.
- **Contract impact:** ADR pending in M3 (texture format/contents) and M4
  (transfer function).
- **Deferred alternatives:** none material.

### D-0005 — Precision policy: derive in input precision, store fp32, no fp64 GPU path
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M3
- **Choice:** Keep a double-precision path on the GPU vs. downconvert.
- **Decision:** Compute magnitude/phase in the input's precision on the host,
  store `fp32` in the texture; rendering is `fp32`. No fp64 GPU path.
- **Rationale:** Vulkan cannot sample fp64 textures; opacity/color resolution
  does not need fp64. Computing the reduction (abs/arg) in input precision before
  downconversion preserves accuracy where it matters.
- **Contract impact:** ADR pending in M3 (precision policy).
- **Deferred alternatives:** → Backlog B-0004 (fp64 compute path).

### D-0006 — Flat-array memory layout: x-fastest, 0-based
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M3
- **Choice:** Index ordering and base for the flat `(nx, ny, nz)` input array.
- **Decision:** `idx = x + nx*(y + ny*z)`, x-fastest, 0-based.
- **Rationale:** The overwhelmingly common convention for an "nx × ny × nz" flat
  buffer; matches natural 3D-texture row ordering on upload.
- **Contract impact:** ADR pending in M3 (a §5 convention with public surface).
- **Deferred alternatives:** none; caller-overridable strides may be revisited
  if needed (not committed).

### D-0007 — Phase colormap: perceptually-uniform cyclic by default, HSV selectable
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M4
- **Choice:** Classic HSV hue wheel vs. a perceptually-uniform cyclic map.
- **Decision:** Default to a perceptually-uniform cyclic map (twilight/phase
  style); keep the classic HSV hue wheel selectable (traditional domain coloring).
- **Rationale:** Perceptually-uniform cyclic maps avoid the misleading
  brightness banding of HSV while preserving the cyclic phase mapping; HSV
  retained for familiarity/compatibility with domain-coloring conventions.
- **Contract impact:** ADR pending in M4 (exact `arg`→color mapping).
- **Deferred alternatives:** → Backlog B-0005 (additional colormaps).

### D-0008 — Rendering technique: ray-marched DVR, verified headlessly first
- **Date / milestone:** 2026-06-18 / M0 (init), realized in M2–M4
- **Choice:** Direct volume rendering technique and verification strategy.
- **Decision:** Ray-marched DVR with front-to-back alpha compositing; verify the
  renderer offscreen via deterministic pixel readback before adding the window.
- **Rationale:** DVR maps cleanly onto the abs→opacity / arg→color transfer
  function and onto interactive framerates; headless-first keeps contract tests
  deterministic and decoupled from windowing.
- **Contract impact:** ADR pending in M4 (compositing model).
- **Deferred alternatives:** none material at this stage.

---

## Backlog

### B-0001 — Direct xcb/Wayland windowing (zero windowing dependency)
- **Origin:** D-0002 (road not taken).
- **What:** Own the platform windowing/surface code directly instead of GLFW.
- **Why deferred:** Substantial platform-specific boilerplate and the X11/Wayland
  split; not worth it while GLFW satisfies the minimal-deps goal.
- **Revisit when:** A hard zero-third-party-dependency requirement emerges.
- **Contract link:** would supersede the M5 windowing-dependency ADR.

### B-0002 — SDL3 windowing/input backend
- **Origin:** D-0002 (road not taken).
- **What:** Use SDL3 instead of GLFW.
- **Why deferred:** Heavier than GLFW; its extra subsystems (gamepad, audio)
  aren't needed.
- **Revisit when:** Broader input/multimedia needs appear.
- **Contract link:** would supersede the M5 windowing-dependency ADR.

### B-0003 — Minimal custom test harness (zero test dependency)
- **Origin:** D-0003 (road not taken).
- **What:** Replace Catch2 with an owned minimal assertion/reporting harness.
- **Why deferred:** Catch2's footprint is acceptable and its ergonomics aid the
  teeth discipline.
- **Revisit when:** Catch2's build cost or footprint becomes objectionable.
- **Contract link:** would supersede the M1 test-framework ADR.

### B-0004 — fp64 GPU compute path
- **Origin:** D-0005 (road not taken).
- **What:** Carry double precision onto the GPU for derived quantities.
- **Why deferred:** Vulkan can't sample fp64 textures; no demonstrated need.
- **Revisit when:** Rendering reveals precision artifacts traceable to `fp32`
  derived storage.
- **Contract link:** would supersede the M3 precision-policy ADR.

### B-0005 — Additional colormaps beyond default + HSV
- **Origin:** D-0007 (road not taken).
- **What:** Offer further cyclic colormaps (and/or caller-supplied maps).
- **Why deferred:** Two maps cover the immediate need; more is scope creep now.
- **Revisit when:** Users request specific additional maps.
- **Contract link:** would extend the M4 colormap ADR.

### B-0006 — Vulkan Memory Allocator (VMA) for device memory
- **Origin:** D-0013 / ADR-0006 (road not taken).
- **What:** Adopt AMD's VMA (single-header) instead of raw `vkAllocateMemory`.
- **Why deferred:** M2 needs only one image + one staging buffer; raw allocation
  is adequate and avoids a new dependency before it pays off.
- **Revisit when:** M3 — allocations proliferate (3D textures, staging, buffers).
- **Contract link:** a new dependency ADR (§1.1) + would amend ADR-0006's memory
  section.

### B-0010 — Present-path (viewer) Slug glyph rendering
- **Origin:** M6 ADR-0023 implementation (D-0036), 2026-06-19.
- **What:** Draw Slug glyph quads on the **present path** (`recordFrame`), not just
  headless `render()`, so the interactive viewer shows text. Needs the glyph vertex
  buffer + atlas texel buffer/view + descriptor set managed across the in-flight
  frame (like `frameOverlayBuf_`), rather than the transient per-render
  `buildGlyphResources`. Also: cache the atlas/encoding instead of rebuilding it
  per frame.
- **Why deferred:** ADR-0023's verification is headless coverage readback; the
  viewer's *live* labels are an M7 concern (the annotation layer decides what text to
  draw, where, and when it changes). Shipping headless-only kept the M6 renderer
  changes bounded.
- **Revisit when:** M7 — bounding box / ticked axes / legend need on-screen labels in
  the viewer.
- **Contract link:** extends ADR-0023 (rendering path) under ADR-0021's overlay; no
  new public contract.

### B-0009 — Declare a top-level project LICENSE
- **Origin:** M6 font vetting (ADR-0022), 2026-06-19 — the repo has no
  `LICENSE`/`COPYING` of its own.
- **What:** Choose and add the project's own license. A usable library should declare
  one; it also governs whether GPL-with-Distribution-Exception assets (e.g. NCM's
  `NewCM10-Regular`) could ever be bundled (they require a GPL-compatible program
  license — we currently sidestep this by bundling **GFL** faces only).
- **Why deferred / open:** the license choice is the maintainer's; not blocking M6
  (the bundled NCM faces are GFL, which does not constrain our code license).
- **Revisit when:** before a public release, or if bundling any GPL+DE asset.
- **Contract link:** none yet (project-governance, would touch ADR-0001).

### B-0008 — Opacity correction for sample spacing (dt-independent α)
- **Origin:** M5 benchmark teeth investigation (D-0030), 2026-06-19.
- **What:** The per-sample opacity (ADR-0013) does not account for the ray step
  spacing `dt`, so the accumulated opacity — and thus the displayed density and the
  early-termination saturation point — depends on `stepCount`: more steps render a
  *denser* image. For a quantitative tool the rendered density should be invariant
  to the sampling rate.
- **Proposed approach:** Standard DVR opacity correction
  `α_dt = 1 − (1 − a)^(dt / refStep)`, so `stepCount` controls only sampling
  fidelity, not displayed density (and makes the ADR-0019 "8× stepCount" teeth bite
  directly, without `--no-early-term`).
- **Why deferred:** M5 is the viewer + performance contract; this is a
  rendering-model refinement, not a viewer concern.
- **Revisit when:** the "usable scientific data plotting library" milestone
  (quantitative correctness of opacity / transfer function).
- **Status:** **Scheduled into M6** — ADR-0020 (Proposed) / D-0031.
- **Contract link:** would extend ADR-0013 (opacity transfer function).

### B-0007 — Volume bounding box with axis ticks/labels
- **Origin:** ADR-0012 review (maintainer, 2026-06-19).
- **What:** Optionally draw the volume's `[0,1]³` bounding box with axis
  ticks/labels for spatial reference in the rendered image.
- **Why deferred:** M4 renders the field itself; annotations/overlays are a
  separable presentation feature.
- **Revisit when:** A milestone adds scene annotation / overlays (likely with or
  after M5's viewer).
- **Contract link:** would extend the M4 rendering ADRs (camera/compositing,
  ADR-0012) and/or a future overlay ADR.
