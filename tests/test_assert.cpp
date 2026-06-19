#include "iv/assert.hpp"

#include "catch_amalgamated.hpp"

#include <string>

namespace {

// A capturing handler installed in place of the aborting default, so IV_ASSERT
// can be exercised without killing the test process (ADR-0003, ADR-0002 §4).
struct Capture {
    bool fired = false;
    int count = 0;
    std::string expr;
    std::string msg;
    int line = 0;
};

Capture g_capture;

void capturing_handler(const char* /*file*/, int line, const char* expr,
                       const char* msg) {
    g_capture.fired = true;
    ++g_capture.count;
    g_capture.expr = (expr != nullptr ? expr : "");
    g_capture.msg = (msg != nullptr ? msg : "");
    g_capture.line = line;
}

// RAII: install the capturing handler (resetting capture), restore on exit.
struct HandlerGuard {
    iv::detail::AssertHandler previous;
    HandlerGuard() : previous(iv::detail::set_assert_handler(&capturing_handler)) {
        g_capture = Capture{};
    }
    ~HandlerGuard() { iv::detail::set_assert_handler(previous); }
};

} // namespace

// teeth: catches IV_ASSERT not firing on a false condition, or passing the
// wrong stringized expression / message to the handler.
TEST_CASE("IV_ASSERT invokes the handler on a false condition", "[assert]") {
    HandlerGuard guard;
    const int x = 1;
    IV_ASSERT(x == 2, "x must be two");
    CHECK(g_capture.fired);
    CHECK(g_capture.count == 1);
    CHECK(g_capture.expr == "x == 2");
    CHECK(g_capture.msg == "x must be two");
}

// teeth: catches IV_ASSERT firing on a TRUE condition (a false positive that
// would abort correct programs).
TEST_CASE("IV_ASSERT does not fire on a true condition", "[assert]") {
    HandlerGuard guard;
    const int x = 2;
    IV_ASSERT(x == 2, "x must be two");
    CHECK_FALSE(g_capture.fired);
    CHECK(g_capture.count == 0);
}

// teeth: catches the macro evaluating its condition more than once (side-effect
// safety) or zero times.
TEST_CASE("IV_ASSERT evaluates its condition exactly once", "[assert]") {
    HandlerGuard guard;
    int calls = 0;
    auto cond = [&calls]() {
        ++calls;
        return false;
    };
    IV_ASSERT(cond(), "always false");
    CHECK(calls == 1);
    CHECK(g_capture.fired);
}

// teeth: catches set_assert_handler not returning the previously installed
// handler (which would break save/restore discipline).
TEST_CASE("set_assert_handler returns the previous handler", "[assert]") {
    const auto original = iv::detail::get_assert_handler();
    const auto prev = iv::detail::set_assert_handler(&capturing_handler);
    CHECK(prev == original);
    const auto prev2 = iv::detail::set_assert_handler(original);
    CHECK(prev2 == &capturing_handler);
    CHECK(iv::detail::get_assert_handler() == original);
}
