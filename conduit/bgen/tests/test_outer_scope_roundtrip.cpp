// SPDX-License-Identifier: MIT
// Outer-scope field access + auto-length backpatch roundtrip tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>

#include "outer_scope/structs.hpp"

namespace {

// Helper: encode a Packet and return the wire bytes
conduit::Result<std::vector<uint8_t>> encode_packet(const outer_scope::Packet& pkt) {
    conduit::io::BitWriter w;
    CONDUIT_TRY(pkt.encode(w));
    return w.finish();
}

// Helper: decode a Packet from wire bytes
conduit::Result<outer_scope::Packet> decode_packet(std::span<const uint8_t> data) {
    conduit::io::BitReader r(data);
    return outer_scope::Packet::decode(r);
}

} // namespace

// ============================================================================
// Auto-length backpatch: len field is automatically set during encode
// ============================================================================

TEST_CASE("outer_scope: Packet with DataA roundtrip", "[outer_scope][roundtrip]") {
    outer_scope::Packet pkt;
    pkt.set_tag(1);

    outer_scope::DataA data;
    data.set_x(10);
    data.set_y(20);
    pkt.set_payload(data);

    auto bytes = encode_packet(pkt);
    REQUIRE(bytes.has_value());

    // Wire: [tag:1][len:1][x:1][y:1] = 4 bytes
    REQUIRE(bytes->size() == 4);
    CHECK((*bytes)[0] == 1);   // tag
    CHECK((*bytes)[1] == 4);   // auto-length = total struct size
    CHECK((*bytes)[2] == 10);  // x
    CHECK((*bytes)[3] == 20);  // y

    auto decoded = decode_packet(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->tag() == 1);
    CHECK(decoded->len() == 4);

    auto* p = std::get_if<outer_scope::DataA>(&decoded->payload());
    REQUIRE(p != nullptr);
    CHECK(p->x() == 10);
    CHECK(p->y() == 20);
}

TEST_CASE("outer_scope: Packet with DataB roundtrip", "[outer_scope][roundtrip]") {
    outer_scope::Packet pkt;
    pkt.set_tag(2);

    outer_scope::DataB data;
    data.set_value(0x1234);
    pkt.set_payload(data);

    auto bytes = encode_packet(pkt);
    REQUIRE(bytes.has_value());

    // Wire: [tag:1][len:1][value_hi:1][value_lo:1] = 4 bytes
    REQUIRE(bytes->size() == 4);
    CHECK((*bytes)[0] == 2);     // tag
    CHECK((*bytes)[1] == 4);     // auto-length = total struct size
    CHECK((*bytes)[2] == 0x12);  // value high byte (big-endian)
    CHECK((*bytes)[3] == 0x34);  // value low byte

    auto decoded = decode_packet(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->tag() == 2);
    CHECK(decoded->len() == 4);

    auto* p = std::get_if<outer_scope::DataB>(&decoded->payload());
    REQUIRE(p != nullptr);
    CHECK(p->value() == 0x1234);
}

// ============================================================================
// Outer-scope field access: otherwise case uses parent's "len" to compute
// the bytes length via length-from="len - 2"
// ============================================================================

TEST_CASE("outer_scope: Packet with otherwise case (outer-scope len)", "[outer_scope][roundtrip]") {
    outer_scope::Packet pkt;
    pkt.set_tag(99);  // not 1 or 2, so falls through to otherwise

    outer_scope::Packet_payloadOtherwise raw;
    raw.set_data({0xAA, 0xBB, 0xCC});
    pkt.set_payload(raw);

    auto bytes = encode_packet(pkt);
    REQUIRE(bytes.has_value());

    // Wire: [tag:1][len:1][data:3] = 5 bytes
    REQUIRE(bytes->size() == 5);
    CHECK((*bytes)[0] == 99);    // tag
    CHECK((*bytes)[1] == 5);     // auto-length = total struct size
    CHECK((*bytes)[2] == 0xAA);  // data[0]
    CHECK((*bytes)[3] == 0xBB);  // data[1]
    CHECK((*bytes)[4] == 0xCC);  // data[2]

    // Decode: payloadOtherwise::decode(r, len) reads len - 2 = 5 - 2 = 3 bytes
    auto decoded = decode_packet(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->tag() == 99);
    CHECK(decoded->len() == 5);

    auto* p = std::get_if<outer_scope::Packet_payloadOtherwise>(&decoded->payload());
    REQUIRE(p != nullptr);
    REQUIRE(p->data().size() == 3);
    CHECK(p->data()[0] == 0xAA);
    CHECK(p->data()[1] == 0xBB);
    CHECK(p->data()[2] == 0xCC);
}

TEST_CASE("outer_scope: otherwise with empty data", "[outer_scope][roundtrip]") {
    outer_scope::Packet pkt;
    pkt.set_tag(255);

    outer_scope::Packet_payloadOtherwise raw;
    // empty data
    pkt.set_payload(raw);

    auto bytes = encode_packet(pkt);
    REQUIRE(bytes.has_value());

    // Wire: [tag:1][len:1] = 2 bytes (no data)
    REQUIRE(bytes->size() == 2);
    CHECK((*bytes)[1] == 2);  // auto-length = 2 (tag + len, no payload bytes)

    // Decode: len - 2 = 0 bytes
    auto decoded = decode_packet(*bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->len() == 2);

    auto* p = std::get_if<outer_scope::Packet_payloadOtherwise>(&decoded->payload());
    REQUIRE(p != nullptr);
    CHECK(p->data().empty());
}

// ============================================================================
// Encode validation: variant must match discriminator
// ============================================================================

TEST_CASE("outer_scope: encode mismatch tag vs variant returns error", "[outer_scope][error]") {
    outer_scope::Packet pkt;
    pkt.set_tag(1);  // tag says DataA

    outer_scope::DataB data;
    data.set_value(42);
    pkt.set_payload(data);  // but payload is DataB

    auto bytes = encode_packet(pkt);
    REQUIRE(!bytes.has_value());
    CHECK(bytes.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

TEST_CASE("outer_scope: encode otherwise with reserved tag returns error", "[outer_scope][error]") {
    outer_scope::Packet pkt;
    pkt.set_tag(1);  // tag=1 is reserved for DataA

    outer_scope::Packet_payloadOtherwise raw;
    raw.set_data({0x01});
    pkt.set_payload(raw);  // but payload is otherwise

    auto bytes = encode_packet(pkt);
    REQUIRE(!bytes.has_value());
    CHECK(bytes.error().code() == conduit::ErrorCode::EncodeConstraintViolation);
}

// ============================================================================
// to_string smoke tests
// ============================================================================

TEST_CASE("outer_scope: Packet to_string includes all fields", "[outer_scope][to_string]") {
    outer_scope::Packet pkt;
    pkt.set_tag(1);
    outer_scope::DataA data;
    data.set_x(5);
    data.set_y(7);
    pkt.set_payload(data);

    auto s = pkt.to_string();
    CHECK(s.find("Packet{") != std::string::npos);
    CHECK(s.find("tag=1") != std::string::npos);
    CHECK(s.find("DataA{") != std::string::npos);
    CHECK(s.find("x=5") != std::string::npos);
    CHECK(s.find("y=7") != std::string::npos);
}

// ============================================================================
// equality
// ============================================================================

TEST_CASE("outer_scope: Packet equality", "[outer_scope][equality]") {
    outer_scope::Packet pkt1, pkt2;
    pkt1.set_tag(2);
    pkt2.set_tag(2);

    outer_scope::DataB d1, d2;
    d1.set_value(100);
    d2.set_value(100);
    pkt1.set_payload(d1);
    pkt2.set_payload(d2);

    CHECK(pkt1 == pkt2);

    d2.set_value(200);
    pkt2.set_payload(d2);
    CHECK_FALSE(pkt1 == pkt2);
}
