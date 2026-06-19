#include "iv/vk/context.hpp"
#include "iv/vk/volume.hpp"

#include "catch_amalgamated.hpp"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

using iv::Errc;
using iv::GridDims;
using iv::MagnitudeRange;
using iv::VolumeOptions;
using iv::vk::Context;
using iv::vk::Volume;

namespace {

// True iff `r` is an error carrying code `c`. Short-circuits on has_value(), so it
// stays well-defined (no .error() on a value) even if create() wrongly succeeds.
bool rejected(const iv::Result<Volume>& r, iv::Errc c) {
    return !r.has_value() && r.error().code == c;
}

// A non-cubic field with a distinct value per voxel — so a transposed layout or a
// swapped image extent is observable on readback — plus one true zero voxel.
constexpr GridDims kDims{2, 3, 4}; // 24 voxels

std::vector<std::complex<float>> makeField() {
    std::vector<std::complex<float>> v(kDims.count());
    for (std::uint32_t z = 0; z < kDims.nz; ++z) {
        for (std::uint32_t y = 0; y < kDims.ny; ++y) {
            for (std::uint32_t x = 0; x < kDims.nx; ++x) {
                const std::size_t i = kDims.index(x, y, z);
                const float re = static_cast<float>(i) - 11.0f; // distinct per voxel
                const float im = static_cast<float>(x) - 2.0f * static_cast<float>(z);
                v[i] = std::complex<float>(re, im);
            }
        }
    }
    v[kDims.index(1, 1, 2)] = std::complex<float>(0.0f, 0.0f); // a true zero voxel
    return v;
}

} // namespace

// teeth: round-trips the stored complex value (Re, Im) bit-exactly under the
// x-fastest layout (ADR-0015). A wrong index order, swapped image extent, or an
// Re/Im swap makes at least one voxel diverge.
TEST_CASE("Volume round-trips (Re, Im) bit-exactly (float)", "[vk][volume]") {
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());

    const auto field = makeField();
    auto vol = Volume::create(*ctx, field, kDims);
    REQUIRE(vol.has_value());
    CHECK(vol->dims().nx == kDims.nx);
    CHECK(vol->dims().ny == kDims.ny);
    CHECK(vol->dims().nz == kDims.nz);

    auto rb = vol->readback();
    REQUIRE(rb.has_value());

    bool allMatch = true;
    for (std::uint32_t z = 0; z < kDims.nz; ++z) {
        for (std::uint32_t y = 0; y < kDims.ny; ++y) {
            for (std::uint32_t x = 0; x < kDims.nx; ++x) {
                const auto& z0 = field[kDims.index(x, y, z)];
                const auto t = rb->at(x, y, z);
                if (t.re != z0.real() || t.im != z0.imag()) {
                    allMatch = false;
                }
            }
        }
    }
    CHECK(allMatch);
    CHECK(ctx->validationClean());
}

// teeth: the double input path narrows (Re, Im) to fp32 on the host (ADR-0015
// conversion point) and round-trips bit-exactly. Magnitude/phase are derived
// in-shader (not stored), so there is no per-voxel double-vs-float divergence in
// the stored texels here.
TEST_CASE("Volume double input round-trips (Re, Im) narrowed to fp32", "[vk][volume]") {
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());

    const auto ff = makeField();
    std::vector<std::complex<double>> dd(ff.size());
    for (std::size_t i = 0; i < ff.size(); ++i) {
        dd[i] = std::complex<double>(static_cast<double>(ff[i].real()),
                                     static_cast<double>(ff[i].imag()));
    }

    auto vd = Volume::create(*ctx, dd, kDims);
    REQUIRE(vd.has_value());
    auto rd = vd->readback();
    REQUIRE(rd.has_value());

    bool allMatch = true;
    for (std::uint32_t z = 0; z < kDims.nz; ++z) {
        for (std::uint32_t y = 0; y < kDims.ny; ++y) {
            for (std::uint32_t x = 0; x < kDims.nx; ++x) {
                const auto& z0 = dd[kDims.index(x, y, z)];
                const auto t = rd->at(x, y, z);
                if (t.re != static_cast<float>(z0.real()) ||
                    t.im != static_cast<float>(z0.imag())) {
                    allMatch = false;
                }
            }
        }
    }
    CHECK(allMatch);
    CHECK(ctx->validationClean());
}

// teeth: the auto magnitude range matches a host recomputation; an override is
// returned verbatim while autoMagnitudeRange() still reflects the data (ADR-0010).
TEST_CASE("Volume exposes auto and overridden magnitude range", "[vk][volume]") {
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());

    const auto field = makeField();
    float expMin = 0.0f;
    float expMax = 0.0f;
    bool anyPos = false;
    for (const auto& z0 : field) {
        const float m = std::abs(z0);
        if (m > expMax) {
            expMax = m;
        }
        if (m > 0.0f && (!anyPos || m < expMin)) {
            expMin = m;
            anyPos = true;
        }
    }

    auto autoVol = Volume::create(*ctx, field, kDims);
    REQUIRE(autoVol.has_value());
    CHECK(autoVol->autoMagnitudeRange().minPositive == expMin);
    CHECK(autoVol->autoMagnitudeRange().max == expMax);
    CHECK(autoVol->magnitudeRange().minPositive == expMin); // no override => auto
    CHECK(autoVol->magnitudeRange().max == expMax);

    const MagnitudeRange ovRange{0.5f, 99.0f};
    auto ovVol = Volume::create(*ctx, field, kDims, VolumeOptions{ovRange});
    REQUIRE(ovVol.has_value());
    CHECK(ovVol->magnitudeRange().minPositive == 0.5f); // override, verbatim
    CHECK(ovVol->magnitudeRange().max == 99.0f);
    CHECK(ovVol->autoMagnitudeRange().minPositive == expMin); // auto still computed
    CHECK(ovVol->autoMagnitudeRange().max == expMax);
}

// teeth: determinism (ADR-0007) and validation cleanliness across create+readback.
TEST_CASE("Volume upload is deterministic and validation-clean", "[vk][volume]") {
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());
    const auto field = makeField();
    auto a = Volume::create(*ctx, field, kDims);
    auto b = Volume::create(*ctx, field, kDims);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    auto ra = a->readback();
    auto rb = b->readback();
    REQUIRE(ra.has_value());
    REQUIRE(rb.has_value());
    CHECK(ra->data() == rb->data());
    CHECK(ctx->validationClean());
}

// teeth: create() enforces the ingestion contract (ADR-0008/0010) before any GPU
// work — wrong dims/shape/override are rejected with invalid_argument.
TEST_CASE("Volume::create rejects malformed inputs", "[vk][volume]") {
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());

    const std::vector<std::complex<float>> six(6, std::complex<float>(1.0f, 0.0f));
    CHECK(rejected(Volume::create(*ctx, six, GridDims{0, 2, 3}), Errc::invalid_argument));
    CHECK(rejected(Volume::create(*ctx, six, GridDims{2, 2, 3}), Errc::invalid_argument));
    CHECK(Volume::create(*ctx, six, GridDims{1, 2, 3}).has_value()); // 6 == 1*2*3
    CHECK(rejected(Volume::create(*ctx, six, GridDims{1, 2, 3},
                                  VolumeOptions{MagnitudeRange{5.0f, 1.0f}}),
                   Errc::invalid_argument));
    CHECK(ctx->validationClean());
}
