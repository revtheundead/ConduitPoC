// SPDX-License-Identifier: MIT
// Bgen tests - Wire encoding roundtrip verification
//
// Tests BCD, BCD_S, BNR_S wire encodings in bgen-generated code.
// Uses the wire_encodings fixture generated at build time.

#include <catch2/catch_test_macros.hpp>
#include <conduit/core/error.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <array>
#include <cstdint>
#include <vector>

#include "wire_encodings/messages.hpp"

// ============================================================================
// Section: Wire Encoding Roundtrip (wire_encodings fixture)
// ============================================================================

TEST_CASE("WireEncodingMsg encode/decode roundtrip", "[roundtrip][wire_encoding]") {
    wire_encodings::WireEncodingMsg msg;

    // BCD altitude: unsigned 16-bit BCD
    msg.set_bcd_alt(1234);
    // BCD heading: signed 13-bit BCD_S
    msg.set_bcd_hdg(-456);
    // Sign-magnitude offset: signed 16-bit BNR_S
    msg.set_sm_offset(-789);
    // CB2 value: standard two's complement (int16)
    msg.set_cb2_val(-100);
    // BNR value: standard unsigned (uint16)
    msg.set_bnr_val(5000);
    // Inline BCD: 12-bit unsigned BCD
    msg.set_inline_bcd(999);
    // Inline BNR_S: 16-bit signed sign-magnitude
    msg.set_inline_bnrs(-300);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = wire_encodings::WireEncodingMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    CHECK(decoded->bcd_alt() == 1234);
    CHECK(decoded->bcd_hdg() == -456);
    CHECK(decoded->sm_offset() == -789);
    CHECK(decoded->cb2_val() == -100);
    CHECK(decoded->bnr_val() == 5000);
    CHECK(decoded->inline_bcd() == 999);
    CHECK(decoded->inline_bnrs() == -300);
}

TEST_CASE("WireEncodingMsg BCD altitude wire format", "[roundtrip][wire_encoding][bcd]") {
    wire_encodings::WireEncodingMsg msg;
    msg.set_bcd_alt(9876);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // BCD 9876 encodes as 0x98, 0x76 (first 2 bytes since bcd-alt is first field)
    REQUIRE(bytes.size() >= 2);
    CHECK(bytes[0] == 0x98);
    CHECK(bytes[1] == 0x76);

    auto decoded = wire_encodings::WireEncodingMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->bcd_alt() == 9876);
}

TEST_CASE("WireEncodingMsg BCD zero values", "[roundtrip][wire_encoding][bcd]") {
    wire_encodings::WireEncodingMsg msg;
    msg.set_bcd_alt(0);
    msg.set_bcd_hdg(0);
    msg.set_sm_offset(0);
    msg.set_cb2_val(0);
    msg.set_bnr_val(0);
    msg.set_inline_bcd(0);
    msg.set_inline_bnrs(0);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = wire_encodings::WireEncodingMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    CHECK(decoded->bcd_alt() == 0);
    CHECK(decoded->bcd_hdg() == 0);
    CHECK(decoded->sm_offset() == 0);
    CHECK(decoded->cb2_val() == 0);
    CHECK(decoded->bnr_val() == 0);
    CHECK(decoded->inline_bcd() == 0);
    CHECK(decoded->inline_bnrs() == 0);
}

TEST_CASE("WireEncodingMsg BCD max values", "[roundtrip][wire_encoding][bcd]") {
    wire_encodings::WireEncodingMsg msg;
    // Max 16-bit BCD = 9999
    msg.set_bcd_alt(9999);
    // Max 12-bit BCD (3 nibbles) = 999
    msg.set_inline_bcd(999);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = wire_encodings::WireEncodingMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    CHECK(decoded->bcd_alt() == 9999);
    CHECK(decoded->inline_bcd() == 999);
}

TEST_CASE("WireEncodingMsg sign-magnitude positive values", "[roundtrip][wire_encoding][bnr_s]") {
    wire_encodings::WireEncodingMsg msg;
    msg.set_sm_offset(32767); // max positive for 16-bit SM (15-bit magnitude max)
    msg.set_inline_bnrs(1000);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = wire_encodings::WireEncodingMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());

    CHECK(decoded->sm_offset() == 32767);
    CHECK(decoded->inline_bnrs() == 1000);
}

TEST_CASE("WireEncodingMsg negative BCD heading roundtrip", "[roundtrip][wire_encoding][bcd]") {
    wire_encodings::WireEncodingMsg msg;
    // BCD_S heading: 13-bit, sign(1) + 3 BCD nibbles (12 bits)
    // Test several negative values
    for (auto val : {int16_t(-1), int16_t(-99), int16_t(-456), int16_t(-999)}) {
        msg.set_bcd_hdg(val);
        auto enc_result = msg.encode_bytes();
        REQUIRE(enc_result.has_value());
        auto bytes = std::move(*enc_result);
        auto decoded = wire_encodings::WireEncodingMsg::decode_bytes(bytes);
        REQUIRE(decoded.has_value());
        CHECK(decoded->bcd_hdg() == val);
    }
}

TEST_CASE("WireEncodingMsg positive BCD heading roundtrip", "[roundtrip][wire_encoding][bcd]") {
    wire_encodings::WireEncodingMsg msg;
    for (auto val : {int16_t(0), int16_t(1), int16_t(42), int16_t(123), int16_t(999)}) {
        msg.set_bcd_hdg(val);
        auto enc_result = msg.encode_bytes();
        REQUIRE(enc_result.has_value());
        auto bytes = std::move(*enc_result);
        auto decoded = wire_encodings::WireEncodingMsg::decode_bytes(bytes);
        REQUIRE(decoded.has_value());
        CHECK(decoded->bcd_hdg() == val);
    }
}

// ============================================================================
// Section: Wire format size verification (tight packing, no alignment padding)
// ============================================================================

TEST_CASE("WireEncodingMsg wire format size is tightly packed", "[roundtrip][wire_encoding][wire]") {
    // Field layout:
    //   bcd-alt:      16 bits (BCD)
    //   bcd-hdg:      13 bits (BCD_S) — non-byte-aligned!
    //   sm-offset:    16 bits (BNR_S)
    //   cb2-val:      16 bits (CB2)
    //   bnr-val:      16 bits (BNR)
    //   inline-bcd:   12 bits (BCD)
    //   inline-bnrs:  16 bits (BNR_S)
    // Total: 16+13+16+16+16+12+16 = 105 bits → ceil(105/8) = 14 bytes
    wire_encodings::WireEncodingMsg msg;
    msg.set_bcd_alt(0);
    msg.set_bcd_hdg(0);
    msg.set_sm_offset(0);
    msg.set_cb2_val(0);
    msg.set_bnr_val(0);
    msg.set_inline_bcd(0);
    msg.set_inline_bnrs(0);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes.size() == 14);
}

TEST_CASE("WireEncodingMsg non-zero tight packing roundtrip", "[roundtrip][wire_encoding][wire]") {
    // Use non-trivial values to stress bit-boundary correctness
    // after the 13-bit BCD_S field shifts everything off byte alignment
    wire_encodings::WireEncodingMsg msg;
    msg.set_bcd_alt(9876);      // 16-bit BCD
    msg.set_bcd_hdg(-999);      // 13-bit BCD_S — shifts alignment by 5 bits
    msg.set_sm_offset(-32767);  // 16-bit BNR_S, now at bit 29 (not byte-aligned)
    msg.set_cb2_val(-1);        // 16-bit CB2, now at bit 45
    msg.set_bnr_val(65535);     // 16-bit BNR, now at bit 61
    msg.set_inline_bcd(888);    // 12-bit BCD, now at bit 77
    msg.set_inline_bnrs(-1000); // 16-bit BNR_S, now at bit 89

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes.size() == 14);

    auto decoded = wire_encodings::WireEncodingMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->bcd_alt() == 9876);
    CHECK(decoded->bcd_hdg() == -999);
    CHECK(decoded->sm_offset() == -32767);
    CHECK(decoded->cb2_val() == -1);
    CHECK(decoded->bnr_val() == 65535);
    CHECK(decoded->inline_bcd() == 888);
    CHECK(decoded->inline_bnrs() == -1000);
}

TEST_CASE("WireEncodingMsg decode from truncated buffer fails", "[wire_encoding][errors]") {
    // 105 bits = 14 bytes needed; provide only 10
    std::vector<uint8_t> data(10, 0x00);
    auto decoded = wire_encodings::WireEncodingMsg::decode_bytes(data);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("WireEncodingMsg decode from empty buffer fails", "[wire_encoding][errors]") {
    std::vector<uint8_t> empty;
    auto decoded = wire_encodings::WireEncodingMsg::decode_bytes(empty);
    CHECK_FALSE(decoded.has_value());
}

// ============================================================================
// Section: Extreme value stress test
// ============================================================================

TEST_CASE("WireEncodingMsg extreme value stress roundtrip", "[roundtrip][wire_encoding]") {
    // Max values for every field simultaneously
    wire_encodings::WireEncodingMsg msg;
    msg.set_bcd_alt(9999);       // max 16-bit BCD
    msg.set_bcd_hdg(999);        // max positive 13-bit BCD_S
    msg.set_sm_offset(32767);    // max positive 16-bit BNR_S
    msg.set_cb2_val(32767);      // max positive int16 CB2
    msg.set_bnr_val(65535);      // max uint16 BNR
    msg.set_inline_bcd(999);     // max 12-bit BCD
    msg.set_inline_bnrs(32767);  // max positive 16-bit BNR_S

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = wire_encodings::WireEncodingMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->bcd_alt() == 9999);
    CHECK(decoded->bcd_hdg() == 999);
    CHECK(decoded->sm_offset() == 32767);
    CHECK(decoded->cb2_val() == 32767);
    CHECK(decoded->bnr_val() == 65535);
    CHECK(decoded->inline_bcd() == 999);
    CHECK(decoded->inline_bnrs() == 32767);
}

// ============================================================================
// Section: BCD overflow on encode (T4 test gap)
// ============================================================================

TEST_CASE("WireEncodingMsg BCD overflow on encode - 16-bit BCD", "[wire_encoding][bcd][overflow]") {
    // T4: 16-bit BCD holds 0-9999. Value 10000 should cause encode failure.
    wire_encodings::WireEncodingMsg msg;
    msg.set_bcd_alt(10000);  // Overflow: max for 16-bit BCD is 9999
    msg.set_bcd_hdg(0);
    msg.set_sm_offset(0);
    msg.set_cb2_val(0);
    msg.set_bnr_val(0);
    msg.set_inline_bcd(0);
    msg.set_inline_bnrs(0);

    auto result = msg.encode_bytes();
    CHECK_FALSE(result.has_value());
}

TEST_CASE("WireEncodingMsg BCD overflow on encode - 12-bit BCD", "[wire_encoding][bcd][overflow]") {
    // 12-bit BCD holds 0-999. Value 1000 should cause encode failure.
    wire_encodings::WireEncodingMsg msg;
    msg.set_bcd_alt(0);
    msg.set_bcd_hdg(0);
    msg.set_sm_offset(0);
    msg.set_cb2_val(0);
    msg.set_bnr_val(0);
    msg.set_inline_bcd(1000);  // Overflow: max for 12-bit BCD (3 nibbles) is 999
    msg.set_inline_bnrs(0);

    auto result = msg.encode_bytes();
    CHECK_FALSE(result.has_value());
}

TEST_CASE("WireEncodingMsg BCD_S overflow on encode", "[wire_encoding][bcd][overflow]") {
    // 13-bit BCD_S: sign(1) + 3 BCD nibbles = max magnitude 999
    // Value 1000 should cause encode failure.
    wire_encodings::WireEncodingMsg msg;
    msg.set_bcd_alt(0);
    msg.set_bcd_hdg(1000);  // Overflow: max for 13-bit BCD_S is +/-999
    msg.set_sm_offset(0);
    msg.set_cb2_val(0);
    msg.set_bnr_val(0);
    msg.set_inline_bcd(0);
    msg.set_inline_bnrs(0);

    auto result = msg.encode_bytes();
    CHECK_FALSE(result.has_value());
}

TEST_CASE("WireEncodingMsg all-negative stress roundtrip", "[roundtrip][wire_encoding]") {
    wire_encodings::WireEncodingMsg msg;
    msg.set_bcd_alt(0);
    msg.set_bcd_hdg(-999);        // max negative 13-bit BCD_S
    msg.set_sm_offset(-32767);    // max negative 16-bit BNR_S
    msg.set_cb2_val(-32768);      // min int16 CB2
    msg.set_bnr_val(0);
    msg.set_inline_bcd(0);
    msg.set_inline_bnrs(-32767);  // max negative 16-bit BNR_S

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = wire_encodings::WireEncodingMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->bcd_alt() == 0);
    CHECK(decoded->bcd_hdg() == -999);
    CHECK(decoded->sm_offset() == -32767);
    CHECK(decoded->cb2_val() == -32768);
    CHECK(decoded->bnr_val() == 0);
    CHECK(decoded->inline_bcd() == 0);
    CHECK(decoded->inline_bnrs() == -32767);
}
