#ifndef IV_VK_CONTEXT_HPP
#define IV_VK_CONTEXT_HPP

// The Vulkan context: instance (+ optional Debug validation messenger), the
// selected physical device, a logical device with one graphics queue, and a
// command pool (ADR-0005). Move-only, single-owner; tears everything down in
// the correct reverse order via its Unique<> members (ADR-0004). Not thread-safe
// (ADR-0007): a Debug thread-affinity check guards each accessor.

#include "iv/error.hpp"
#include "iv/vk/unique.hpp"
#include "iv/vk/vulkan.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace iv::vk {

// Optional capabilities for a non-headless Context (ADR-0016). For presentation
// the viewer passes the GLFW-required instance extensions plus the swapchain
// device extension; an empty config yields the headless Context (ADR-0005).
struct ContextConfig {
    std::vector<const char*> instanceExtensions; // e.g. glfwGetRequiredInstanceExtensions()
    std::vector<const char*> deviceExtensions;   // e.g. VK_KHR_swapchain
};

class Context {
public:
    // Brings up Vulkan headlessly (ADR-0005). Honors the IV_VULKAN_DEVICE_INDEX
    // environment override. Returns device_unavailable if no suitable device
    // (or an out-of-range override) is found.
    [[nodiscard]] static Result<Context> create();

    // As create(), additionally enabling the requested instance/device extensions
    // (ADR-0016, presentation). The selected device must support all requested
    // device extensions, else device_unavailable. The graphics queue's
    // presentation support is verified by the caller against its surface.
    [[nodiscard]] static Result<Context> create(const ContextConfig& config);

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) noexcept = default;
    Context& operator=(Context&&) noexcept = default;
    ~Context() = default;

    [[nodiscard]] ::vk::Instance instance() const noexcept { checkAffinity(); return instance_.get(); }
    [[nodiscard]] ::vk::PhysicalDevice physicalDevice() const noexcept { checkAffinity(); return physicalDevice_; }
    [[nodiscard]] ::vk::Device device() const noexcept { checkAffinity(); return device_.get(); }
    [[nodiscard]] ::vk::Queue queue() const noexcept { checkAffinity(); return queue_; }
    [[nodiscard]] std::uint32_t queueFamilyIndex() const noexcept { return queueFamilyIndex_; }
    [[nodiscard]] ::vk::CommandPool commandPool() const noexcept { checkAffinity(); return commandPool_.get(); }

    // Number of WARNING/ERROR validation messages observed since creation
    // (0 when validation is not active). ADR-0005.
    [[nodiscard]] std::uint32_t validationMessageCount() const noexcept {
        return validationCount_ ? validationCount_->load(std::memory_order_relaxed) : 0u;
    }
    [[nodiscard]] bool validationClean() const noexcept { return validationMessageCount() == 0u; }

    // True when VK_EXT_line_rasterization + the smoothLines feature were available and
    // enabled at device creation, so the renderer can use anti-aliased (smooth) line
    // rasterization for the overlay (ADR-0021/0026). Opportunistic: falls back to
    // aliased lines when unsupported.
    [[nodiscard]] bool smoothLinesAvailable() const noexcept { return smoothLines_; }

    // Shared handle to the validation counter so a test can observe teardown-time
    // validation: the instance's pNext debug messenger reports undestroyed
    // objects during vkDestroyInstance, after this Context is gone. Null when
    // validation is inactive (Release / layer unavailable).
    [[nodiscard]] std::shared_ptr<const std::atomic<std::uint32_t>>
    validationCounter() const noexcept {
        return validationCount_;
    }

private:
    Context() = default;
    void checkAffinity() const noexcept; // Debug-only thread-affinity (ADR-0007)

    // Destruction is reverse-declaration order, and that order is load-bearing:
    // validationCount_ is declared FIRST so it is destroyed LAST — the instance's
    // pNext debug messenger fires object-leak checks during vkDestroyInstance and
    // writes to this counter, so it must outlive the instance. The instance is
    // declared before the objects created from it (device, pool, messenger), so
    // those are torn down first.
    std::shared_ptr<std::atomic<std::uint32_t>> validationCount_;
    Unique<::vk::Instance> instance_;
    Unique<::vk::DebugUtilsMessengerEXT> messenger_;
    Unique<::vk::Device> device_;
    Unique<::vk::CommandPool> commandPool_;
    ::vk::PhysicalDevice physicalDevice_{};
    ::vk::Queue queue_{};
    std::uint32_t queueFamilyIndex_{0};
    bool smoothLines_{false};
    std::thread::id ownerThread_{};
};

} // namespace iv::vk

#endif // IV_VK_CONTEXT_HPP
