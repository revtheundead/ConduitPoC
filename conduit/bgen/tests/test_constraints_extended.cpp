// SPDX-License-Identifier: MIT
// Bgen tests - Extended constraint tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <conduit/core/error.hpp>
#include <cstdint>
#include <vector>

#include "constraints_extended/messages.hpp"

// ============================================================================
// Hex equals constraint
// ============================================================================

TEST_CASE("hex equals - encode succeeds with correct value", "[constraints_ext]") {
    constraints_ext::ExtConstraintMsg msg;
    REQUIRE(msg.set_sync_word(0xABCD).has_value());
    REQUIRE(msg.set_version(42).has_value());
    REQUIRE(msg.set_temperature(0).has_value());
    REQUIRE(msg.set_count(1).has_value());
    REQUIRE(msg.set_index(0).has_value());
    msg.set_data(0);

    auto enc = msg.encode_bytes();
    CHECK(enc.has_value());
}

TEST_CASE("hex equals - setter rejects wrong value", "[constraints_ext]") {
    constraints_ext::ExtConstraintMsg msg;
    auto result = msg.set_sync_word(0x1234);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

TEST_CASE("hex equals - decode succeeds with correct value", "[constraints_ext]") {
    conduit::io::BitWriter w;
    w.write_u16(0xABCD, conduit::io::Endian::Big); // sync-word
    w.write_u8(42);                                  // version
    w.write_u16(0, conduit::io::Endian::Big);       // temperature (int16 = 0)
    w.write_u16(1, conduit::io::Endian::Big);       // count
    w.write_u8(0);                                    // index
    w.write_u16(0, conduit::io::Endian::Big);       // data
    auto finish = w.finish();
    REQUIRE(finish.has_value());

    auto dec = constraints_ext::ExtConstraintMsg::decode_bytes(*finish);
    REQUIRE(dec.has_value());
    CHECK(dec->sync_word() == 0xABCD);
}

TEST_CASE("hex equals - decode fails with wrong value", "[constraints_ext]") {
    conduit::io::BitWriter w;
    w.write_u16(0x1234, conduit::io::Endian::Big); // wrong sync-word
    w.write_u8(42);
    w.write_u16(0, conduit::io::Endian::Big);
    w.write_u16(1, conduit::io::Endian::Big);
    w.write_u8(0);
    w.write_u16(0, conduit::io::Endian::Big);
    auto finish = w.finish();
    REQUIRE(finish.has_value());

    auto dec = constraints_ext::ExtConstraintMsg::decode_bytes(*finish);
    REQUIRE_FALSE(dec.has_value());
    CHECK(dec.error().code() == conduit::ErrorCode::ConstraintViolation);
}

// ============================================================================
// Decimal equals constraint
// ============================================================================

TEST_CASE("decimal equals - version=42 succeeds", "[constraints_ext]") {
    constraints_ext::ExtConstraintMsg msg;
    REQUIRE(msg.set_sync_word(0xABCD).has_value());
    REQUIRE(msg.set_version(42).has_value());
    REQUIRE(msg.set_temperature(0).has_value());
    REQUIRE(msg.set_count(1).has_value());
    REQUIRE(msg.set_index(0).has_value());
    msg.set_data(0);
    auto enc = msg.encode_bytes();
    CHECK(enc.has_value());
}

TEST_CASE("decimal equals - version=43 fails", "[constraints_ext]") {
    constraints_ext::ExtConstraintMsg msg;
    auto result = msg.set_version(43);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

// ============================================================================
// Signed range constraint
// ============================================================================

TEST_CASE("signed range - temperature=-40 OK", "[constraints_ext]") {
    constraints_ext::ExtConstraintMsg msg;
    REQUIRE(msg.set_sync_word(0xABCD).has_value());
    REQUIRE(msg.set_version(42).has_value());
    REQUIRE(msg.set_temperature(-40).has_value());
    REQUIRE(msg.set_count(1).has_value());
    REQUIRE(msg.set_index(0).has_value());
    msg.set_data(0);
    auto enc = msg.encode_bytes();
    CHECK(enc.has_value());
}

TEST_CASE("signed range - temperature=-41 fails", "[constraints_ext]") {
    constraints_ext::ExtConstraintMsg msg;
    auto result = msg.set_temperature(-41);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

TEST_CASE("signed range - temperature=85 OK", "[constraints_ext]") {
    constraints_ext::ExtConstraintMsg msg;
    REQUIRE(msg.set_temperature(85).has_value());
}

TEST_CASE("signed range - temperature=86 fails", "[constraints_ext]") {
    constraints_ext::ExtConstraintMsg msg;
    auto result = msg.set_temperature(86);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

// ============================================================================
// Min-only constraint
// ============================================================================

TEST_CASE("min-only - count=0 fails", "[constraints_ext]") {
    constraints_ext::ExtConstraintMsg msg;
    auto result = msg.set_count(0);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

TEST_CASE("min-only - count=1 OK", "[constraints_ext]") {
    constraints_ext::ExtConstraintMsg msg;
    REQUIRE(msg.set_count(1).has_value());
}

TEST_CASE("min-only - count=65535 OK (max uint16)", "[constraints_ext]") {
    constraints_ext::ExtConstraintMsg msg;
    REQUIRE(msg.set_count(65535).has_value());
}

// ============================================================================
// Max-only constraint
// ============================================================================

TEST_CASE("max-only - index=99 OK", "[constraints_ext]") {
    constraints_ext::ExtConstraintMsg msg;
    REQUIRE(msg.set_index(99).has_value());
}

TEST_CASE("max-only - index=100 fails", "[constraints_ext]") {
    constraints_ext::ExtConstraintMsg msg;
    auto result = msg.set_index(100);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

TEST_CASE("max-only - index=0 OK", "[constraints_ext]") {
    constraints_ext::ExtConstraintMsg msg;
    REQUIRE(msg.set_index(0).has_value());
}

// ============================================================================
// Mutable escape hatch
// ============================================================================

TEST_CASE("mutable escape hatch bypasses validation, encode catches it", "[constraints_ext]") {
    constraints_ext::ExtConstraintMsg msg;
    REQUIRE(msg.set_sync_word(0xABCD).has_value());
    REQUIRE(msg.set_version(42).has_value());
    REQUIRE(msg.set_temperature(0).has_value());
    REQUIRE(msg.set_count(1).has_value());
    REQUIRE(msg.set_index(0).has_value());
    msg.set_data(0);

    // Use mutable to bypass setter validation
    msg.mutable_sync_word() = 0x0000;

    auto enc = msg.encode_bytes();
    REQUIRE_FALSE(enc.has_value());
    CHECK(enc.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

// ============================================================================
// Full roundtrip
// ============================================================================

TEST_CASE("extended constraints - full roundtrip", "[constraints_ext][roundtrip]") {
    constraints_ext::ExtConstraintMsg msg;
    REQUIRE(msg.set_sync_word(0xABCD).has_value());
    REQUIRE(msg.set_version(42).has_value());
    REQUIRE(msg.set_temperature(-10).has_value());
    REQUIRE(msg.set_count(100).has_value());
    REQUIRE(msg.set_index(50).has_value());
    msg.set_data(0x1234);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = constraints_ext::ExtConstraintMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->sync_word() == 0xABCD);
    CHECK(dec->version() == 42);
    CHECK(dec->temperature() == -10);
    CHECK(dec->count() == 100);
    CHECK(dec->index() == 50);
    CHECK(dec->data() == 0x1234);
}
