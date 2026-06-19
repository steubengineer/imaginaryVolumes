#ifndef IV_VK_VIEW_PROJECTION_HPP
#define IV_VK_VIEW_PROJECTION_HPP

// World -> clip view-projection matrix consistent with the ADR-0012 ray camera
// (ADR-0026). Pure host math: derived directly from the ADR-0012 ray-generation
// formula so a world point projects to the same pixel its camera ray passes
// through. Used to draw world-space annotations (bounding box, axes, ticks) over the
// volume via Overlay::transform, and to anchor screen-space labels.
//
// Conventions (ADR-0012): right-handed world, +Y up, image origin top-left; output
// is Vulkan clip space (x right, y DOWN, z in [0,1], perspective w). Column-major
// (GLSL mat4), so the overlay vertex shader's `transform * vec4(pos,1)` is correct.

#include "iv/vk/renderer.hpp" // RenderParams

#include <array>
#include <cstdint>

namespace iv::vk {

// Column-major 4x4 world->clip matrix for `camera` at the given framebuffer
// `aspect` (= width / height). Matches the ADR-0012 ray camera exactly: for world
// p in front of the eye, the projected pixel equals the pixel whose ray passes
// through p. Degenerate cameras (zero fov, eye == target) yield an unspecified but
// finite matrix.
[[nodiscard]] std::array<float, 16> viewProjection(const RenderParams& camera, float aspect);

// Project a world point with a column-major `m` to a pixel in a top-left-origin
// framebuffer of (width, height). Returns {pixelX, pixelY, clipW}; clipW > 0 means
// the point is in front of the camera (behind if <= 0). Convenience for annotation
// label anchoring (ADR-0026).
[[nodiscard]] std::array<float, 3> projectToPixel(const std::array<float, 16>& m,
                                                  const std::array<float, 3>& worldPoint,
                                                  std::uint32_t width, std::uint32_t height);

} // namespace iv::vk

#endif // IV_VK_VIEW_PROJECTION_HPP
