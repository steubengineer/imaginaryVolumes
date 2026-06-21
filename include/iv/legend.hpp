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

    // The field name drives the captions |fieldName| (magnitude, above the bar) and
    // arg(fieldName) (phase, below) — distinct from the verbose plot title (PlotAxes::title).
    // It is a general label string and may carry inline `$…$` math (ADR-0033); the default
    // `"$f$"` renders the field in true math italic (so the captions read |𝑓| / arg(𝑓)). For an
    // upright field set "f"; for Greek, `"$\\Phi$"`. Override a whole caption via magnitudeLabel /
    // phaseLabel (the escape hatch).
    std::string fieldName{"$f$"};
    std::string magnitudeLabel{}; // explicit magnitude caption; empty => derive "|" fieldName "|"
    std::string phaseLabel{};     // explicit phase caption;     empty => derive "arg(" fieldName ")"

    // Resolved captions (ADR-0031): the explicit override if set, else derived from fieldName
    // (empty when both are empty -> that caption is not drawn).
    [[nodiscard]] std::string magnitudeCaption() const {
        return !magnitudeLabel.empty() ? magnitudeLabel
               : fieldName.empty()     ? std::string{}
                                       : "|" + fieldName + "|";
    }
    [[nodiscard]] std::string phaseCaption() const {
        return !phaseLabel.empty() ? phaseLabel
               : fieldName.empty() ? std::string{}
                                   : "arg(" + fieldName + ")";
    }

    // Swatch rectangle in NDC = {left, top, right, bottom}, Vulkan clip space (y-down, so
    // top < bottom). Default: a panel on the right of the frame, vertically centered. The
    // numeric tick labels extend to the right of `right`; keep `right` < ~0.85 to leave room.
    std::array<float, 4> rectNdc{0.60f, -0.45f, 0.84f, 0.45f};

    bool show{true};
};

} // namespace iv

#endif // IV_LEGEND_HPP
