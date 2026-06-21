#ifndef IV_LEGEND_HPP
#define IV_LEGEND_HPP

// Declarative legend/colorbar model (ADR-0028): a 2-D swatch depicting the combined transfer
// function — phase -> color (ADR-0014) across its width, magnitude -> opacity (ADR-0013/0027)
// up its height — with labeled bounds. Pure host data (no Vulkan, no HarfBuzz); lives in core
// `iv` like plot_axes. The view-independent placement is in NDC; iv::text::buildLegend projects
// it into an iv::vk::Overlay (it needs a Shaper for the numeric/axis labels).

#include "iv/volume.hpp" // MagnitudeRange

#include <array>
#include <cstdint>
#include <string>

namespace iv {

struct LegendSpec {
    // The transfer state to depict. MUST match the active RenderParams so the legend agrees
    // with the render; the high-level facade keeps them in sync (ADR-0029).
    MagnitudeRange range{};         // magnitude bounds (the Volume's effective range, ADR-0010)
    std::uint32_t colormapMode{0};  // 0 = twilight LUT, 1 = HSV hue wheel (ADR-0014)
    std::uint32_t opacityMode{0};   // 0 = linear, 1 = logarithmic (ADR-0013)
    float densityScale{1.0f};       // ADR-0013
    float logDecades{0.0f};         // log decade window, 0 = full range (ADR-0027)
    // Reference thickness for the opacity "thickness" correction (ADR-0030): the swatch alpha is
    // accumulatedOpacity(a, referenceThickness) = the opacity a uniform slab of this thickness
    // (unit-cube units) renders, so it matches the volume's accumulation. 0 = uncorrected
    // per-sample legend (ADR-0028). Default 0.1 (a soft, readable slab; 1.0 = full traversal).
    float referenceThickness{0.1f};

    std::string magnitudeLabel{"|z|"};   // caption above the bar (the vertical axis)
    std::string phaseLabel{"arg z"};     // caption below the bar (the horizontal axis)

    // Swatch rectangle in NDC = {left, top, right, bottom}, Vulkan clip space (y-down, so
    // top < bottom). Default: a panel on the right of the frame, vertically centered. The
    // numeric tick labels extend to the right of `right`; keep `right` < ~0.85 to leave room.
    std::array<float, 4> rectNdc{0.60f, -0.45f, 0.84f, 0.45f};

    bool show{true};
};

} // namespace iv

#endif // IV_LEGEND_HPP
