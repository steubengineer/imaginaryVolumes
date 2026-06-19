#include "iv/vk/context.hpp"

#include "iv/assert.hpp"
#include "iv/vk/result.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace iv::vk {
namespace {

namespace vkh = ::vk; // Vulkan-Hpp

constexpr std::uint32_t kApiVersion = VK_API_VERSION_1_3;
constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

#ifndef NDEBUG
constexpr bool kDebugBuild = true;
#else
constexpr bool kDebugBuild = false;
#endif

// Validation message sink: counts WARNING/ERROR messages and echoes them.
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*types*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData) {
    const auto interesting = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                             | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    if ((severity & static_cast<VkDebugUtilsMessageSeverityFlagBitsEXT>(interesting)) != 0) {
        if (userData != nullptr) {
            static_cast<std::atomic<std::uint32_t>*>(userData)->fetch_add(
                1, std::memory_order_relaxed);
        }
        std::fprintf(stderr, "[validation] %s\n",
                     (data != nullptr && data->pMessage != nullptr) ? data->pMessage : "");
    }
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT makeMessengerInfo(void* userData) {
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                           | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                       | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                       | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = &debugCallback;
    info.pUserData = userData;
    return info;
}

bool hasLayer(const std::vector<vkh::LayerProperties>& layers, std::string_view name) {
    return std::any_of(layers.begin(), layers.end(), [&](const vkh::LayerProperties& l) {
        return std::string_view(l.layerName.data()) == name;
    });
}

bool hasExtension(const std::vector<vkh::ExtensionProperties>& exts, std::string_view name) {
    return std::any_of(exts.begin(), exts.end(), [&](const vkh::ExtensionProperties& e) {
        return std::string_view(e.extensionName.data()) == name;
    });
}

int deviceTypeScore(vkh::PhysicalDeviceType type) {
    switch (type) {
    case vkh::PhysicalDeviceType::eDiscreteGpu:   return 4;
    case vkh::PhysicalDeviceType::eIntegratedGpu: return 3;
    case vkh::PhysicalDeviceType::eVirtualGpu:    return 2;
    case vkh::PhysicalDeviceType::eCpu:           return 1;
    default:                                       return 0;
    }
}

std::optional<std::uint32_t> graphicsFamily(vkh::PhysicalDevice pd) {
    const auto families = pd.getQueueFamilyProperties();
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(families.size()); ++i) {
        if (families[i].queueFlags & vkh::QueueFlagBits::eGraphics) {
            return i;
        }
    }
    return std::nullopt;
}

std::uint64_t deviceLocalMemory(vkh::PhysicalDevice pd) {
    const auto mem = pd.getMemoryProperties();
    std::uint64_t best = 0;
    for (std::uint32_t i = 0; i < mem.memoryHeapCount; ++i) {
        if (mem.memoryHeaps[i].flags & vkh::MemoryHeapFlagBits::eDeviceLocal) {
            best = std::max(best, static_cast<std::uint64_t>(mem.memoryHeaps[i].size));
        }
    }
    return best;
}

// Returns the IV_VULKAN_DEVICE_INDEX override if set. An unparseable value
// yields a deliberately out-of-range sentinel so selection fails cleanly.
std::optional<std::uint32_t> envDeviceIndex() {
    const char* raw = std::getenv("IV_VULKAN_DEVICE_INDEX");
    if (raw == nullptr || raw[0] == '\0') {
        return std::nullopt;
    }
    const std::string_view sv{raw};
    std::uint32_t value = 0;
    const auto res = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    if (res.ec != std::errc{} || res.ptr != sv.data() + sv.size()) {
        return std::optional<std::uint32_t>{0xFFFF'FFFFu};
    }
    return value;
}

bool deviceUsable(vkh::PhysicalDevice pd) {
    return pd.getProperties().apiVersion >= kApiVersion && graphicsFamily(pd).has_value();
}

bool supportsDeviceExtensions(vkh::PhysicalDevice pd, const std::vector<const char*>& exts) {
    if (exts.empty()) {
        return true;
    }
    const auto r = pd.enumerateDeviceExtensionProperties();
    if (r.result != vkh::Result::eSuccess) {
        return false;
    }
    for (const char* want : exts) {
        if (!hasExtension(r.value, want)) {
            return false;
        }
    }
    return true;
}

} // namespace

void Context::checkAffinity() const noexcept {
    IV_DEBUG_ASSERT(std::this_thread::get_id() == ownerThread_,
                    "iv::vk::Context used off its creating thread (ADR-0007)");
}

Result<Context> Context::create() {
    return create(ContextConfig{});
}

Result<Context> Context::create(const ContextConfig& config) {
    // 1. Instance (+ validation layer/debug-utils in Debug, best-effort).
    std::vector<const char*> layers;
    std::vector<const char*> extensions;
    bool validationEnabled = false;

    if constexpr (kDebugBuild) {
        auto layerProps = take(vkh::enumerateInstanceLayerProperties(),
                               "enumerateInstanceLayerProperties");
        auto extProps = take(vkh::enumerateInstanceExtensionProperties(nullptr),
                             "enumerateInstanceExtensionProperties");
        if (layerProps && extProps && hasLayer(*layerProps, kValidationLayer)
            && hasExtension(*extProps, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            layers.push_back(kValidationLayer);
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            validationEnabled = true;
        } else {
            std::fprintf(stderr,
                         "[iv] validation unavailable; continuing without it (ADR-0005)\n");
        }
    }

    // Extra instance extensions requested by the caller (e.g. GLFW surface extensions
    // for presentation; ADR-0016).
    for (const char* ext : config.instanceExtensions) {
        extensions.push_back(ext);
    }

    // The validation counter and the pNext create/destroy messenger must exist
    // BEFORE the instance: the loader uses the pNext messenger during both
    // vkCreateInstance and vkDestroyInstance, and the counter it writes to must
    // outlive the instance (see member order in context.hpp).
    std::shared_ptr<std::atomic<std::uint32_t>> validationCount;
    VkDebugUtilsMessengerCreateInfoEXT pnextMessenger{};
    if (validationEnabled) {
        validationCount = std::make_shared<std::atomic<std::uint32_t>>(0u);
        pnextMessenger = makeMessengerInfo(validationCount.get());
    }

    const vkh::ApplicationInfo appInfo{"imaginaryVolumes", 0, "imaginaryVolumes", 0,
                                       kApiVersion};
    auto instanceInfo = vkh::InstanceCreateInfo{}
                            .setPApplicationInfo(&appInfo)
                            .setPEnabledLayerNames(layers)
                            .setPEnabledExtensionNames(extensions);
    if (validationEnabled) {
        instanceInfo.setPNext(&pnextMessenger);
    }
    auto instanceR = take(vkh::createInstance(instanceInfo), "createInstance");
    if (!instanceR) {
        return std::unexpected(std::move(instanceR).error());
    }
    const vkh::Instance instance = *instanceR;

    Context ctx;
    ctx.validationCount_ = validationCount; // shared; null when validation is off
    ctx.instance_ = Unique<vkh::Instance>(instance, [](vkh::Instance i) { i.destroy(); });

    // 2. Standalone messenger for the instance's normal lifetime (the pNext one
    //    above is used by the loader only during create/destroy). Procs are loaded
    //    by hand to keep default dispatch (ADR-0004).
    if (validationEnabled) {
        auto createFn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            instance.getProcAddr("vkCreateDebugUtilsMessengerEXT"));
        auto destroyFn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            instance.getProcAddr("vkDestroyDebugUtilsMessengerEXT"));
        if (createFn != nullptr && destroyFn != nullptr) {
            auto mci = makeMessengerInfo(validationCount.get());
            const VkInstance cInstance = static_cast<VkInstance>(instance);
            VkDebugUtilsMessengerEXT rawMessenger = VK_NULL_HANDLE;
            if (createFn(cInstance, &mci, nullptr, &rawMessenger) == VK_SUCCESS
                && rawMessenger != VK_NULL_HANDLE) {
                ctx.messenger_ = Unique<vkh::DebugUtilsMessengerEXT>(
                    vkh::DebugUtilsMessengerEXT(rawMessenger),
                    [cInstance, destroyFn](vkh::DebugUtilsMessengerEXT m) {
                        destroyFn(cInstance, static_cast<VkDebugUtilsMessengerEXT>(m), nullptr);
                    });
            }
        }
    }

    // 3-4. Select a physical device (accept software; ADR-0005).
    auto devicesR = take(instance.enumeratePhysicalDevices(), "enumeratePhysicalDevices");
    if (!devicesR) {
        return std::unexpected(std::move(devicesR).error());
    }
    const std::vector<vkh::PhysicalDevice>& devices = *devicesR;
    if (devices.empty()) {
        return make_error(Errc::device_unavailable, "no Vulkan physical devices present");
    }

    std::optional<std::uint32_t> chosen;
    if (const auto forced = envDeviceIndex()) {
        if (*forced < static_cast<std::uint32_t>(devices.size())
            && deviceUsable(devices[*forced])
            && supportsDeviceExtensions(devices[*forced], config.deviceExtensions)) {
            chosen = *forced;
        } else {
            return make_error(Errc::device_unavailable,
                              "IV_VULKAN_DEVICE_INDEX is out of range or unusable");
        }
    } else {
        int bestScore = -1;
        std::uint64_t bestMem = 0;
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(devices.size()); ++i) {
            if (!deviceUsable(devices[i])
                || !supportsDeviceExtensions(devices[i], config.deviceExtensions)) {
                continue;
            }
            const int score = deviceTypeScore(devices[i].getProperties().deviceType);
            const std::uint64_t mem = deviceLocalMemory(devices[i]);
            if (score > bestScore || (score == bestScore && mem > bestMem)) {
                bestScore = score;
                bestMem = mem;
                chosen = i;
            }
        }
        if (!chosen) {
            return make_error(Errc::device_unavailable,
                              "no device with Vulkan 1.3 and a graphics queue family");
        }
    }

    const vkh::PhysicalDevice phys = devices[*chosen];
    const std::uint32_t family = *graphicsFamily(phys);

    // 5. Logical device with one graphics queue.
    const float priority = 1.0f;
    const auto queueInfo = vkh::DeviceQueueCreateInfo{}
                               .setQueueFamilyIndex(family)
                               .setQueuePriorities(priority);
    const auto deviceInfo = vkh::DeviceCreateInfo{}
                                .setQueueCreateInfos(queueInfo)
                                .setPEnabledExtensionNames(config.deviceExtensions);
    auto deviceR = take(phys.createDevice(deviceInfo), "createDevice");
    if (!deviceR) {
        return std::unexpected(std::move(deviceR).error());
    }
    const vkh::Device device = *deviceR;
    ctx.device_ = Unique<vkh::Device>(device, [](vkh::Device d) { d.destroy(); });

    const vkh::Queue queue = device.getQueue(family, 0);

    // 6. Command pool (resettable buffers).
    const auto poolInfo = vkh::CommandPoolCreateInfo{}
                              .setFlags(vkh::CommandPoolCreateFlagBits::eResetCommandBuffer)
                              .setQueueFamilyIndex(family);
    auto poolR = take(device.createCommandPool(poolInfo), "createCommandPool");
    if (!poolR) {
        return std::unexpected(std::move(poolR).error());
    }
    ctx.commandPool_ = Unique<vkh::CommandPool>(
        *poolR, [device](vkh::CommandPool p) { device.destroyCommandPool(p); });

    ctx.physicalDevice_ = phys;
    ctx.queue_ = queue;
    ctx.queueFamilyIndex_ = family;
    ctx.ownerThread_ = std::this_thread::get_id();
    return ctx;
}

} // namespace iv::vk
