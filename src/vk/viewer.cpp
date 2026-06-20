#include "iv/vk/viewer.hpp"

#include "iv/assert.hpp"
#include "iv/vk/commands.hpp" // imageBarrier
#include "iv/vk/result.hpp"

// vulkan.hpp (via the headers above) defines VK_VERSION_1_0 before GLFW is
// included, so GLFW exposes its Vulkan entry points (glfwCreateWindowSurface,
// glfwGetRequiredInstanceExtensions, glfwVulkanSupported).
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace iv::vk {

namespace vkh = ::vk;

namespace {

constexpr std::uint64_t kNoTimeout = std::numeric_limits<std::uint64_t>::max();

// Scroll/drag sensitivities (ADR-0018): pixel deltas -> radians; one scroll notch
// -> a 10% dolly.
constexpr float kOrbitRadiansPerPixel = 0.005f;
constexpr float kDollyPerScroll = 0.9f;

} // namespace

// All window/Vulkan/interaction state. Held behind a stable unique_ptr so the
// Viewer is cheaply movable while the GLFW user-pointer into this Impl stays
// valid. Member DECLARATION order is load-bearing: members are destroyed in
// reverse, so the GLFW lib guard (first) is torn down last, the window before
// the Context's instance, and all device children before the Context. ~Impl
// waits the device idle first (the cached `device` handle, so no affinity-checked
// accessor runs during teardown).
struct Viewer::Impl {
    // Terminates GLFW on destruction (declared first => destroyed last).
    struct GlfwLib {
        bool ok{false};
        ~GlfwLib() {
            if (ok) {
                glfwTerminate();
            }
        }
    };

    GlfwLib glfw;
    Unique<GLFWwindow*> window; // glfwDestroyWindow; after surface + instance gone
    std::optional<Context> context;
    Unique<vkh::SurfaceKHR> surface;
    Unique<vkh::CommandPool> cmdPool;
    Unique<vkh::SwapchainKHR> swapchain;
    std::optional<Renderer> renderer;
    std::optional<Volume> volume;
    Unique<vkh::Semaphore> imageAvailable;
    std::vector<Unique<vkh::Semaphore>> renderFinished; // one per swapchain image
    Unique<vkh::Fence> inFlight;

    // Cached, non-owning handles (copies of Context-owned objects; valid for the
    // Context's lifetime) and configuration / interaction state.
    vkh::PhysicalDevice physicalDevice{};
    vkh::Device device{};
    vkh::Queue queue{};
    std::uint32_t queueFamilyIndex{0};
    vkh::CommandBuffer cmd{}; // owned by cmdPool

    vkh::Format swapFormat{};
    vkh::Extent2D swapExtent{};
    std::vector<vkh::Image> swapImages;

    OrbitCamera camera;
    RenderParams params;
    Overlay overlay; // drawn over the volume each frame (ADR-0021)
    Viewer::FrameCallback onFrame; // rebuilds the overlay each frame (ADR-0026)
    bool lmbDown{false};
    double lastX{0.0};
    double lastY{0.0};
    bool framebufferResized{false};

    ~Impl() {
        if (device) {
            // Frames may be in flight; finish them before the Unique<> members
            // (semaphores, swapchain, renderer, ...) run their deleters.
            static_cast<void>(device.waitIdle());
        }
    }

    void applyCamera() noexcept {
        params.eye = camera.eye();
        params.target = camera.target();
        params.up = camera.up();
    }

    [[nodiscard]] Status createSwapchain();
    [[nodiscard]] Status recreateSwapchain();
    [[nodiscard]] Status drawFrame();

    // GLFW input callbacks (static so they are plain function pointers; they reach
    // this Impl via the window user-pointer). Members so they can touch private
    // state (ADR-0018 input mapping).
    static Impl* of(GLFWwindow* w) { return static_cast<Impl*>(glfwGetWindowUserPointer(w)); }
    static void onCursorPos(GLFWwindow* w, double x, double y);
    static void onMouseButton(GLFWwindow* w, int button, int action, int mods);
    static void onScroll(GLFWwindow* w, double xoff, double yoff);
    static void onKey(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void onFramebufferSize(GLFWwindow* w, int width, int height);
};

void Viewer::Impl::onCursorPos(GLFWwindow* w, double x, double y) {
    Impl* im = of(w);
    if (im->lmbDown) {
        const float dx = static_cast<float>(x - im->lastX);
        const float dy = static_cast<float>(y - im->lastY);
        // Drag orbits the camera with the cursor (eye follows the drag direction).
        im->camera.orbit(dx * kOrbitRadiansPerPixel, dy * kOrbitRadiansPerPixel);
    }
    im->lastX = x;
    im->lastY = y;
}

void Viewer::Impl::onMouseButton(GLFWwindow* w, int button, int action, int /*mods*/) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) {
        return;
    }
    Impl* im = of(w);
    im->lmbDown = (action == GLFW_PRESS);
    glfwGetCursorPos(w, &im->lastX, &im->lastY);
}

void Viewer::Impl::onScroll(GLFWwindow* w, double /*xoff*/, double yoff) {
    // Scroll up (yoff > 0) zooms in (distance *= 0.9^yoff).
    of(w)->camera.dolly(std::pow(kDollyPerScroll, static_cast<float>(yoff)));
}

void Viewer::Impl::onKey(GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
    // Toggles fire on press only; the decade-window keys ([ ]) also repeat (hold to ramp).
    if (action != GLFW_PRESS && action != GLFW_REPEAT) {
        return;
    }
    const bool repeat = (action == GLFW_REPEAT);
    Impl* im = of(w);
    constexpr float kDecadeStep = 0.5f;
    constexpr float kDecadeMin = 0.5f;  // narrowest useful window
    constexpr float kDecadeMax = 20.0f;
    constexpr float kDecadeInit = 4.0f; // engage here from "off" (logDecades == 0)
    constexpr float kDensityFactor = 1.25f; // multiplicative density step (a scale param)
    constexpr float kDensityMin = 0.02f;
    constexpr float kDensityMax = 50.0f;
    switch (key) {
    case GLFW_KEY_ESCAPE:
        if (!repeat) {
            glfwSetWindowShouldClose(w, GLFW_TRUE);
        }
        break;
    case GLFW_KEY_L: // toggle linear/log opacity (ADR-0013)
        if (!repeat) {
            im->params.opacityMode ^= 1u;
        }
        break;
    case GLFW_KEY_C: // toggle LUT/HSV colormap (ADR-0014)
        if (!repeat) {
            im->params.colormapMode ^= 1u;
        }
        break;
    case GLFW_KEY_R: // reset camera (ADR-0018)
        if (!repeat) {
            im->camera.reset();
        }
        break;
    case GLFW_KEY_UP: // increase opacity density (ADR-0013 densityScale)
        im->params.densityScale *= kDensityFactor;
        if (im->params.densityScale > kDensityMax) {
            im->params.densityScale = kDensityMax;
        }
        break;
    case GLFW_KEY_DOWN: // decrease opacity density
        im->params.densityScale /= kDensityFactor;
        if (im->params.densityScale < kDensityMin) {
            im->params.densityScale = kDensityMin;
        }
        break;
    case GLFW_KEY_RIGHT: // right arrow: more decades (wider window; ADR-0027)
        im->params.logDecades =
            im->params.logDecades <= 0.0f ? kDecadeInit : im->params.logDecades + kDecadeStep;
        if (im->params.logDecades > kDecadeMax) {
            im->params.logDecades = kDecadeMax;
        }
        im->params.opacityMode = 1u; // ensure log mode so the change is visible
        break;
    case GLFW_KEY_LEFT: // left arrow: fewer decades (narrower window, emphasizes the peak)
        im->params.logDecades =
            im->params.logDecades <= 0.0f ? kDecadeInit : im->params.logDecades - kDecadeStep;
        if (im->params.logDecades < kDecadeMin) {
            im->params.logDecades = kDecadeMin;
        }
        im->params.opacityMode = 1u;
        break;
    default:
        break;
    }
}

void Viewer::Impl::onFramebufferSize(GLFWwindow* w, int /*width*/, int /*height*/) {
    of(w)->framebufferResized = true;
}

Status Viewer::Impl::createSwapchain() {
    auto caps = take(physicalDevice.getSurfaceCapabilitiesKHR(surface.get()),
                     "getSurfaceCapabilitiesKHR");
    if (!caps) {
        return std::unexpected(std::move(caps).error());
    }
    auto formats = take(physicalDevice.getSurfaceFormatsKHR(surface.get()), "getSurfaceFormatsKHR");
    if (!formats) {
        return std::unexpected(std::move(formats).error());
    }

    // Prefer a UNORM format so the blit from our linear-UNORM render target does not
    // hit an sRGB-encoding surprise (ADR-0017): B8G8R8A8 first, else R8G8B8A8, else
    // whatever the surface offers first.
    vkh::SurfaceFormatKHR chosen = formats->front();
    for (const auto& f : *formats) {
        if (f.colorSpace == vkh::ColorSpaceKHR::eSrgbNonlinear
            && f.format == vkh::Format::eB8G8R8A8Unorm) {
            chosen = f;
            break;
        }
    }
    if (chosen.format != vkh::Format::eB8G8R8A8Unorm) {
        for (const auto& f : *formats) {
            if (f.colorSpace == vkh::ColorSpaceKHR::eSrgbNonlinear
                && f.format == vkh::Format::eR8G8B8A8Unorm) {
                chosen = f;
                break;
            }
        }
    }

    // Extent: the surface's current extent, or (when it defers to us) the clamped
    // framebuffer size.
    vkh::Extent2D extent = caps->currentExtent;
    if (caps->currentExtent.width == std::numeric_limits<std::uint32_t>::max()) {
        int fbw = 0;
        int fbh = 0;
        glfwGetFramebufferSize(window.get(), &fbw, &fbh);
        extent.width = std::clamp(static_cast<std::uint32_t>(fbw), caps->minImageExtent.width,
                                  caps->maxImageExtent.width);
        extent.height = std::clamp(static_cast<std::uint32_t>(fbh), caps->minImageExtent.height,
                                   caps->maxImageExtent.height);
    }

    std::uint32_t imageCount = caps->minImageCount + 1u;
    if (caps->maxImageCount > 0u && imageCount > caps->maxImageCount) {
        imageCount = caps->maxImageCount;
    }

    const vkh::Device dev = device;
    {
        auto sc = take(dev.createSwapchainKHR(
                           vkh::SwapchainCreateInfoKHR{}
                               .setSurface(surface.get())
                               .setMinImageCount(imageCount)
                               .setImageFormat(chosen.format)
                               .setImageColorSpace(chosen.colorSpace)
                               .setImageExtent(extent)
                               .setImageArrayLayers(1u)
                               .setImageUsage(vkh::ImageUsageFlagBits::eTransferDst)
                               .setImageSharingMode(vkh::SharingMode::eExclusive)
                               .setPreTransform(caps->currentTransform)
                               .setCompositeAlpha(vkh::CompositeAlphaFlagBitsKHR::eOpaque)
                               .setPresentMode(vkh::PresentModeKHR::eFifo)
                               .setClipped(VK_TRUE)),
                       "createSwapchainKHR");
        if (!sc) {
            return std::unexpected(std::move(sc).error());
        }
        swapchain =
            Unique<vkh::SwapchainKHR>(*sc, [dev](vkh::SwapchainKHR h) { dev.destroySwapchainKHR(h); });
    }
    {
        auto imgs = take(dev.getSwapchainImagesKHR(swapchain.get()), "getSwapchainImagesKHR");
        if (!imgs) {
            return std::unexpected(std::move(imgs).error());
        }
        swapImages = *std::move(imgs);
    }
    swapFormat = chosen.format;
    swapExtent = extent;

    // One renderFinished semaphore per swapchain image (signaled by the frame's
    // submit, waited by its present) so present never reuses a still-pending
    // semaphore (ADR-0017, validation-clean).
    renderFinished.clear();
    renderFinished.reserve(swapImages.size());
    for (std::size_t i = 0; i < swapImages.size(); ++i) {
        auto s = take(dev.createSemaphore(vkh::SemaphoreCreateInfo{}), "createSemaphore(present)");
        if (!s) {
            return std::unexpected(std::move(s).error());
        }
        renderFinished.emplace_back(*s, [dev](vkh::Semaphore h) { dev.destroySemaphore(h); });
    }
    return {};
}

Status Viewer::Impl::recreateSwapchain() {
    // Block while minimized (zero framebuffer) until it is non-zero again.
    int fbw = 0;
    int fbh = 0;
    glfwGetFramebufferSize(window.get(), &fbw, &fbh);
    while ((fbw == 0 || fbh == 0) && !glfwWindowShouldClose(window.get())) {
        glfwWaitEvents();
        glfwGetFramebufferSize(window.get(), &fbw, &fbh);
    }
    if (auto s = check(device.waitIdle(), "waitIdle(recreate)"); !s) {
        return std::unexpected(std::move(s).error());
    }
    swapchain.reset(); // destroy the old swapchain before creating the new one
    return createSwapchain();
}

Status Viewer::Impl::drawFrame() {
    const vkh::Device dev = device;
    const vkh::Fence fence = inFlight.get();
    if (auto s = check(dev.waitForFences(fence, VK_TRUE, kNoTimeout), "waitForFences"); !s) {
        return std::unexpected(std::move(s).error());
    }

    const auto acq = dev.acquireNextImageKHR(swapchain.get(), kNoTimeout, imageAvailable.get(),
                                             nullptr);
    if (acq.result == vkh::Result::eErrorOutOfDateKHR) {
        return recreateSwapchain();
    }
    if (acq.result != vkh::Result::eSuccess && acq.result != vkh::Result::eSuboptimalKHR) {
        return std::unexpected(check(acq.result, "acquireNextImageKHR").error());
    }
    const std::uint32_t imageIndex = acq.value;

    if (auto s = check(dev.resetFences(fence), "resetFences"); !s) {
        return std::unexpected(std::move(s).error());
    }

    const vkh::Image swapImage = swapImages[imageIndex];
    cmd.reset();
    if (auto s = check(cmd.begin(vkh::CommandBufferBeginInfo{}.setFlags(
                           vkh::CommandBufferUsageFlagBits::eOneTimeSubmit)),
                       "beginCommandBuffer(frame)");
        !s) {
        return std::unexpected(std::move(s).error());
    }
    // Let the caller rebuild camera-tracking annotations for this frame (ADR-0026):
    // params already reflects the current camera (applyCamera ran before drawFrame).
    if (onFrame) {
        onFrame(overlay, params, swapExtent.width, swapExtent.height);
    }
    // Dispatch + (optional) overlay + blit into the swapchain image; leaves it in
    // eTransferDstOptimal.
    const Overlay* ov = overlay.empty() ? nullptr : &overlay;
    if (auto s = renderer->recordFrame(cmd, *volume, params, swapImage, swapExtent, ov); !s) {
        return std::unexpected(std::move(s).error());
    }
    // Hand the image to the presentation engine.
    cmd.pipelineBarrier(
        vkh::PipelineStageFlagBits::eTransfer, vkh::PipelineStageFlagBits::eBottomOfPipe,
        vkh::DependencyFlags{}, nullptr, nullptr,
        imageBarrier(swapImage, vkh::ImageLayout::eTransferDstOptimal,
                     vkh::ImageLayout::ePresentSrcKHR, vkh::AccessFlagBits::eTransferWrite,
                     vkh::AccessFlagBits::eNone));
    if (auto s = check(cmd.end(), "endCommandBuffer(frame)"); !s) {
        return std::unexpected(std::move(s).error());
    }

    const vkh::Semaphore waitSem = imageAvailable.get();
    const vkh::Semaphore signalSem = renderFinished[imageIndex].get();
    const vkh::CommandBuffer submitCmd = cmd;
    const vkh::PipelineStageFlags waitStage = vkh::PipelineStageFlagBits::eTransfer;
    if (auto s = check(queue.submit(vkh::SubmitInfo{}
                                        .setWaitSemaphores(waitSem)
                                        .setWaitDstStageMask(waitStage)
                                        .setCommandBuffers(submitCmd)
                                        .setSignalSemaphores(signalSem),
                                    fence),
                       "queueSubmit(frame)");
        !s) {
        return std::unexpected(std::move(s).error());
    }

    const vkh::SwapchainKHR sc = swapchain.get();
    const vkh::Result pr = queue.presentKHR(
        vkh::PresentInfoKHR{}.setWaitSemaphores(signalSem).setSwapchains(sc).setImageIndices(
            imageIndex));
    if (pr == vkh::Result::eErrorOutOfDateKHR || pr == vkh::Result::eSuboptimalKHR
        || framebufferResized) {
        framebufferResized = false;
        return recreateSwapchain();
    }
    if (pr != vkh::Result::eSuccess) {
        return std::unexpected(check(pr, "presentKHR").error());
    }
    return {};
}

Result<Viewer> Viewer::create(const Options& options) {
    auto impl = std::make_unique<Impl>();

    if (glfwInit() != GLFW_TRUE) {
        return make_error(Errc::internal, "glfwInit failed");
    }
    impl->glfw.ok = true;
    if (glfwVulkanSupported() != GLFW_TRUE) {
        return make_error(Errc::unsupported_configuration,
                          "GLFW reports no Vulkan loader / ICD available");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // no OpenGL context (ADR-0016)
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    {
        GLFWwindow* w = glfwCreateWindow(static_cast<int>(options.width),
                                         static_cast<int>(options.height), options.title, nullptr,
                                         nullptr);
        if (w == nullptr) {
            return make_error(Errc::internal, "glfwCreateWindow failed");
        }
        impl->window = Unique<GLFWwindow*>(w, [](GLFWwindow* h) { glfwDestroyWindow(h); });
    }

    // Presentation Context (ADR-0016): GLFW-required instance extensions + swapchain.
    {
        std::uint32_t count = 0;
        const char** required = glfwGetRequiredInstanceExtensions(&count);
        if (required == nullptr) {
            return make_error(Errc::unsupported_configuration,
                              "glfwGetRequiredInstanceExtensions returned none");
        }
        ContextConfig config;
        config.instanceExtensions.assign(required, required + count);
        config.deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        auto ctx = Context::create(config);
        if (!ctx) {
            return std::unexpected(std::move(ctx).error());
        }
        impl->context = *std::move(ctx);
    }
    impl->physicalDevice = impl->context->physicalDevice();
    impl->device = impl->context->device();
    impl->queue = impl->context->queue();
    impl->queueFamilyIndex = impl->context->queueFamilyIndex();

    // Surface (ADR-0016).
    {
        VkSurfaceKHR raw = VK_NULL_HANDLE;
        const VkResult gr = glfwCreateWindowSurface(
            static_cast<VkInstance>(impl->context->instance()), impl->window.get(), nullptr, &raw);
        if (auto s = check(static_cast<vkh::Result>(gr), "glfwCreateWindowSurface"); !s) {
            return std::unexpected(std::move(s).error());
        }
        const vkh::Instance inst = impl->context->instance();
        impl->surface = Unique<vkh::SurfaceKHR>(
            vkh::SurfaceKHR(raw), [inst](vkh::SurfaceKHR h) { inst.destroySurfaceKHR(h); });
    }

    // The graphics queue family must present to this surface (ADR-0016: graphics ==
    // present on the target; no separate present queue in M5).
    {
        auto supported = take(impl->physicalDevice.getSurfaceSupportKHR(impl->queueFamilyIndex,
                                                                        impl->surface.get()),
                              "getSurfaceSupportKHR");
        if (!supported) {
            return std::unexpected(std::move(supported).error());
        }
        if (*supported != VK_TRUE) {
            return make_error(Errc::unsupported_configuration,
                              "the graphics queue family cannot present to the window surface");
        }
    }

    // Renderer (M4 pipeline + present path).
    {
        auto r = Renderer::create(*impl->context);
        if (!r) {
            return std::unexpected(std::move(r).error());
        }
        impl->renderer = *std::move(r);
    }

    // Viewer-owned command pool (resettable: the single frame command buffer is
    // re-recorded each frame) + that command buffer.
    const vkh::Device dev = impl->device;
    {
        auto p = take(dev.createCommandPool(
                          vkh::CommandPoolCreateInfo{}
                              .setFlags(vkh::CommandPoolCreateFlagBits::eResetCommandBuffer)
                              .setQueueFamilyIndex(impl->queueFamilyIndex)),
                      "createCommandPool(viewer)");
        if (!p) {
            return std::unexpected(std::move(p).error());
        }
        impl->cmdPool =
            Unique<vkh::CommandPool>(*p, [dev](vkh::CommandPool h) { dev.destroyCommandPool(h); });
    }
    {
        auto cb = take(dev.allocateCommandBuffers(vkh::CommandBufferAllocateInfo{}
                                                      .setCommandPool(impl->cmdPool.get())
                                                      .setLevel(vkh::CommandBufferLevel::ePrimary)
                                                      .setCommandBufferCount(1u)),
                       "allocateCommandBuffers(viewer)");
        if (!cb) {
            return std::unexpected(std::move(cb).error());
        }
        impl->cmd = (*cb).front();
    }

    // Sync: one imageAvailable semaphore + one in-flight fence (one frame in
    // flight); renderFinished semaphores are per-image, created with the swapchain.
    {
        auto s = take(dev.createSemaphore(vkh::SemaphoreCreateInfo{}), "createSemaphore(acquire)");
        if (!s) {
            return std::unexpected(std::move(s).error());
        }
        impl->imageAvailable =
            Unique<vkh::Semaphore>(*s, [dev](vkh::Semaphore h) { dev.destroySemaphore(h); });
    }
    {
        auto f = take(dev.createFence(vkh::FenceCreateInfo{}.setFlags(
                          vkh::FenceCreateFlagBits::eSignaled)),
                      "createFence(inFlight)");
        if (!f) {
            return std::unexpected(std::move(f).error());
        }
        impl->inFlight = Unique<vkh::Fence>(*f, [dev](vkh::Fence h) { dev.destroyFence(h); });
    }

    if (auto s = impl->createSwapchain(); !s) {
        return std::unexpected(std::move(s).error());
    }

    // Wire input callbacks to this (stable) Impl.
    glfwSetWindowUserPointer(impl->window.get(), impl.get());
    glfwSetCursorPosCallback(impl->window.get(), &Impl::onCursorPos);
    glfwSetMouseButtonCallback(impl->window.get(), &Impl::onMouseButton);
    glfwSetScrollCallback(impl->window.get(), &Impl::onScroll);
    glfwSetKeyCallback(impl->window.get(), &Impl::onKey);
    glfwSetFramebufferSizeCallback(impl->window.get(), &Impl::onFramebufferSize);

    Viewer viewer;
    viewer.impl_ = std::move(impl);
    return viewer;
}

Result<Viewer> Viewer::create() { return create(Options{}); }

Viewer::Viewer(Viewer&&) noexcept = default;
Viewer& Viewer::operator=(Viewer&&) noexcept = default;
Viewer::~Viewer() = default;

const Context& Viewer::context() const noexcept { return *impl_->context; }

void Viewer::setVolume(Volume&& volume) noexcept { impl_->volume = std::move(volume); }

RenderParams& Viewer::params() noexcept { return impl_->params; }

OrbitCamera& Viewer::camera() noexcept { return impl_->camera; }

void Viewer::requestResize(std::uint32_t width, std::uint32_t height) noexcept {
    glfwSetWindowSize(impl_->window.get(), static_cast<int>(width), static_cast<int>(height));
}

Overlay& Viewer::overlay() noexcept { return impl_->overlay; }

void Viewer::setOnFrame(FrameCallback callback) noexcept { impl_->onFrame = std::move(callback); }

Status Viewer::run() {
    IV_ASSERT(impl_->volume.has_value(), "Viewer::run: no volume set");
    Impl& im = *impl_;
    while (glfwWindowShouldClose(im.window.get()) == GLFW_FALSE) {
        glfwPollEvents();
        int fbw = 0;
        int fbh = 0;
        glfwGetFramebufferSize(im.window.get(), &fbw, &fbh);
        if (fbw == 0 || fbh == 0) {
            glfwWaitEvents(); // minimized: idle until restored
            continue;
        }
        im.applyCamera();
        if (auto s = im.drawFrame(); !s) {
            return s;
        }
    }
    return check(im.device.waitIdle(), "waitIdle(run)");
}

Status Viewer::runFrames(std::uint32_t maxFrames) {
    IV_ASSERT(impl_->volume.has_value(), "Viewer::runFrames: no volume set");
    Impl& im = *impl_;
    std::uint32_t rendered = 0;
    while (rendered < maxFrames && glfwWindowShouldClose(im.window.get()) == GLFW_FALSE) {
        glfwPollEvents();
        int fbw = 0;
        int fbh = 0;
        glfwGetFramebufferSize(im.window.get(), &fbw, &fbh);
        if (fbw == 0 || fbh == 0) {
            glfwWaitEvents();
            continue;
        }
        im.applyCamera();
        if (auto s = im.drawFrame(); !s) {
            return s;
        }
        ++rendered;
    }
    return check(im.device.waitIdle(), "waitIdle(runFrames)");
}

} // namespace iv::vk
