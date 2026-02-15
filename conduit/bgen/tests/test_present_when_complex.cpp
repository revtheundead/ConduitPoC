// SPDX-License-Identifier: MIT
// Tests for present_when on ArrayDef and ChoiceDef decode paths

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>

#include "present_when_complex/structs.hpp"

namespace {

// Helper: encode a Packet to bytes via BitWriter
conduit::Result<std::vector<uint8_t>> encode(const present_when_complex::Packet& msg) {
    conduit::io::BitWriter w;
    auto r = msg.encode(w);
    if (!r) return std::unexpected(r.error());
    return w.finish();
}

// Helper: decode a Packet from bytes via BitReader
conduit::Result<present_when_complex::Packet> decode(std::span<const uint8_t> data) {
    conduit::io::BitReader r(data);
    return present_when_complex::Packet::decode(r);
}

} // namespace

// ============================================================================
// H1: ArrayDef/ChoiceDef with present_when — decode guard
// ============================================================================

TEST_CASE("present_when array: present when flag set", "[present_when][array]") {
    present_when_complex::Packet msg;
    msg.set_flags(0x01);  // bit 0 set -> array present
    msg.set_count(2);
    auto& items = msg.mutable_items();
    items.push_back(0x1234);
    items.push_back(0x5678);
    msg.set_trailer(0xAA);

    auto enc = encode(msg);
    REQUIRE(enc.has_value());

    auto dec = decode(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->flags() == 0x01);
    CHECK(dec->count() == 2);
    REQUIRE(dec->has_items());
    REQUIRE(dec->items().size() == 2);
    CHECK(dec->items()[0] == 0x1234);
    CHECK(dec->items()[1] == 0x5678);
    CHECK(dec->trailer() == 0xAA);
}

TEST_CASE("present_when array: absent when flag clear", "[present_when][array]") {
    // Encode manually: flags=0 (no array, no choice), count=0, trailer=0xBB
    conduit::io::BitWriter w;
    w.write_u8(0x00);  // flags: no present_when conditions met
    w.write_u8(0);     // count
    w.write_u8(0xBB);  // trailer
    auto bytes = w.finish();
    REQUIRE(bytes.has_value());

    auto dec = decode(*bytes);
    REQUIRE(dec.has_value());
    CHECK(dec->flags() == 0x00);
    CHECK_FALSE(dec->has_items());
    CHECK_FALSE(dec->has_extra());
    CHECK(dec->trailer() == 0xBB);
}

TEST_CASE("present_when choice: present when flag set", "[present_when][choice]") {
    present_when_complex::Packet msg;
    msg.set_flags(0x12);  // bit 1 set -> choice present, upper nibble 0x10 -> TypeA
    msg.set_count(0);

    // Set choice to TypeA variant (inline case types are at namespace level)
    present_when_complex::Packet_TypeA type_a;
    type_a.set_a_val(0x9999);
    msg.mutable_extra() = type_a;
    msg.set_trailer(0xCC);

    auto enc = encode(msg);
    REQUIRE(enc.has_value());

    auto dec = decode(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->flags() == 0x12);
    REQUIRE(dec->has_extra());
    auto* a = std::get_if<present_when_complex::Packet_TypeA>(&dec->extra());
    REQUIRE(a != nullptr);
    CHECK(a->a_val() == 0x9999);
    CHECK(dec->trailer() == 0xCC);
}

TEST_CASE("present_when both: array and choice present", "[present_when][array][choice]") {
    present_when_complex::Packet msg;
    msg.set_flags(0x13);  // bits 0+1 set -> both present, upper nibble 0x10 -> TypeA
    msg.set_count(1);
    auto& items = msg.mutable_items();
    items.push_back(0xAAAA);

    present_when_complex::Packet_TypeA type_a;
    type_a.set_a_val(0xBBBB);
    msg.mutable_extra() = type_a;
    msg.set_trailer(0xDD);

    auto enc = encode(msg);
    REQUIRE(enc.has_value());

    auto dec = decode(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->flags() == 0x13);
    REQUIRE(dec->has_items());
    REQUIRE(dec->items().size() == 1);
    CHECK(dec->items()[0] == 0xAAAA);
    REQUIRE(dec->has_extra());
    CHECK(dec->trailer() == 0xDD);
}
