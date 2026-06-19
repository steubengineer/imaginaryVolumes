#ifndef IV_VK_OFFSCREEN_HPP
#define IV_VK_OFFSCREEN_HPP

// Offscreen render target + host readback (ADR-0006). For M2 the only "render"
// is a clear to a known color; later milestones reuse this target for the volume
// ray-march. The readback layout is fixed and testable.

#include "iv/error.hpp"
#include "iv/vk/context.hpp"
#include "iv/vk/vulkan.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace iv::vk {

// Host-side image read back from the device (ADR-0006): tightly packed, row-major,
// top-left origin, 4 bytes/pixel in R,G,B,A order. Pixel (x,y) lives at byte
// (y*width + x)*4.
class ImageReadback {
public:
    struct Rgba {
        std::uint8_t r;
        std::uint8_t g;
        std::uint8_t b;
        std::uint8_t a;
    };

    ImageReadback(std::uint32_t width, std::uint32_t height,
                  std::vector<std::uint8_t> bytes);

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept { return bytes_; }

    // Precondition: x < width() && y < height() (IV_ASSERT).
    [[nodiscard]] Rgba at(std::uint32_t x, std::uint32_t y) const noexcept;

private:
    std::uint32_t width_;
    std::uint32_t height_;
    std::vector<std::uint8_t> bytes_;
};

// Clear an offscreen R8G8B8A8_UNORM image to `color` (RGBA, each component in
// [0,1]) and read it back to the host (ADR-0006). Precondition: width>0, height>0.
[[nodiscard]] Result<ImageReadback> clearAndReadback(const Context& ctx,
                                                     std::uint32_t width,
                                                     std::uint32_t height,
                                                     std::array<float, 4> color);

} // namespace iv::vk

#endif // IV_VK_OFFSCREEN_HPP
