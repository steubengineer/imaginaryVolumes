#include "iv/error.hpp"

#include "catch_amalgamated.hpp"

using iv::Errc;
using iv::Error;
using iv::Result;
using iv::Status;

// teeth: catches a wrong Errc->name mapping (renamed, swapped, or dropped case).
TEST_CASE("to_string maps each Errc to its stable name", "[error]") {
    CHECK(iv::to_string(Errc::invalid_argument) == "invalid_argument");
    CHECK(iv::to_string(Errc::unsupported_configuration) == "unsupported_configuration");
    CHECK(iv::to_string(Errc::device_unavailable) == "device_unavailable");
    CHECK(iv::to_string(Errc::allocation_failed) == "allocation_failed");
    CHECK(iv::to_string(Errc::internal) == "internal");
}

// teeth: catches a broken format() (missing separator, dropped message, wrong code).
TEST_CASE("format renders the code and an optional message", "[error]") {
    CHECK(iv::format(Error{Errc::internal, ""}) == "internal");
    CHECK(iv::format(Error{Errc::invalid_argument, "nx must be > 0"})
          == "invalid_argument: nx must be > 0");
}

// teeth: catches make_error producing the wrong code/message, or Result not
// carrying the error state through std::expected.
TEST_CASE("make_error yields an error-state Result with the given code", "[error]") {
    Result<int> r = iv::make_error(Errc::device_unavailable, "no GPU");
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == Errc::device_unavailable);
    CHECK(r.error().message == "no GPU");
}

// teeth: catches Result<T> failing to carry a success value.
TEST_CASE("Result carries a success value", "[error]") {
    Result<int> r = 42;
    REQUIRE(r.has_value());
    CHECK(*r == 42);
}

// teeth: catches make_error failing to convert into a void Status.
TEST_CASE("Status carries an error from make_error", "[error]") {
    Status s = iv::make_error(Errc::allocation_failed, "oom");
    REQUIRE_FALSE(s.has_value());
    CHECK(s.error().code == Errc::allocation_failed);
}
