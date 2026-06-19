#include "iv/vk/context.hpp"
#include "iv/vk/offscreen.hpp"

#include "catch_amalgamated.hpp"

#include <array>
#include <cstdint>

using iv::vk::clearAndReadback;
using iv::vk::Context;

namespace {
// Clear components chosen as k/255 so the expected UNORM byte is exactly k
// (ADR-0006), avoiding any tie-rounding ambiguity.
constexpr std::array<float, 4> kColor{64.0f / 255.0f, 128.0f / 255.0f, 192.0f / 255.0f,
                                      255.0f / 255.0f};
} // namespace

// teeth: catches a wrong clear color, swapped channel order, or a broken
// (x,y)->byte-offset formula — every pixel must equal the rounded UNORM bytes.
TEST_CASE("clearAndReadback fills the whole image with the clear color", "[vk][offscreen]") {
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());

    const std::uint32_t w = 8;
    const std::uint32_t h = 4;
    auto rb = clearAndReadback(*ctx, w, h, kColor);
    REQUIRE(rb.has_value());
    CHECK(rb->width() == w);
    CHECK(rb->height() == h);
    CHECK(rb->bytes().size() == static_cast<std::size_t>(w) * h * 4u);

    bool allMatch = true;
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const auto p = rb->at(x, y);
            if (p.r != 64 || p.g != 128 || p.b != 192 || p.a != 255) {
                allMatch = false;
            }
        }
    }
    CHECK(allMatch);
    CHECK(ctx->validationClean());
}

// teeth: directly exercises the (x,y)->byte-offset mapping with spatially-varying
// data. The GPU uniform-clear tests can't catch an offset/stride bug (every pixel
// is identical); here a transposed formula or wrong stride goes red. Non-square
// dimensions make transposition observable.
TEST_CASE("ImageReadback::at maps (x,y) to the ADR-0006 byte offset", "[offscreen]") {
    const std::uint32_t w = 3;
    const std::uint32_t h = 2;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(w) * h * 4u);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::size_t o = (static_cast<std::size_t>(y) * w + x) * 4u;
            bytes[o + 0] = static_cast<std::uint8_t>(10u * x + y); // distinct per pixel
            bytes[o + 1] = static_cast<std::uint8_t>(100u + x);
            bytes[o + 2] = static_cast<std::uint8_t>(200u + y);
            bytes[o + 3] = 255u;
        }
    }
    const iv::vk::ImageReadback rb(w, h, std::move(bytes));
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const auto p = rb.at(x, y);
            CHECK(static_cast<int>(p.r) == static_cast<int>(10u * x + y));
            CHECK(static_cast<int>(p.g) == static_cast<int>(100u + x));
            CHECK(static_cast<int>(p.b) == static_cast<int>(200u + y));
            CHECK(static_cast<int>(p.a) == 255);
        }
    }
}

// teeth: catches a clear color hardcoded to the first test's value — a second,
// distinct color must also round-trip.
TEST_CASE("clearAndReadback is not hardcoded to one color", "[vk][offscreen]") {
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());

    auto rb = clearAndReadback(*ctx, 2, 2, {0.0f, 51.0f / 255.0f, 0.0f, 1.0f});
    REQUIRE(rb.has_value());
    const auto p = rb->at(1, 1);
    CHECK(static_cast<int>(p.r) == 0);
    CHECK(static_cast<int>(p.g) == 51);
    CHECK(static_cast<int>(p.b) == 0);
    CHECK(static_cast<int>(p.a) == 255);
}

// teeth: catches nondeterminism (ADR-0007) — two identical runs must match byte
// for byte.
TEST_CASE("clearAndReadback is deterministic across runs", "[vk][offscreen]") {
    auto ctx = Context::create();
    REQUIRE(ctx.has_value());

    auto a = clearAndReadback(*ctx, 4, 4, kColor);
    auto b = clearAndReadback(*ctx, 4, 4, kColor);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->bytes() == b->bytes());
}
