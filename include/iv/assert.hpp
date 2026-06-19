#ifndef IV_ASSERT_HPP
#define IV_ASSERT_HPP

// Assertion macros and the overridable failure handler (ADR-0003).
//
// IV_ASSERT is ALWAYS active (Debug and Release): a failed condition is a
// well-defined, deterministic event (it invokes the current handler, which by
// default aborts), never undefined behavior. The handler is overridable so
// tests can intercept assertion failures without killing the process.
//
// IV_DEBUG_ASSERT is active only in Debug (compiled out under NDEBUG) and is for
// hot-path internal checks; it is NEVER used for public-API preconditions.

namespace iv::detail {

// Signature of an assertion-failure handler.
using AssertHandler = void (*)(const char* file, int line, const char* expr,
                               const char* msg);

// Default handler: reports to stderr and aborts. Never returns.
[[noreturn]] void default_assert_handler(const char* file, int line,
                                         const char* expr,
                                         const char* msg) noexcept;

// Install `handler` (nullptr restores the default). Returns the previous
// handler. Atomic; safe to call from any thread.
AssertHandler set_assert_handler(AssertHandler handler) noexcept;

// The currently installed handler.
AssertHandler get_assert_handler() noexcept;

// Invoked by IV_ASSERT on a failed condition. Calls the current handler. Not
// [[noreturn]]: a test handler may choose to return.
void invoke_assert_handler(const char* file, int line, const char* expr,
                           const char* msg);

} // namespace iv::detail

// Always-on contract assertion. The condition is evaluated exactly once; the
// message is evaluated only on failure.
#define IV_ASSERT(cond, msg)                                                    \
    do {                                                                        \
        if (!(cond)) [[unlikely]] {                                             \
            ::iv::detail::invoke_assert_handler(__FILE__, __LINE__, #cond,      \
                                                (msg));                         \
        }                                                                       \
    } while (false)

#ifdef NDEBUG
#define IV_DEBUG_ASSERT(cond, msg) ((void)0)
#else
#define IV_DEBUG_ASSERT(cond, msg) IV_ASSERT(cond, msg)
#endif

#endif // IV_ASSERT_HPP
