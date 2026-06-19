#ifndef IV_VK_RENDERER_HPP
#define IV_VK_RENDERER_HPP

// Offscreen direct-volume ray-marcher (M4): a compute pipeline (ADR-0011) that
// ray-marches an iv::vk::Volume (ADR-0012 camera/compositing) with an
// abs→opacity transfer function (ADR-0013) and an arg→color cyclic colormap
// (ADR-0014), writing an R8G8B8A8_UNORM image read back with the ADR-0006 layout.
// Move-only; borrows the Context (which must outlive it). Not thread-safe
// (ADR-0007): a Debug thread-affinity check guards render().

#include "iv/error.hpp"
#include "iv/vk/context.hpp"
#include "iv/vk/offscreen.hpp" // ImageReadback (ADR-0006)
#include "iv/vk/unique.hpp"
#include "iv/vk/volume.hpp"
#include "iv/vk/vulkan.hpp"

#include <array>
#include <cstdint>
#include <thread>

namespace iv::vk {

// Per-render parameters (ADR-0012/0013/0014). The magnitude range is taken from
// the Volume (`volume.magnitudeRange()`), so it is not duplicated here.
struct RenderParams {
    // Camera (ADR-0012): right-handed, +Y up; the volume is the unit cube [0,1]^3.
    std::array<float, 3> eye{2.0f, 1.6f, 2.4f};
    std::array<float, 3> target{0.5f, 0.5f, 0.5f};
    std::array<float, 3> up{0.0f, 1.0f, 0.0f};
    float vfovRadians{0.6f};
    // Compositing (ADR-0012).
    std::uint32_t stepCount{256};
    float alphaTermination{0.995f};
    std::array<float, 4> background{0.0f, 0.0f, 0.0f, 1.0f};
    // Opacity transfer function (ADR-0013): 0 = linear, 1 = logarithmic.
    std::uint32_t opacityMode{0};
    float densityScale{1.0f};
    // Phase colormap (ADR-0014): 0 = perceptually-uniform LUT, 1 = HSV hue wheel.
    std::uint32_t colormapMode{0};
};

class Renderer {
public:
    // Build the compute pipeline, samplers, and the colormap LUT (ADR-0011/0014).
    [[nodiscard]] static Result<Renderer> create(const Context& ctx);

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) noexcept = default;
    Renderer& operator=(Renderer&&) noexcept = default;
    ~Renderer() = default;

    // Render `volume` to a width x height image and read it back (ADR-0006 layout:
    // top-left origin, row-major, pixel (x,y) at byte (y*w+x)*4, R,G,B,A).
    [[nodiscard]] Result<ImageReadback> render(const Volume& volume, std::uint32_t width,
                                               std::uint32_t height, const RenderParams& params);

    // Present path (ADR-0017): record a render of `volume` with `params` into the
    // caller-provided command buffer, dispatching into an internal storage image
    // (sized to `dstExtent`, lazily (re)created) and blitting it into `dstImage`.
    // No host readback. `dstImage` must have eTransferDst usage; its prior contents
    // are discarded and it is left in eTransferDstOptimal (the caller transitions
    // it to ePresentSrcKHR). The command buffer must be in the recording state.
    [[nodiscard]] Status recordFrame(::vk::CommandBuffer cmd, const Volume& volume,
                                     const RenderParams& params, ::vk::Image dstImage,
                                     ::vk::Extent2D dstExtent);

    // True if the volume sampler uses linear filtering (false => nearest fallback
    // when R32G32_SFLOAT lacks linear-filter support; ADR-0009 note).
    [[nodiscard]] bool volumeLinearFilter() const noexcept { return volumeLinearFilter_; }

private:
    Renderer() = default;
    void checkAffinity() const noexcept;

    // Write the 4 compute descriptors (volume sampler, storage image, UBO, LUT).
    void writeComputeDescriptors(::vk::DescriptorSet set, ::vk::ImageView volumeView,
                                 ::vk::ImageView storageView, ::vk::Buffer ubo);
    // Lazily (re)create the present-path storage image / UBO / descriptor set when
    // the extent or volume changes (ADR-0017).
    [[nodiscard]] Status ensureFrameResources(const Volume& volume, ::vk::Extent2D extent);

    // Borrowed from the Context, which must outlive this Renderer.
    ::vk::Device device_{};
    ::vk::PhysicalDevice physicalDevice_{};
    ::vk::Queue queue_{};
    ::vk::CommandPool commandPool_{};
    std::thread::id ownerThread_{};
    bool volumeLinearFilter_{true};

    // Owned; destruction is reverse-declaration order (LUT view/image before its
    // memory; pipeline before its layout).
    Unique<::vk::DescriptorSetLayout> setLayout_;
    Unique<::vk::PipelineLayout> pipelineLayout_;
    Unique<::vk::Pipeline> pipeline_;
    Unique<::vk::DescriptorPool> descriptorPool_;
    Unique<::vk::Sampler> volumeSampler_;
    Unique<::vk::Sampler> colormapSampler_;
    Unique<::vk::DeviceMemory> lutMemory_;
    Unique<::vk::Image> lutImage_;
    Unique<::vk::ImageView> lutView_;

    // Present-path (recordFrame) resources, lazily (re)created on extent/volume
    // change. Declared after the pipeline objects; the view/image precede their
    // backing memory only within each (image, memory) pair via reset order below —
    // so memory_ members are declared before their image/view here.
    ::vk::Extent2D frameExtent_{0, 0};
    ::vk::ImageView frameVolumeView_{};
    void* frameUboMapped_{nullptr};
    Unique<::vk::DeviceMemory> frameImageMem_;
    Unique<::vk::Image> frameImage_;
    Unique<::vk::ImageView> frameView_;
    Unique<::vk::DeviceMemory> frameUboMem_;
    Unique<::vk::Buffer> frameUbo_;
    Unique<::vk::DescriptorPool> frameDescriptorPool_;
    ::vk::DescriptorSet frameSet_{};
};

} // namespace iv::vk

#endif // IV_VK_RENDERER_HPP
