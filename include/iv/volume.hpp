#ifndef IV_VOLUME_HPP
#define IV_VOLUME_HPP

// Host-side volume data model (ADR-0008, ADR-0010): the public ingestion data
// contract — a flat complex field plus grid dimensions under an x-fastest,
// 0-based layout — together with the host packing of per-voxel (Re, Im) and the
// magnitude-range metadata. Magnitude/phase are derived in-shader at render time
// (ADR-0015), not stored. No Vulkan here; the GPU resource that consumes this is
// iv::vk::Volume (iv/vk/volume.hpp, ADR-0015).

#include "iv/assert.hpp"
#include "iv/error.hpp"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace iv {

// Grid dimensions plus the library-wide flat-array indexing convention
// (ADR-0008, D-0006): x-fastest, 0-based, idx = x + nx*(y + ny*z). Linear
// arithmetic is 64-bit — a several-hundred-per-side volume exceeds 2^31 elements.
struct GridDims {
    std::uint32_t nx{};
    std::uint32_t ny{};
    std::uint32_t nz{};

    [[nodiscard]] constexpr std::size_t count() const noexcept {
        return static_cast<std::size_t>(nx) * ny * nz;
    }

    // Precondition: x < nx && y < ny && z < nz (IV_DEBUG_ASSERT).
    [[nodiscard]] constexpr std::size_t index(std::uint32_t x, std::uint32_t y,
                                              std::uint32_t z) const noexcept {
        IV_DEBUG_ASSERT(x < nx && y < ny && z < nz, "GridDims::index: coordinate out of range");
        return static_cast<std::size_t>(x)
               + static_cast<std::size_t>(nx)
                     * (static_cast<std::size_t>(y)
                        + static_cast<std::size_t>(ny) * static_cast<std::size_t>(z));
    }
};

// Compile-time pin of the convention (ADR-0008 Verification).
static_assert(GridDims{3, 4, 5}.count() == 60);
static_assert(GridDims{3, 4, 5}.index(0, 0, 0) == 0);
static_assert(GridDims{3, 4, 5}.index(1, 0, 0) == 1);   // x is fastest
static_assert(GridDims{3, 4, 5}.index(0, 1, 0) == 3);   // +nx per y step
static_assert(GridDims{3, 4, 5}.index(0, 0, 1) == 12);  // +nx*ny per z step
static_assert(GridDims{3, 4, 5}.index(1, 2, 3) == 43);  // 1 + 3*(2 + 4*3)
static_assert(GridDims{3, 4, 5}.index(2, 3, 4) == 59);  // last element == count-1

// Per-voxel magnitude statistics for M4's opacity normalization (ADR-0010).
struct MagnitudeRange {
    float minPositive{};  // least strictly-positive |z| (0 if the field is all-zero)
    float max{};          // greatest |z| (0 iff the field is all-zero)
};

// Caller-supplied ingestion options (ADR-0010).
struct VolumeOptions {
    std::optional<MagnitudeRange> magnitudeRange{};  // overrides the auto range when set
};

// --- Host validation (ADR-0008/0010); defined in src/volume.cpp. ---
[[nodiscard]] Status validateGrid(GridDims dims);                          // each dim >= 1
[[nodiscard]] Status validateShape(std::size_t inputCount, GridDims dims); // count matches dims
[[nodiscard]] Status validateOptions(const VolumeOptions& options);        // override is sane

// Pack the complex field into `out` (length 2*count, x-fastest interleaved:
// out[2*i] = Re(z), out[2*i+1] = Im(z)) and return the auto magnitude range
// (ADR-0010). Per ADR-0015 the texture stores the raw complex value; magnitude
// and phase are derived in-shader from (Re, Im) at render time — so this is the
// only place double input is narrowed to float (per component, here). The
// magnitude range is computed from |z| in the input precision T and then narrowed,
// so normalization keeps input-precision accuracy. Preconditions
// (IV_DEBUG_ASSERT): in.size() == dims.count() and out.size() == 2*dims.count().
template <class T>
[[nodiscard]] MagnitudeRange deriveField(std::span<const std::complex<T>> in, GridDims dims,
                                         std::span<float> out) {
    IV_DEBUG_ASSERT(in.size() == dims.count(), "deriveField: input size != count");
    IV_DEBUG_ASSERT(out.size() == 2u * dims.count(), "deriveField: output size != 2*count");

    T maxMag = T(0);
    T minPos = T(0);
    bool anyPositive = false;
    const std::size_t n = dims.count();
    for (std::size_t i = 0; i < n; ++i) {
        const std::complex<T> z = in[i];
        out[2u * i] = static_cast<float>(z.real());      // R = Re(z)  (double -> float here)
        out[2u * i + 1u] = static_cast<float>(z.imag()); // G = Im(z)

        const T mag = std::abs(z); // magnitude range only (ADR-0010), in input precision
        if (mag > maxMag) {
            maxMag = mag;
        }
        if (mag > T(0) && (!anyPositive || mag < minPos)) {
            minPos = mag;
            anyPositive = true;
        }
    }
    return MagnitudeRange{static_cast<float>(anyPositive ? minPos : T(0)),
                          static_cast<float>(maxMag)};
}

} // namespace iv

#endif // IV_VOLUME_HPP
