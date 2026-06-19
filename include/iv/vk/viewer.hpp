#ifndef IV_VK_VIEWER_HPP
#define IV_VK_VIEWER_HPP

// Windowed interactive viewer (ADR-0016/0017/0018): owns a GLFW window + Vulkan
// surface, a presentation-capable Context (ADR-0016), a swapchain + present loop
// (ADR-0017), a Renderer, an OrbitCamera, and the current Volume + RenderParams.
// Single-threaded (ADR-0007): events are polled and frames recorded on the owner
// thread. This is the ONLY GLFW/display-coupled component; the core library `iv`
// stays windowing-free (D-0001) and this lives in the separate `iv_viewer` target.
//
// Usage (two-step, because a Volume is built from the presentation Context):
//   auto v = iv::vk::Viewer::create();                       // window + Context + swapchain
//   auto vol = iv::vk::Volume::create(v->context(), field, dims);
//   v->setVolume(std::move(*vol));
//   v->run();                                                // until the window closes

#include "iv/error.hpp"
#include "iv/orbit_camera.hpp"
#include "iv/vk/context.hpp"
#include "iv/vk/renderer.hpp" // RenderParams, Overlay
#include "iv/vk/volume.hpp"

#include <cstdint>
#include <functional>
#include <memory>

namespace iv::vk {

class Viewer {
public:
    struct Options {
        std::uint32_t width{1280};
        std::uint32_t height{720};
        const char* title{"imaginaryVolumes"};
    };

    // Create the GLFW window, presentation Context, surface, swapchain, and
    // Renderer (ADR-0016/0017). The Volume is supplied afterwards via setVolume()
    // because it is built from context(). Maps GLFW/surface failures to Errc.
    [[nodiscard]] static Result<Viewer> create(const Options& options);
    // As create(options) with the default Options.
    [[nodiscard]] static Result<Viewer> create();

    Viewer(const Viewer&) = delete;
    Viewer& operator=(const Viewer&) = delete;
    Viewer(Viewer&&) noexcept;
    Viewer& operator=(Viewer&&) noexcept;
    ~Viewer();

    // The presentation Context (ADR-0016). Build the Volume to display from this:
    // Volume::create(viewer.context(), field, dims).
    [[nodiscard]] const Context& context() const noexcept;

    // Hand over the Volume to display (must have been built from context()).
    void setVolume(Volume&& volume) noexcept;

    // Live-editable render parameters and camera (ADR-0012/0018). The camera is
    // applied onto params() each frame; editing params() directly also takes effect.
    [[nodiscard]] RenderParams& params() noexcept;
    [[nodiscard]] OrbitCamera& camera() noexcept;

    // Request a new window size; the next frame recreates the swapchain at the new
    // framebuffer size (ADR-0017). The window manager may override the request.
    void requestResize(std::uint32_t width, std::uint32_t height) noexcept;

    // Live-editable overlay (lines + quads) drawn over the volume each frame
    // (ADR-0021); empty by default. M7 populates it with the box / axes / legend.
    [[nodiscard]] Overlay& overlay() noexcept;

    // Per-frame callback (ADR-0026), invoked just before each frame is recorded with
    // the live overlay, the current camera-applied RenderParams, and the framebuffer
    // size (pixels). Use it to rebuild camera-tracking annotations each frame, e.g.
    // iv::text::buildAnnotations(overlay, axes, params, w, h, shaper). Empty by default.
    using FrameCallback =
        std::function<void(Overlay&, const RenderParams&, std::uint32_t, std::uint32_t)>;
    void setOnFrame(FrameCallback callback) noexcept;

    // Enter the present loop until the window is closed (ADR-0017). Requires a
    // Volume to have been set.
    [[nodiscard]] Status run();
    // Render exactly `maxFrames` frames then return (verification; ADR-0017).
    [[nodiscard]] Status runFrames(std::uint32_t maxFrames);

private:
    Viewer() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace iv::vk

#endif // IV_VK_VIEWER_HPP
