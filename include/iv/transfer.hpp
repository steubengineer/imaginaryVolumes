#ifndef IV_TRANSFER_HPP
#define IV_TRANSFER_HPP

// Host-side transfer-function evaluators (ADR-0028): pure functions that mirror the
// ray-marcher's per-sample mappings — arg(z) -> color (ADR-0014) and abs(z) -> opacity
// (ADR-0013 linear/log, ADR-0027 decade window). They are the single source of truth for
// any host depiction of the transfer function (the legend, previews, tests). The default
// colormap samples the SAME committed LUT the GPU sampler uses (iv/vk/colormap_lut.hpp), so
// host and shader agree by construction; HSV and the opacity formulas are analytic mirrors.
// Pure host — no Vulkan, no HarfBuzz; lives in core `iv` (like plot_axes / orbit_camera).

#include "iv/volume.hpp" // MagnitudeRange

#include <array>
#include <cstdint>

namespace iv {

// Reference sample count for the ADR-0020 opacity correction (must equal the shader's
// kReferenceSteps): per-sample opacities are authored for a step spacing dt_ref = 1/256.
inline constexpr float kReferenceSteps = 256.0f;

// Accumulate a per-sample opacity `a` (= transferOpacity, ADR-0013/0028) over a uniform slab of
// `thickness` (in unit-cube [0,1]^3 path-length units), mirroring the volume's ADR-0020/0012
// compositing: A = 1 - (1 - a)^(kReferenceSteps * thickness). `thickness <= 0` returns `a`
// unchanged (the uncorrected per-sample legend). Used to correct the legend opacity for the
// volume's "thickness" effect (ADR-0030); result is finite and clamped to [0,1].
[[nodiscard]] float accumulatedOpacity(float perSampleAlpha, float thickness) noexcept;

// arg(z) -> RGB (ADR-0014), mirroring ray_march.comp::sampleColor. `phaseRadians` is the
// phase in [-pi, pi]; values outside wrap cyclically (t = (phase+pi)/(2pi), periodic).
// colormapMode 0 samples the committed twilight LUT with linear interpolation + repeat wrap
// (the same data and rule as the GPU sampler1D); mode 1 is the analytic HSV hue wheel.
// Result is non-premultiplied rgb in [0,1]. phaseColor(-pi) == phaseColor(+pi) (the seam).
[[nodiscard]] std::array<float, 3> phaseColor(float phaseRadians,
                                              std::uint32_t colormapMode) noexcept;

// abs(z) -> normalized opacity position mn in [0,1] (ADR-0013/0027), WITHOUT densityScale —
// the abscissa the opacity ramp/legend places a magnitude on. opacityMode 0 = linear
// (clamp(m/max)); 1 = logarithmic: logDecades > 0 windows the top `logDecades` decades below
// max (ADR-0027), else the full [minPositive, max] range. Degenerate/empty ranges -> 0; no
// log is evaluated at <= 0 (guards precede it), so the result is always finite.
[[nodiscard]] float transferNormalized(float magnitude, MagnitudeRange range,
                                       std::uint32_t opacityMode, float logDecades) noexcept;

// abs(z) -> per-sample opacity alpha in [0,1], mirroring ray_march.comp::sampleOpacity before
// the ADR-0020 dt-correction: clamp(transferNormalized(...) * densityScale, 0, 1).
[[nodiscard]] float transferOpacity(float magnitude, MagnitudeRange range,
                                    std::uint32_t opacityMode, float densityScale,
                                    float logDecades) noexcept;

} // namespace iv

#endif // IV_TRANSFER_HPP
