#include "iv/volume.hpp"

#include "catch_amalgamated.hpp"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

using iv::deriveField;
using iv::Errc;
using iv::GridDims;
using iv::MagnitudeRange;
using iv::validateGrid;
using iv::validateOptions;
using iv::validateShape;
using iv::VolumeOptions;

namespace {
// True iff `s` is an error carrying code `c`. Short-circuits on has_value(), so it
// stays well-defined (no .error() on a value) even if the call wrongly succeeds.
bool rejected(const iv::Status& s, iv::Errc c) {
    return !s.has_value() && s.error().code == c;
}
} // namespace

// teeth: the x-fastest, 0-based indexing convention (ADR-0008 / D-0006). A
// transposed formula changes these mappings. (Also pinned at compile time by the
// static_asserts in iv/volume.hpp.)
TEST_CASE("GridDims indexes x-fastest, 0-based", "[volume]") {
    constexpr GridDims d{3, 4, 5};
    CHECK(d.count() == 60u);
    CHECK(d.index(0, 0, 0) == 0u);
    CHECK(d.index(1, 0, 0) == 1u);   // x is fastest
    CHECK(d.index(0, 1, 0) == 3u);   // +nx per y step
    CHECK(d.index(0, 0, 1) == 12u);  // +nx*ny per z step
    CHECK(d.index(1, 2, 3) == 43u);
    CHECK(d.index(2, 3, 4) == 59u);

    // Every voxel maps to a distinct, in-range linear index (a bijection).
    std::vector<int> seen(d.count(), 0);
    for (std::uint32_t z = 0; z < d.nz; ++z) {
        for (std::uint32_t y = 0; y < d.ny; ++y) {
            for (std::uint32_t x = 0; x < d.nx; ++x) {
                seen[d.index(x, y, z)]++;
            }
        }
    }
    bool bijective = true;
    for (int c : seen) {
        if (c != 1) {
            bijective = false;
        }
    }
    CHECK(bijective);
}

// teeth: stores (Re, Im) in channel order, and computes the magnitude range from
// |z| (ADR-0015 / ADR-0010). An Re/Im swap, or a norm-vs-abs error in the range,
// changes these exact values. Pythagorean inputs give exact integer magnitudes.
// (Magnitude/phase themselves are derived in-shader; see the renderer tests.)
TEST_CASE("deriveField stores (Re, Im) and computes the magnitude range", "[volume]") {
    const GridDims d{2, 2, 2}; // 8 voxels
    const std::vector<std::complex<float>> in{{0.0f, 0.0f}, {3.0f, 4.0f},  {6.0f, 8.0f},
                                              {5.0f, 12.0f}, {8.0f, 6.0f}, {9.0f, 12.0f},
                                              {0.0f, 7.0f},  {0.0f, 0.0f}};
    REQUIRE(in.size() == d.count());

    std::vector<float> out(d.count() * 2u);
    const MagnitudeRange r = deriveField<float>(in, d, out);

    for (std::size_t i = 0; i < in.size(); ++i) {
        CHECK(out[2u * i] == in[i].real());      // R = Re(z)
        CHECK(out[2u * i + 1u] == in[i].imag()); // G = Im(z)
    }
    // range from |z|: minPositive excludes the two zeros; max ignores them too.
    CHECK(r.minPositive == 5.0f);  // |3+4i|
    CHECK(r.max == 15.0f);         // |9+12i|
}

// teeth: an all-zero field has no positive magnitude — minPositive and max are
// both 0 (the documented degenerate case, ADR-0010).
TEST_CASE("deriveField on an all-zero field yields a (0,0) range", "[volume]") {
    const GridDims d{2, 1, 1};
    const std::vector<std::complex<float>> in{{0.0f, 0.0f}, {0.0f, 0.0f}};
    std::vector<float> out(d.count() * 2u);
    const MagnitudeRange r = deriveField<float>(in, d, out);
    CHECK(r.minPositive == 0.0f);
    CHECK(r.max == 0.0f);
}

// teeth: the double path narrows (Re, Im) to fp32 on the host — the ADR-0015
// conversion point. The magnitude range is computed from |z| in double then
// narrowed (D-0005 retained for the range). Magnitude/phase are derived in-shader,
// not here.
TEST_CASE("deriveField double path narrows (Re, Im) to fp32", "[volume]") {
    const GridDims d{2, 2, 1};
    const std::vector<std::complex<double>> dd{{3.0, 4.0}, {6.0, 8.0}, {5.0, 12.0}, {0.0, 0.0}};
    std::vector<float> od(d.count() * 2u);
    const MagnitudeRange r = deriveField<double>(dd, d, od);

    for (std::size_t i = 0; i < dd.size(); ++i) {
        CHECK(od[2u * i] == static_cast<float>(dd[i].real()));
        CHECK(od[2u * i + 1u] == static_cast<float>(dd[i].imag()));
    }
    CHECK(r.minPositive == 5.0f); // |3+4i|=5, |6+8i|=10, |5+12i|=13 (exact); zero excluded
    CHECK(r.max == 13.0f);
}

// teeth: each validator rejects exactly its bad input (ADR-0008/0010). Dropping a
// check flips the matching case from invalid_argument to ok.
TEST_CASE("validators reject malformed ingestion inputs", "[volume]") {
    CHECK(rejected(validateGrid({0, 4, 5}), Errc::invalid_argument));
    CHECK(rejected(validateGrid({3, 0, 5}), Errc::invalid_argument));
    CHECK(rejected(validateGrid({3, 4, 0}), Errc::invalid_argument));
    CHECK(validateGrid({3, 4, 5}).has_value());

    CHECK(rejected(validateShape(59, {3, 4, 5}), Errc::invalid_argument));
    CHECK(validateShape(60, {3, 4, 5}).has_value());

    const VolumeOptions ok{MagnitudeRange{1.0f, 2.0f}};
    const VolumeOptions none{};
    const VolumeOptions badOrder{MagnitudeRange{2.0f, 1.0f}}; // max < minPositive
    const VolumeOptions negative{MagnitudeRange{-1.0f, 2.0f}};
    CHECK(validateOptions(ok).has_value());
    CHECK(validateOptions(none).has_value());
    CHECK(rejected(validateOptions(badOrder), Errc::invalid_argument));
    CHECK(rejected(validateOptions(negative), Errc::invalid_argument));
}
