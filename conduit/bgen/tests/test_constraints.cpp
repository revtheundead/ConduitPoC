// SPDX-License-Identifier: MIT
// Bgen tests - Constraint and validation tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <conduit/core/error.hpp>
#include <cstdint>
#include <vector>

#include "constraints/messages.hpp"
#include "optional_constrained/messages.hpp"

// ============================================================================
// Section: Immediate constraints
// ============================================================================

TEST_CASE("constraint equals - valid decode", "[constraints]") {
    // Encode a valid message with magic=0xBEEF
    conduit::io::BitWriter w;
    w.write_u16(0xBEEF, conduit::io::Endian::Big); // magic
    w.write_u8(50);                                  // percent
    w.write_u16(100, conduit::io::Endian::Big);     // deferred-val
    w.write_u32(0x12345678, conduit::io::Endian::Big); // payload
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto data = std::move(*finish_result);

    auto decoded = constraints::ConstraintMsg::decode_bytes(data);
    REQUIRE(decoded.has_value());
    CHECK(decoded->magic() == 0xBEEF);
    CHECK(decoded->percent() == 50);
    CHECK(decoded->deferred_val() == 100);
    CHECK(decoded->payload() == 0x12345678);
}

TEST_CASE("constraint equals - invalid decode fails", "[constraints]") {
    conduit::io::BitWriter w;
    w.write_u16(0xDEAD, conduit::io::Endian::Big); // wrong magic
    w.write_u8(50);
    w.write_u16(100, conduit::io::Endian::Big);
    w.write_u32(0, conduit::io::Endian::Big);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto data = std::move(*finish_result);

    auto decoded = constraints::ConstraintMsg::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::ConstraintViolation);
}

TEST_CASE("constraint range - valid value passes", "[constraints]") {
    conduit::io::BitWriter w;
    w.write_u16(0xBEEF, conduit::io::Endian::Big);
    w.write_u8(0); // min valid percent
    w.write_u16(100, conduit::io::Endian::Big);
    w.write_u32(0, conduit::io::Endian::Big);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto data = std::move(*finish_result);

    auto decoded = constraints::ConstraintMsg::decode_bytes(data);
    REQUIRE(decoded.has_value());
    CHECK(decoded->percent() == 0);
}

TEST_CASE("constraint range - exceeds max fails", "[constraints]") {
    conduit::io::BitWriter w;
    w.write_u16(0xBEEF, conduit::io::Endian::Big);
    w.write_u8(101); // exceeds max 100
    w.write_u16(100, conduit::io::Endian::Big);
    w.write_u32(0, conduit::io::Endian::Big);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto data = std::move(*finish_result);

    auto decoded = constraints::ConstraintMsg::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::ConstraintViolation);
}

// ============================================================================
// Section: Deferred validation
// ============================================================================

TEST_CASE("deferred constraint - skipped during decode", "[constraints][deferred]") {
    // Deferred constraint allows invalid value through decode
    conduit::io::BitWriter w;
    w.write_u16(0xBEEF, conduit::io::Endian::Big);
    w.write_u8(50);
    w.write_u16(5, conduit::io::Endian::Big); // deferred-val=5, below min 10
    w.write_u32(0, conduit::io::Endian::Big);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto data = std::move(*finish_result);

    auto decoded = constraints::ConstraintMsg::decode_bytes(data);
    REQUIRE(decoded.has_value()); // decode should succeed
    CHECK(decoded->deferred_val() == 5);
}

TEST_CASE("deferred constraint - validate() catches violation", "[constraints][deferred]") {
    conduit::io::BitWriter w;
    w.write_u16(0xBEEF, conduit::io::Endian::Big);
    w.write_u8(50);
    w.write_u16(5, conduit::io::Endian::Big); // deferred-val=5, below min 10
    w.write_u32(0, conduit::io::Endian::Big);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto data = std::move(*finish_result);

    auto decoded = constraints::ConstraintMsg::decode_bytes(data);
    REQUIRE(decoded.has_value());

    auto result = decoded->validate();
    REQUIRE(!result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::ConstraintViolationDeferred);
}

TEST_CASE("deferred constraint - validate() passes for valid value", "[constraints][deferred]") {
    conduit::io::BitWriter w;
    w.write_u16(0xBEEF, conduit::io::Endian::Big);
    w.write_u8(50);
    w.write_u16(100, conduit::io::Endian::Big); // valid: 10 <= 100 <= 500
    w.write_u32(0, conduit::io::Endian::Big);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto data = std::move(*finish_result);

    auto decoded = constraints::ConstraintMsg::decode_bytes(data);
    REQUIRE(decoded.has_value());

    auto result = decoded->validate();
    CHECK(result.has_value());
}

// ============================================================================
// Section: Roundtrip
// ============================================================================

TEST_CASE("constraint message encode-decode roundtrip", "[constraints][roundtrip]") {
    constraints::ConstraintMsg msg;
    REQUIRE(msg.set_magic(0xBEEF).has_value());
    REQUIRE(msg.set_percent(75).has_value());
    msg.set_deferred_val(250);
    msg.set_payload(0xCAFEBABE);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = constraints::ConstraintMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->magic() == 0xBEEF);
    CHECK(decoded->percent() == 75);
    CHECK(decoded->deferred_val() == 250);
    CHECK(decoded->payload() == 0xCAFEBABE);
}

// ============================================================================
// Section: Wire-byte verification
// ============================================================================

TEST_CASE("ConstraintMsg wire bytes magic position", "[wire][constraints]") {
    constraints::ConstraintMsg msg;
    REQUIRE(msg.set_magic(0xBEEF).has_value());
    REQUIRE(msg.set_percent(50).has_value());
    msg.set_deferred_val(100);
    msg.set_payload(0x12345678);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // magic(2) + percent(1) + deferred_val(2) + payload(4) = 9 bytes
    REQUIRE(bytes.size() == 9);
    CHECK(bytes[0] == 0xBE);       // magic high
    CHECK(bytes[1] == 0xEF);       // magic low
    CHECK(bytes[2] == 50);         // percent
    CHECK(bytes[3] == 0x00);       // deferred_val high (100 = 0x0064)
    CHECK(bytes[4] == 0x64);       // deferred_val low
    CHECK(bytes[5] == 0x12);       // payload byte 0
    CHECK(bytes[6] == 0x34);       // payload byte 1
    CHECK(bytes[7] == 0x56);       // payload byte 2
    CHECK(bytes[8] == 0x78);       // payload byte 3
}

// ============================================================================
// Section: Edge-case tests
// ============================================================================

// ============================================================================
// Section: Encode constraint violations (I2 regression)
//
// The I2 bug was that optional fields skipped constraint checks on encode.
// No existing fixture has both an optional field AND a constraint on it, so
// we test the encode constraint violation path on non-optional fields here.
// The codegen fix (emit_encode_constraint_check for optional fields with
// "*member" dereference) is validated by the same error path.
// ============================================================================

TEST_CASE("encode_bytes rejects constraint violation - magic mismatch", "[constraints][encode]") {
    constraints::ConstraintMsg msg;

    // Setter rejects invalid magic
    auto set_result = msg.set_magic(0xDEAD);
    REQUIRE(!set_result.has_value());
    CHECK(set_result.error().code() == conduit::ErrorCode::EncodeConstraintViolation);

    // Bypass validation via mutable accessor to test encode rejection
    msg.mutable_magic() = 0xDEAD;
    REQUIRE(msg.set_percent(50).has_value());
    msg.set_deferred_val(100);
    msg.set_payload(0);

    auto result = msg.encode_bytes();
    REQUIRE(!result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

TEST_CASE("encode_bytes rejects constraint violation - percent exceeds max", "[constraints][encode]") {
    constraints::ConstraintMsg msg;
    REQUIRE(msg.set_magic(0xBEEF).has_value());

    // Setter rejects invalid percent
    auto set_result = msg.set_percent(101);
    REQUIRE(!set_result.has_value());
    CHECK(set_result.error().code() == conduit::ErrorCode::EncodeConstraintViolation);

    // Bypass validation via mutable accessor to test encode rejection
    msg.mutable_percent() = 101;
    msg.set_deferred_val(100);
    msg.set_payload(0);

    auto result = msg.encode_bytes();
    REQUIRE(!result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

TEST_CASE("encode_bytes rejects constraint violation - deferred_val below min", "[constraints][encode]") {
    constraints::ConstraintMsg msg;
    REQUIRE(msg.set_magic(0xBEEF).has_value());
    REQUIRE(msg.set_percent(50).has_value());
    msg.set_deferred_val(5);  // Below min 10 (encode checks both deferred and immediate)
    msg.set_payload(0);

    auto result = msg.encode_bytes();
    REQUIRE(!result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

TEST_CASE("encode_bytes rejects constraint violation - deferred_val exceeds max", "[constraints][encode]") {
    constraints::ConstraintMsg msg;
    REQUIRE(msg.set_magic(0xBEEF).has_value());
    REQUIRE(msg.set_percent(50).has_value());
    msg.set_deferred_val(501);  // Exceeds max 500
    msg.set_payload(0);

    auto result = msg.encode_bytes();
    REQUIRE(!result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

TEST_CASE("encode_bytes succeeds at constraint boundaries", "[constraints][encode]") {
    // percent=0 (min), deferred_val=10 (min)
    {
        constraints::ConstraintMsg msg;
        REQUIRE(msg.set_magic(0xBEEF).has_value());
        REQUIRE(msg.set_percent(0).has_value());
        msg.set_deferred_val(10);
        msg.set_payload(0);
        auto result = msg.encode_bytes();
        CHECK(result.has_value());
    }
    // percent=100 (max), deferred_val=500 (max)
    {
        constraints::ConstraintMsg msg;
        REQUIRE(msg.set_magic(0xBEEF).has_value());
        REQUIRE(msg.set_percent(100).has_value());
        msg.set_deferred_val(500);
        msg.set_payload(0);
        auto result = msg.encode_bytes();
        CHECK(result.has_value());
    }
}

// ============================================================================
// Section: Optional constrained fields
// ============================================================================

TEST_CASE("optional constrained - roundtrip with valid present values", "[constraints][optional]") {
    conduit::io::BitWriter w;
    w.write_u8(0x03);  // flags: both quality and priority present
    w.write_u8(50);    // quality = 50 (valid: 0-100)
    w.write_u8(5);     // priority = 5 (valid: 1-10, deferred)
    w.write_u16(0x1234, conduit::io::Endian::Big); // data
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto data = std::move(*finish_result);

    auto decoded = optional_constrained::OptConstMsg::decode_bytes(data);
    REQUIRE(decoded.has_value());
    CHECK(decoded->has_quality());
    CHECK(decoded->quality() == 50);
    CHECK(decoded->has_priority());
    CHECK(decoded->priority() == 5);
    CHECK(decoded->data() == 0x1234);
}

TEST_CASE("optional constrained - decode with invalid present value fails", "[constraints][optional]") {
    conduit::io::BitWriter w;
    w.write_u8(0x01);  // flags: quality present only
    w.write_u8(101);   // quality = 101 (exceeds max 100)
    w.write_u16(0x0000, conduit::io::Endian::Big); // data
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto data = std::move(*finish_result);

    auto decoded = optional_constrained::OptConstMsg::decode_bytes(data);
    REQUIRE_FALSE(decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::ConstraintViolation);
}

TEST_CASE("optional constrained - decode with absent optional no error", "[constraints][optional]") {
    conduit::io::BitWriter w;
    w.write_u8(0x00);  // flags: neither present
    w.write_u16(0xABCD, conduit::io::Endian::Big); // data
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto data = std::move(*finish_result);

    auto decoded = optional_constrained::OptConstMsg::decode_bytes(data);
    REQUIRE(decoded.has_value());
    CHECK_FALSE(decoded->has_quality());
    CHECK_FALSE(decoded->has_priority());
    CHECK(decoded->data() == 0xABCD);
}

TEST_CASE("optional constrained - setter rejects invalid value", "[constraints][optional][encode]") {
    optional_constrained::OptConstMsg msg;
    msg.set_flags(0x01); // quality present

    auto set_result = msg.set_quality(101); // exceeds max 100
    REQUIRE_FALSE(set_result.has_value());
    CHECK(set_result.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

TEST_CASE("optional constrained - deferred validate with invalid present value", "[constraints][optional][deferred]") {
    conduit::io::BitWriter w;
    w.write_u8(0x02);  // flags: priority present only
    w.write_u8(0);     // priority = 0 (below min 1, but deferred)
    w.write_u16(0x0000, conduit::io::Endian::Big); // data
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto data = std::move(*finish_result);

    auto decoded = optional_constrained::OptConstMsg::decode_bytes(data);
    REQUIRE(decoded.has_value()); // decode succeeds (deferred)
    CHECK(decoded->priority() == 0);

    auto result = decoded->validate();
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::ConstraintViolationDeferred);
}

TEST_CASE("optional constrained - deferred validate with absent optional passes", "[constraints][optional][deferred]") {
    conduit::io::BitWriter w;
    w.write_u8(0x00);  // flags: neither present
    w.write_u16(0x0000, conduit::io::Endian::Big); // data
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto data = std::move(*finish_result);

    auto decoded = optional_constrained::OptConstMsg::decode_bytes(data);
    REQUIRE(decoded.has_value());

    auto result = decoded->validate();
    CHECK(result.has_value());
}

TEST_CASE("ConstraintMsg percent at boundary 0 and 100", "[edge][constraints]") {
    // percent=0 (min boundary)
    {
        constraints::ConstraintMsg msg;
        REQUIRE(msg.set_magic(0xBEEF).has_value());
        REQUIRE(msg.set_percent(0).has_value());
        msg.set_deferred_val(100);
        msg.set_payload(0);

        auto enc_result = msg.encode_bytes();
        REQUIRE(enc_result.has_value());
        auto bytes = std::move(*enc_result);
        auto decoded = constraints::ConstraintMsg::decode_bytes(bytes);
        REQUIRE(decoded.has_value());
        CHECK(decoded->percent() == 0);
    }
    // percent=100 (max boundary)
    {
        constraints::ConstraintMsg msg;
        REQUIRE(msg.set_magic(0xBEEF).has_value());
        REQUIRE(msg.set_percent(100).has_value());
        msg.set_deferred_val(100);
        msg.set_payload(0);

        auto enc_result = msg.encode_bytes();
        REQUIRE(enc_result.has_value());
        auto bytes = std::move(*enc_result);
        auto decoded = constraints::ConstraintMsg::decode_bytes(bytes);
        REQUIRE(decoded.has_value());
        CHECK(decoded->percent() == 100);
    }
}
