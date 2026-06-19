#include "iv/assert.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace iv::detail {

void default_assert_handler(const char* file, int line, const char* expr,
                            const char* msg) noexcept {
    std::fprintf(stderr,
                 "IV_ASSERT failed: %s\n  at %s:%d\n  expr: %s\n",
                 (msg != nullptr ? msg : ""), file, line, expr);
    std::fflush(stderr);
    std::abort();
}

namespace {
// The installed handler. Atomic so installing/reading is race-free even before
// the project's broader concurrency contract is set (M2).
std::atomic<AssertHandler> g_handler{&default_assert_handler};
} // namespace

AssertHandler set_assert_handler(AssertHandler handler) noexcept {
    if (handler == nullptr) {
        handler = &default_assert_handler;
    }
    return g_handler.exchange(handler, std::memory_order_acq_rel);
}

AssertHandler get_assert_handler() noexcept {
    return g_handler.load(std::memory_order_acquire);
}

void invoke_assert_handler(const char* file, int line, const char* expr,
                           const char* msg) {
    const AssertHandler handler = g_handler.load(std::memory_order_acquire);
    handler(file, line, expr, msg);
}

} // namespace iv::detail
