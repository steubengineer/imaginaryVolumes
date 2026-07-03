#include "iv/transfer.hpp"

#include "iv/vk/colormap_lut.hpp" // iv::vk::kTwilightLut / kColormapLutSize (committed data)

#include <algorithm>
#include <cmath>

namespace iv {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kLn10 = 2.302585093f; // matches the shader's ln(10) literal (ADR-0027)

float fract(float v) noexcept { return v - std::floor(v); }

// Mirror ray_march.comp::hsv2rgb: HSV(h, 1, 1) -> rgb, h periodic.
std::array<float, 3> hsv2rgb(float h) noexcept {
    const float r = std::abs(fract(h + 1.0f) * 6.0f - 3.0f);
    const float g = std::abs(fract(h + 2.0f / 3.0f) * 6.0f - 3.0f);
    const float b = std::abs(fract(h + 1.0f / 3.0f) * 6.0f - 3.0f);
    return {std::clamp(r - 1.0f, 0.0f, 1.0f), std::clamp(g - 1.0f, 0.0f, 1.0f),
            std::clamp(b - 1.0f, 0.0f, 1.0f)};
}

// Sample a 256-entry RGBA8 cyclic LUT exactly as the GPU sampler1DArray layer does: texel
// centers (i+0.5)/N, linear filtering with REPEAT wrap (entry N-1 lerps back to 0). `lut` is one
// layer of iv::vk::kColormapLuts.
std::array<float, 3> sampleLut(const unsigned char* lut, float t) noexcept {
    const int n = static_cast<int>(iv::vk::kColormapLutSize);
    const float x = t * static_cast<float>(n) - 0.5f; // continuous texel coordinate
    const float fl = std::floor(x);
    const float f = x - fl;
    const int i0 = static_cast<int>(fl);
    const auto wrap = [n](int i) noexcept {
        const int m = i % n;
        return m < 0 ? m + n : m;
    };
    const int a = wrap(i0);
    const int b = wrap(i0 + 1);
    std::array<float, 3> out{};
    for (int c = 0; c < 3; ++c) {
        const float ca = static_cast<float>(lut[a * 4 + c]) / 255.0f;
        const float cb = static_cast<float>(lut[b * 4 + c]) / 255.0f;
        out[static_cast<std::size_t>(c)] = ca + (cb - ca) * f;
    }
    return out;
}

// ADR-0036: colormapMode -> baked LUT layer index. 0->0 twilight, 2->1 infinity, 3->2 grayscale
// (mode 1 = analytic HSV has no layer; the caller handles it). Out-of-range clamps to twilight.
// Mirrors ray_march.comp::colormapLayer.
unsigned int colormapLutLayer(std::uint32_t mode) noexcept {
    const unsigned int layer = (mode == 0u) ? 0u : mode - 1u;
    return layer < iv::vk::kColormapLutCount ? layer : 0u;
}

} // namespace

float accumulatedOpacity(float perSampleAlpha, float thickness) noexcept {
    const float a = std::clamp(perSampleAlpha, 0.0f, 1.0f);
    if (thickness <= 0.0f) {
        return a; // uncorrected: the ADR-0028 per-sample legend
    }
    // 1 - (1-a)^(256*thickness): the ADR-0020 accumulation over a uniform slab (1-a in [0,1]).
    return std::clamp(1.0f - std::pow(1.0f - a, kReferenceSteps * thickness), 0.0f, 1.0f);
}

std::array<float, 3> phaseColor(float phaseRadians, std::uint32_t colormapMode) noexcept {
    const float t = (phaseRadians + kPi) / (2.0f * kPi);
    if (colormapMode == 1u) {
        return hsv2rgb(t);
    }
    return sampleLut(iv::vk::kColormapLuts[colormapLutLayer(colormapMode)], t);
}

float transferNormalized(float magnitude, MagnitudeRange range, std::uint32_t opacityMode,
                         float logDecades) noexcept {
    const float minP = range.minPositive;
    const float maxM = range.max;
    float mn = 0.0f;
    if (opacityMode == 0u) { // linear (ADR-0013)
        if (maxM > 0.0f) {
            mn = std::clamp(magnitude / maxM, 0.0f, 1.0f);
        }
    } else { // logarithmic (ADR-0013/0027)
        if (logDecades > 0.0f) {
            if (magnitude > 0.0f && maxM > 0.0f) { // top `logDecades` decades below max
                mn = std::clamp(
                    1.0f + (std::log(magnitude) - std::log(maxM)) / (logDecades * kLn10), 0.0f,
                    1.0f);
            }
        } else if (magnitude > minP && maxM > minP) { // full [minPositive, max] range
            mn = std::clamp((std::log(magnitude) - std::log(minP)) / (std::log(maxM) - std::log(minP)),
                            0.0f, 1.0f);
        }
    }
    return mn;
}

float transferOpacity(float magnitude, MagnitudeRange range, std::uint32_t opacityMode,
                      float densityScale, float logDecades) noexcept {
    return std::clamp(transferNormalized(magnitude, range, opacityMode, logDecades) * densityScale,
                      0.0f, 1.0f);
}

} // namespace iv
