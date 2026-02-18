// SPDX-License-Identifier: MIT
// Tests proving constants work in constraints everywhere in the spec

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>

#include "constants_everywhere/messages.hpp"
#include "constants_everywhere/sessions.hpp"
#include "constants_everywhere/protocol.hpp"

// ============================================================================
// Constants in frame sync field (constraint equals with named constant)
// ============================================================================

TEST_CASE("constants: frame sync field uses named constant SYNC", "[constants]") {
    constants_everywhere::Versioned msg;
    msg.set_data(42);

    auto frame = constants_everywhere::Frame::wrap(msg);
    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    // Check sync pattern in wire data (SYNC = 0xCAFE, big-endian)
    REQUIRE(bytes->size() >= 2);
    CHECK((*bytes)[0] == 0xCA);
    CHECK((*bytes)[1] == 0xFE);

    // Decode should succeed
    auto decoded = constants_everywhere::Frame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());
}

// ============================================================================
// Constants in message field constraint equals
// ============================================================================

TEST_CASE("constants: message field with constraint equals named constant", "[constants]") {
    constants_everywhere::Versioned msg;
    msg.set_data(100);

    // Version field has constraint equals="VERSION" (= 3)
    // After default-construction, the field should be initialized to the constraint value
    CHECK(msg.version() == 3);

    auto frame = constants_everywhere::Frame::wrap(msg);
    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    auto decoded = constants_everywhere::Frame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());

    auto* payload = std::get_if<constants_everywhere::Versioned>(&decoded->payload());
    REQUIRE(payload != nullptr);
    CHECK(payload->version() == 3);
    CHECK(payload->data() == 100);
}

TEST_CASE("constants: session roundtrip with constrained message field", "[constants]") {
    auto session = constants_everywhere::create_frame_session();
    REQUIRE(session != nullptr);

    constants_everywhere::Versioned msg;
    msg.set_data(200);

    auto encoded = session->encode_wrap(constants_everywhere::Versioned::TYPE_ID, msg);
    REQUIRE(encoded.has_value());

    auto decoded = session->decode_frame(encoded->bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto* payload = std::any_cast<constants_everywhere::Versioned>(&decoded->at(0).payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->version() == 3);
    CHECK(payload->data() == 200);
}

// ============================================================================
// Constants in nested struct constraint equals
// ============================================================================

TEST_CASE("constants: nested struct field with constraint equals named constant", "[constants]") {
    constants_everywhere::WithHeader msg;
    msg.mutable_hdr().set_flags(0x55);
    msg.set_payload_data(12345);

    // Magic field has constraint equals="HEADER_MAGIC" (= 0xAA)
    CHECK(msg.hdr().magic() == 0xAA);

    auto frame = constants_everywhere::Frame::wrap(msg);
    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    auto decoded = constants_everywhere::Frame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());

    auto* payload = std::get_if<constants_everywhere::WithHeader>(&decoded->payload());
    REQUIRE(payload != nullptr);
    CHECK(payload->hdr().magic() == 0xAA);
    CHECK(payload->hdr().flags() == 0x55);
    CHECK(payload->payload_data() == 12345);
}

TEST_CASE("constants: session roundtrip with nested struct constrained field", "[constants]") {
    auto session = constants_everywhere::create_frame_session();

    constants_everywhere::WithHeader msg;
    msg.mutable_hdr().set_flags(0x0F);
    msg.set_payload_data(99999);

    auto encoded = session->encode_wrap(constants_everywhere::WithHeader::TYPE_ID, msg);
    REQUIRE(encoded.has_value());

    auto decoded = session->decode_frame(encoded->bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto* payload = std::any_cast<constants_everywhere::WithHeader>(&decoded->at(0).payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->hdr().magic() == 0xAA);
    CHECK(payload->hdr().flags() == 0x0F);
    CHECK(payload->payload_data() == 99999);
}

// ============================================================================
// Constants as default values
// ============================================================================

TEST_CASE("constants: field with numeric default value", "[constants]") {
    constants_everywhere::DefaultVersion msg;

    // default="3" should set the initial value
    CHECK(msg.version() == 3);
    msg.set_count(5);

    auto frame = constants_everywhere::Frame::wrap(msg);
    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    auto decoded = constants_everywhere::Frame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());

    auto* payload = std::get_if<constants_everywhere::DefaultVersion>(&decoded->payload());
    REQUIRE(payload != nullptr);
    CHECK(payload->version() == 3);
    CHECK(payload->count() == 5);
}

TEST_CASE("constants: field with numeric default can be overridden", "[constants]") {
    constants_everywhere::DefaultVersion msg;
    msg.set_version(7);  // Override the default
    msg.set_count(10);

    auto frame = constants_everywhere::Frame::wrap(msg);
    auto bytes = frame.encode_bytes();
    REQUIRE(bytes.has_value());

    auto decoded = constants_everywhere::Frame::decode_bytes(*bytes);
    REQUIRE(decoded.has_value());

    auto* payload = std::get_if<constants_everywhere::DefaultVersion>(&decoded->payload());
    REQUIRE(payload != nullptr);
    CHECK(payload->version() == 7);
    CHECK(payload->count() == 10);
}

// ============================================================================
// Constants: generated constant values accessible
// ============================================================================

TEST_CASE("constants: generated constant values are correct", "[constants]") {
    CHECK(constants_everywhere::SYNC == 0xCAFE);
    CHECK(constants_everywhere::VERSION == 3);
    CHECK(constants_everywhere::MAX_ITEMS == 10);
    CHECK(constants_everywhere::HEADER_MAGIC == 0xAA);
}
