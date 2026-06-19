# ADR-0016: Windowing & Surface Dependency (GLFW); Presentation-Capable Context

- **Status:** Accepted
- **Date:** 2026-06-19
- **Supersedes:** none

## Context
M5 needs a window, a Vulkan surface, and input. **D-0002** chose GLFW; **D-0001**
keeps the offscreen core windowing-free with a thin viewer layered on top. GLFW is
a new third-party dependency, so per **ADR-0001 §1.1** it needs this ADR and a
vetting/acquisition decision; the maintainer chose **system `find_package`** (it is
not currently installed). The headless `Context` (**ADR-0005**) builds a
surface-less instance/device; presentation additionally needs surface instance
extensions, the `VK_KHR_swapchain` device extension, and a present-capable queue.

## Decision
**GLFW via `find_package(glfw3)`, viewer-only.** The core library `iv` does **not**
link GLFW (D-0001); GLFW and all presentation code live in a separate **`iv_viewer`**
target. A CMake option **`IV_BUILD_VIEWER`** (default ON when `glfw3` is found, OFF
otherwise) gates it, so `iv`, the tests, and the benchmark build with **no** GLFW.
New dependency recorded per ADR-0001 (D-0026).

**Presentation-capable Context (extends ADR-0005).** Add an opt-in presentation
mode to `iv::vk::Context`:
- enable caller-supplied **instance extensions** (the viewer passes the results of
  `glfwGetRequiredInstanceExtensions()` — surface + platform surface) on top of the
  Debug debug-utils set;
- require and enable the **`VK_KHR_swapchain`** device extension;
- select a physical device (same ranking, ADR-0005) whose chosen **graphics queue
  family also supports presentation**, verified against the surface with
  `vkGetPhysicalDeviceSurfaceSupportKHR` once the surface exists.

The headless `Context::create()` is unchanged (no surface/swapchain). A **separate
present queue is not supported in M5** (graphics == present on the target hardware;
deferred to Backlog).

**Window & input (viewer).** GLFW creates the window with **no GL context**
(`GLFW_CLIENT_API = GLFW_NO_API`), the surface via `glfwCreateWindowSurface`, and
delivers input through callbacks (cursor position, mouse button, scroll, key,
framebuffer-resize). GLFW init/terminate and window lifetime are owned by the
viewer (RAII).

## Contract Specification
- `iv` (core) links **no** GLFW; only `iv_viewer` links `glfw`. `IV_BUILD_VIEWER`
  gates the viewer; the rest of the build is unaffected by GLFW's absence.
- Presentation `Context`: instance includes the GLFW-required surface extensions;
  device enables `VK_KHR_swapchain`; the graphics queue family presents to the
  surface (else `Errc::unsupported_configuration`). Headless `create()` unchanged.
- Window uses `GLFW_NO_API`; `glfwCreateWindowSurface` failures map to `Errc`
  (ADR-0004). The surface is owned (destroyed before the instance).
- Single-threaded (ADR-0007): GLFW event polling and rendering on the owner thread.

## Consequences
- The core stays windowing-free and headless-testable (D-0001); the viewer is
  optional and isolated.
- Requires `libglfw3-dev` installed (documented); without it the viewer is simply
  not built — the library and its gates are unaffected.
- Reusing the extended `Context` avoids duplicating instance/device/validation
  logic in the viewer.
- Assuming graphics == present keeps M5 simple; a transfer/present-queue split is
  deferred.

## Alternatives Considered
- **Vendor GLFW (submodule):** rejected by the maintainer — system `find_package`
  is simpler and keeps the repo small; ADR-0001 permits the system path.
- **A viewer-owned instance/device** separate from `Context`: rejected — duplicates
  ADR-0005's selection/validation logic.
- **SDL3 / direct xcb-Wayland:** rejected (D-0002; Backlog B-0001/B-0002).
- **Separate present queue:** deferred (Backlog) — the graphics family presents on
  the target.

## Verification
- `iv` + tests + benchmark build and link with `IV_BUILD_VIEWER=OFF` / no GLFW —
  proving the core is GLFW-free. Teeth: link GLFW into `iv` (not just `iv_viewer`)
  → the "core builds without GLFW" configuration breaks.
- With GLFW present: the viewer builds; creating the presentation `Context` +
  window + surface is validation-clean; the graphics queue presents (asserted).
- Orbit-camera math and the benchmark are headless (ADR-0018/0019); the windowed
  present loop is verified on a display (ADR-0017).
