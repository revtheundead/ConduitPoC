// SPDX-License-Identifier: MIT
// Conduit - Error Unit Tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/core/error.hpp>
#include <chrono>
#include <thread>

using namespace conduit;

TEST_CASE("Error construction", "[error]") {
    Error err(ErrorCode::BufferUnderrun, "test message");

    CHECK(err.code() == ErrorCode::BufferUnderrun);
    CHECK(err.message() == "test message");
}

TEST_CASE("Error category checks", "[error]") {
    SECTION("Decode errors") {
        Error err(ErrorCode::BufferUnderrun, "");
        CHECK(err.is_decode_error());
        CHECK_FALSE(err.is_encode_error());
        CHECK_FALSE(err.is_connection_error());
        CHECK_FALSE(err.is_transceiver_error());
    }

    SECTION("Encode errors") {
        Error err(ErrorCode::MissingRequiredField, "");
        CHECK_FALSE(err.is_decode_error());
        CHECK(err.is_encode_error());
    }

    SECTION("Connection errors") {
        Error err(ErrorCode::ConnectionRefused, "");
        CHECK(err.is_connection_error());
        CHECK(err.is_recoverable());
    }

    SECTION("Transceiver errors") {
        Error err(ErrorCode::NotRunning, "");
        CHECK(err.is_transceiver_error());
    }

    SECTION("BatchNotSupported is transceiver error") {
        Error err(ErrorCode::BatchNotSupported, "");
        CHECK(err.is_transceiver_error());
        CHECK(!err.is_decode_error());
        CHECK(!err.is_encode_error());
    }
}

TEST_CASE("Error severity", "[error]") {
    CHECK(Error(ErrorCode::BufferUnderrun, "").severity() == ErrorSeverity::Warning);
    CHECK(ErrorCode::ConnectionRefused == Error(ErrorCode::ConnectionRefused, "").code());
    CHECK(Error(ErrorCode::ConnectionRefused, "").severity() == ErrorSeverity::Error);
    CHECK(Error(ErrorCode::InternalError, "").severity() == ErrorSeverity::Critical);
    // Timeout is a special case: 900-range but Error severity (recoverable)
    CHECK(Error(ErrorCode::Timeout, "").severity() == ErrorSeverity::Error);
}

TEST_CASE("Error code to string", "[error]") {
    CHECK(Error::code_to_string(ErrorCode::BufferUnderrun) == "BUFFER_UNDERRUN");
    CHECK(Error::code_to_string(ErrorCode::ConnectionRefused) == "CONNECTION_REFUSED");
    CHECK(Error::code_to_string(ErrorCode::NotRunning) == "NOT_RUNNING");
    CHECK(Error::code_to_string(ErrorCode::BatchNotSupported) == "BATCH_NOT_SUPPORTED");
    CHECK(Error::code_to_string(ErrorCode::InternalError) == "INTERNAL_ERROR");
}

TEST_CASE("Error format_short", "[error]") {
    Error err(ErrorCode::BufferUnderrun, "not enough data");
    auto s = err.format_short();
    CHECK(s.find("BUFFER_UNDERRUN") != std::string::npos);
    CHECK(s.find("not enough data") != std::string::npos);
}

TEST_CASE("Result<T> usage", "[error]") {
    SECTION("Success") {
        Result<int> r = 42;
        REQUIRE(r.has_value());
        CHECK(*r == 42);
    }

    SECTION("Failure") {
        Result<int> r = std::unexpected(
            Error(ErrorCode::BufferUnderrun, "oops"));
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().code() == ErrorCode::BufferUnderrun);
    }
}

TEST_CASE("VoidResult usage", "[error]") {
    SECTION("Success") {
        VoidResult r{};
        CHECK(r.has_value());
    }

    SECTION("Failure") {
        VoidResult r = std::unexpected(
            Error(ErrorCode::InvalidConfig, "bad config"));
        CHECK_FALSE(r.has_value());
        CHECK(r.error().code() == ErrorCode::InvalidConfig);
    }
}

// Helper for TRY macro tests
static Result<int> succeed() { return 42; }
static Result<int> fail_result() {
    return std::unexpected(Error(ErrorCode::DecodingFailed, "decode failed"));
}

static Result<int> test_try_success() {
    CONDUIT_TRY_ASSIGN(int, val, succeed());
    return val + 1;
}

static Result<int> test_try_failure() {
    CONDUIT_TRY_ASSIGN(int, val, fail_result());
    return val + 1;  // Should not reach here
}

TEST_CASE("CONDUIT_TRY_ASSIGN macro", "[error]") {
    SECTION("Propagates success") {
        auto r = test_try_success();
        REQUIRE(r.has_value());
        CHECK(*r == 43);
    }

    SECTION("Propagates failure") {
        auto r = test_try_failure();
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().code() == ErrorCode::DecodingFailed);
    }
}

static VoidResult test_ensure_pass() {
    CONDUIT_ENSURE(true, ErrorCode::InvalidArgument, "should not fire");
    return {};
}

static VoidResult test_ensure_fail() {
    CONDUIT_ENSURE(false, ErrorCode::InvalidArgument, "condition failed");
    return {};
}

TEST_CASE("CONDUIT_ENSURE macro", "[error]") {
    CHECK(test_ensure_pass().has_value());

    auto r = test_ensure_fail();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("Error context", "[error]") {
    SECTION("No context by default") {
        Error err(ErrorCode::BufferUnderrun, "test");
        CHECK_FALSE(err.has_context());
        CHECK(err.context().empty());
    }

    SECTION("with_context sets context") {
        Error err(ErrorCode::BufferUnderrun, "not enough data");
        auto err2 = err.with_context("field 'sync'");
        CHECK(err2.has_context());
        CHECK(err2.context() == "field 'sync'");
        CHECK(err2.code() == ErrorCode::BufferUnderrun);
        CHECK(err2.message() == "not enough data");
    }

    SECTION("with_context chains (outer > inner)") {
        Error err(ErrorCode::DecodingFailed, "bad value");
        auto err2 = err.with_context("field 'data'");
        auto err3 = err2.with_context("struct 'Message'");
        CHECK(err3.context() == "struct 'Message' > field 'data'");
    }

    SECTION("format_short includes context") {
        Error err(ErrorCode::BufferUnderrun, "not enough data");
        auto err2 = err.with_context("field 'sync'");
        auto s = err2.format_short();
        CHECK(s.find("BUFFER_UNDERRUN") != std::string::npos);
        CHECK(s.find("not enough data") != std::string::npos);
        CHECK(s.find("field 'sync'") != std::string::npos);
    }

    SECTION("format includes context") {
        Error err(ErrorCode::BufferUnderrun, "not enough data");
        auto err2 = err.with_context("field 'sync'");
        auto s = err2.format();
        CHECK(s.find("field 'sync'") != std::string::npos);
    }

    SECTION("Original error unchanged by with_context") {
        Error err(ErrorCode::BufferUnderrun, "test");
        auto err2 = err.with_context("field 'x'");
        CHECK_FALSE(err.has_context());
        CHECK(err2.has_context());
    }
}

TEST_CASE("CONDUIT_TRY_CONTEXT macro", "[error]") {
    auto inner_fail = []() -> Result<int> {
        return std::unexpected(Error(ErrorCode::BufferUnderrun, "underrun"));
    };

    auto outer = [&]() -> Result<int> {
        CONDUIT_TRY_CONTEXT(inner_fail(), "field 'len'");
        return 0;
    };

    auto r = outer();
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code() == ErrorCode::BufferUnderrun);
    CHECK(r.error().context() == "field 'len'");
}

TEST_CASE("Error timestamp is eagerly set at construction", "[error]") {
    auto before = std::chrono::system_clock::now();
    Error err(ErrorCode::BufferUnderrun, "test");
    auto after = std::chrono::system_clock::now();

    auto ts = err.timestamp();
    CHECK(ts >= before);
    CHECK(ts <= after);
}

TEST_CASE("Error timestamp preserved by with_context", "[error]") {
    Error err(ErrorCode::BufferUnderrun, "test");
    auto ts1 = err.timestamp();

    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    auto err2 = err.with_context("ctx");
    auto ts2 = err2.timestamp();

    // with_context copies the Error, so timestamp should be preserved
    CHECK(ts1 == ts2);
}

TEST_CASE("Default-constructed Error has epoch timestamp", "[error]") {
    Error err;
    auto ts = err.timestamp();
    // Default Error has no timestamp set, should return epoch
    CHECK(ts.time_since_epoch().count() == 0);
}

TEST_CASE("Error operator== compares by code only", "[error]") {
    SECTION("Same code, different messages compare equal") {
        Error a(ErrorCode::BufferUnderrun, "message A");
        Error b(ErrorCode::BufferUnderrun, "message B");
        CHECK(a == b);
    }

    SECTION("Different codes compare unequal") {
        Error a(ErrorCode::BufferUnderrun, "same message");
        Error b(ErrorCode::DecodingFailed, "same message");
        CHECK_FALSE(a == b);
    }

    SECTION("Same code and message compare equal") {
        Error a(ErrorCode::InvalidConfig, "test");
        Error b(ErrorCode::InvalidConfig, "test");
        CHECK(a == b);
    }
}
