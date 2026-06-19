#include "iv/vk/context.hpp"
#include "iv/vk/unique.hpp"

#include "catch_amalgamated.hpp"

#include <cstdlib>
#include <type_traits>

namespace vkh = ::vk;

// Ownership contract (ADR-0004): wrappers and the context are move-only.
static_assert(!std::is_copy_constructible_v<iv::vk::Context>);
static_assert(std::is_move_constructible_v<iv::vk::Context>);
static_assert(!std::is_copy_constructible_v<iv::vk::Unique<vkh::Instance>>);
static_assert(std::is_move_constructible_v<iv::vk::Unique<vkh::Instance>>);

// teeth: catches failure to create instance/device/queue/pool, or selecting a
// queue family that lacks graphics support.
TEST_CASE("Context::create brings up a usable Vulkan device", "[vk][context]") {
    auto ctx = iv::vk::Context::create();
    REQUIRE(ctx.has_value());
    CHECK(static_cast<bool>(ctx->instance()));
    CHECK(static_cast<bool>(ctx->physicalDevice()));
    CHECK(static_cast<bool>(ctx->device()));
    CHECK(static_cast<bool>(ctx->queue()));
    CHECK(static_cast<bool>(ctx->commandPool()));

    const auto families = ctx->physicalDevice().getQueueFamilyProperties();
    REQUIRE(ctx->queueFamilyIndex() < families.size());
    CHECK(static_cast<bool>(families[ctx->queueFamilyIndex()].queueFlags
                            & vkh::QueueFlagBits::eGraphics));
}

// teeth: catches validation errors emitted during a clean bring-up (a wrong
// create-info, bad layout/usage, or leaked object would make this nonzero).
TEST_CASE("Context bring-up is validation-clean", "[vk][context]") {
    auto ctx = iv::vk::Context::create();
    REQUIRE(ctx.has_value());
    CHECK(ctx->validationClean());
}

// teeth: catches a broken out-of-range guard on the device-index override.
TEST_CASE("IV_VULKAN_DEVICE_INDEX out of range -> device_unavailable", "[vk][context]") {
    ::setenv("IV_VULKAN_DEVICE_INDEX", "9999", 1);
    auto ctx = iv::vk::Context::create();
    ::unsetenv("IV_VULKAN_DEVICE_INDEX");
    REQUIRE_FALSE(ctx.has_value());
    CHECK(ctx.error().code == iv::Errc::device_unavailable);
}

// teeth: catches a Vulkan OBJECT leaked at teardown (e.g. an undestroyed command
// pool or device). The instance's pNext debug messenger reports undestroyed
// objects during vkDestroyInstance; we read the shared validation counter AFTER
// the Context is gone. (Only meaningful in Debug, where validation is active.)
TEST_CASE("Context teardown leaks no Vulkan objects", "[vk][context]") {
    std::shared_ptr<const std::atomic<std::uint32_t>> counter;
    {
        auto ctx = iv::vk::Context::create();
        REQUIRE(ctx.has_value());
        counter = ctx->validationCounter();
    } // Context destroyed here; pNext messenger validates the teardown.
    if (counter) {
        CHECK(counter->load() == 0u);
    }
}
