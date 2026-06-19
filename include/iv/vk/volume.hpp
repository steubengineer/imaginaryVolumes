#ifndef IV_VK_VOLUME_HPP
#define IV_VK_VOLUME_HPP

// GPU-resident volume texture (ADR-0009): a 3D RG32F image holding per-voxel
// (magnitude, phase) derived from a complex field (ADR-0008), plus the auto /
// overridden magnitude range (ADR-0010). Move-only, single-owner; it borrows the
// Context's device/queue/pool and must not outlive that Context. Not thread-safe
// (ADR-0007): a Debug thread-affinity check guards each accessor.

#include "iv/error.hpp"
#include "iv/volume.hpp"
#include "iv/vk/context.hpp"
#include "iv/vk/unique.hpp"
#include "iv/vk/vulkan.hpp"

#include <complex>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

namespace iv::vk {

// Host copy of the texture read back from the device (ADR-0009), for verification
// and diagnostics. Tightly packed, x-fastest: texel (x,y,z) lives at the float
// pair starting at dims().index(x,y,z)*2.
class VolumeReadback {
public:
    struct Texel {
        float magnitude;
        float phase;
    };

    VolumeReadback(GridDims dims, std::vector<float> data);

    [[nodiscard]] GridDims dims() const noexcept { return dims_; }
    [[nodiscard]] const std::vector<float>& data() const noexcept { return data_; }

    // Precondition: x < nx && y < ny && z < nz (IV_ASSERT).
    [[nodiscard]] Texel at(std::uint32_t x, std::uint32_t y, std::uint32_t z) const noexcept;

private:
    GridDims dims_;
    std::vector<float> data_;
};

class Volume {
public:
    // Ingest a complex field (ADR-0008) and upload it as a 3D RG32F texture
    // (ADR-0009). Validates dimensions, shape, and options before any GPU work.
    [[nodiscard]] static Result<Volume> create(const Context& ctx,
                                               std::span<const std::complex<float>> field,
                                               GridDims dims, const VolumeOptions& options = {});
    [[nodiscard]] static Result<Volume> create(const Context& ctx,
                                               std::span<const std::complex<double>> field,
                                               GridDims dims, const VolumeOptions& options = {});

    Volume(const Volume&) = delete;
    Volume& operator=(const Volume&) = delete;
    Volume(Volume&&) noexcept = default;
    Volume& operator=(Volume&&) noexcept = default;
    ~Volume() = default;

    [[nodiscard]] GridDims dims() const noexcept {
        checkAffinity();
        return dims_;
    }
    // Effective range: the override if one was supplied, otherwise the auto range.
    [[nodiscard]] MagnitudeRange magnitudeRange() const noexcept {
        checkAffinity();
        return range_;
    }
    // The range computed from the data, regardless of any override.
    [[nodiscard]] MagnitudeRange autoMagnitudeRange() const noexcept {
        checkAffinity();
        return autoRange_;
    }

    [[nodiscard]] ::vk::Image image() const noexcept {
        checkAffinity();
        return image_.get();
    }
    [[nodiscard]] ::vk::ImageView view() const noexcept {
        checkAffinity();
        return view_.get();
    }

    // Copy the texture back to the host (ADR-0009). Transitions the image to
    // TRANSFER_SRC, copies, and restores SHADER_READ_ONLY so the Volume stays
    // usable. Host reads occur only after the submission's fence (ADR-0007).
    [[nodiscard]] Result<VolumeReadback> readback() const;

private:
    Volume() = default;
    void checkAffinity() const noexcept; // Debug-only thread-affinity (ADR-0007)

    template <class T>
    [[nodiscard]] static Result<Volume> createImpl(const Context& ctx,
                                                   std::span<const std::complex<T>> field,
                                                   GridDims dims, const VolumeOptions& options);

    // Borrowed from the Context, which must outlive this Volume.
    ::vk::Device device_{};
    ::vk::PhysicalDevice physicalDevice_{};
    ::vk::Queue queue_{};
    ::vk::CommandPool commandPool_{};

    GridDims dims_{};
    MagnitudeRange range_{};
    MagnitudeRange autoRange_{};
    std::thread::id ownerThread_{};

    // Destruction is reverse-declaration order, and that order is load-bearing:
    // the view and image (declared after) are destroyed before the memory backing
    // them is freed.
    Unique<::vk::DeviceMemory> memory_;
    Unique<::vk::Image> image_;
    Unique<::vk::ImageView> view_;
};

} // namespace iv::vk

#endif // IV_VK_VOLUME_HPP
