#include "iv/assert.hpp"
#include "iv/vk/context.hpp"

#include "catch_amalgamated.hpp"

#include <atomic>
#include <thread>
#include <vector>

namespace {
std::atomic<int> g_fireCount{0};
void countingHandler(const char*, int, const char*, const char*) {
    g_fireCount.fetch_add(1, std::memory_order_relaxed);
}
void noopHandler(const char*, int, const char*, const char*) {}
} // namespace

// teeth (TSan): the assert-handler storage is std::atomic, so concurrent set/get
// is race-free. Under -DIV_SANITIZE=thread, replacing the atomic with a plain
// pointer makes TSan report a data race in this test. Runs enough iterations
// across threads to give a real race a real chance (§2.4).
TEST_CASE("assert handler set/get is race-free under concurrency", "[concurrency]") {
    const auto original = iv::detail::get_assert_handler();
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&] {
            while (!go.load(std::memory_order_acquire)) {
            }
            for (int k = 0; k < 20000; ++k) {
                const auto prev = iv::detail::set_assert_handler(&noopHandler);
                (void)iv::detail::get_assert_handler();
                iv::detail::set_assert_handler(prev);
            }
        });
    }
    go.store(true, std::memory_order_release);
    for (auto& t : threads) {
        t.join();
    }
    iv::detail::set_assert_handler(original); // restore
    CHECK(iv::detail::get_assert_handler() == original);
}

// teeth: in Debug, using a Context off its creating thread trips the thread-
// affinity IV_DEBUG_ASSERT (ADR-0007). Removing the affinity check makes the
// handler not fire, turning this red. (Compiled out in Release.)
TEST_CASE("Context accessor asserts thread-affinity in Debug", "[vk][concurrency]") {
    auto ctx = iv::vk::Context::create();
    REQUIRE(ctx.has_value());

    const auto original = iv::detail::set_assert_handler(&countingHandler);
    g_fireCount.store(0, std::memory_order_relaxed);

    std::thread other([&] {
        (void)ctx->device(); // off-thread use -> IV_DEBUG_ASSERT in Debug
    });
    other.join();

    iv::detail::set_assert_handler(original); // restore

#ifndef NDEBUG
    CHECK(g_fireCount.load(std::memory_order_relaxed) >= 1);
#else
    CHECK(true); // affinity check is compiled out in Release
#endif
}
