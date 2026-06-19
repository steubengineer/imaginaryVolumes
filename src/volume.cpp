#include "iv/volume.hpp"

// Host-side ingestion validation (ADR-0008/0010). deriveField is header-defined
// (a template); these non-template validators live here.

namespace iv {

Status validateGrid(GridDims dims) {
    if (dims.nx == 0u || dims.ny == 0u || dims.nz == 0u) {
        return make_error(Errc::invalid_argument, "grid dimensions must all be >= 1");
    }
    return {};
}

Status validateShape(std::size_t inputCount, GridDims dims) {
    if (inputCount != dims.count()) {
        return make_error(Errc::invalid_argument,
                          "input element count does not match nx*ny*nz");
    }
    return {};
}

Status validateOptions(const VolumeOptions& options) {
    if (options.magnitudeRange) {
        const MagnitudeRange r = *options.magnitudeRange;
        // The negated form also rejects NaN.
        if (!(r.minPositive >= 0.0f) || !(r.max >= r.minPositive)) {
            return make_error(Errc::invalid_argument,
                              "magnitudeRange override must satisfy 0 <= minPositive <= max");
        }
    }
    return {};
}

} // namespace iv
