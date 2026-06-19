#ifndef IV_VK_UNIQUE_HPP
#define IV_VK_UNIQUE_HPP

// Move-only, single-owner RAII wrapper for a Vulkan handle (ADR-0004). We define
// our own (rather than vk::raii, whose constructors signal failure via
// exceptions). The deleter is type-erased so one template serves every handle
// type; it captures whatever parent/dispatch it needs to destroy the object.
// Setup code is not hot-path, so the std::function indirection is acceptable.

#include <functional>
#include <utility>

namespace iv::vk {

template <class Handle>
class Unique {
public:
    Unique() noexcept = default;
    Unique(Handle handle, std::function<void(Handle)> deleter) noexcept
        : handle_(handle), deleter_(std::move(deleter)) {}

    Unique(const Unique&) = delete;
    Unique& operator=(const Unique&) = delete;

    Unique(Unique&& other) noexcept
        : handle_(std::exchange(other.handle_, Handle{})),
          deleter_(std::move(other.deleter_)) {
        other.deleter_ = nullptr;
    }
    Unique& operator=(Unique&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, Handle{});
            deleter_ = std::move(other.deleter_);
            other.deleter_ = nullptr;
        }
        return *this;
    }

    ~Unique() { reset(); }

    // Destroys the owned object (if any) and becomes null.
    void reset() noexcept {
        if (static_cast<bool>(handle_) && deleter_) {
            deleter_(handle_);
        }
        handle_ = Handle{};
        deleter_ = nullptr;
    }

    [[nodiscard]] Handle get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(handle_);
    }

private:
    Handle handle_{};
    std::function<void(Handle)> deleter_{};
};

} // namespace iv::vk

#endif // IV_VK_UNIQUE_HPP
