// SPDX-License-Identifier: MIT
// Conduit - Integration Tests: direction-qualified dispatch (bgen-generated)

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <direction_qualified/sessions.hpp>
#include <direction_qualified/messages.hpp>
#include <any>
#include <cstdint>
#include <vector>

TEST_CASE("direction: common case full roundtrip",
          "[integration][direction]") {
    auto session = direction_qualified::create_frame_session();

    direction_qualified::CommonPayload common;
    common.set_common_data(0x77);

    auto encoded = session->encode_wrap(
        direction_qualified::CommonPayload::TYPE_ID, std::any(common));
    REQUIRE(encoded.has_value());

    auto decoded = session->decode_frame(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    auto& dm = (*decoded)[0];
    CHECK(dm.type_name == "CommonPayload");

    auto& body = std::any_cast<const direction_qualified::CommonPayload&>(dm.payload);
    CHECK(body.common_data() == 0x77);
}

TEST_CASE("direction: encode_wrap receive-only succeeds (enforcement at transceiver level)",
          "[integration][direction]") {
    auto session = direction_qualified::create_frame_session();

    direction_qualified::DownlinkPayload downlink;
    downlink.set_rx_data(0x12345678);

    auto encoded = session->encode_wrap(
        direction_qualified::DownlinkPayload::TYPE_ID, std::any(downlink));
    CHECK(encoded.has_value());
}

TEST_CASE("direction: decode raw bytes with shared discriminator yields receive type",
          "[integration][direction]") {
    // Build raw bytes: tag=1 (TAG_SHARED) + uint32 DownlinkPayload
    conduit::io::BitWriter w;
    w.write_u8(direction_qualified::DownlinkPayload::ID_VALUE);
    w.write_u32(0xDEADFACE);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto session = direction_qualified::create_frame_session();
    auto decoded = session->decode_frame(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == 1);

    CHECK(decoded->front().type_name == "DownlinkPayload");

    auto& body = std::any_cast<const direction_qualified::DownlinkPayload&>(
        decoded->front().payload);
    CHECK(body.rx_data() == 0xDEADFACE);
}

TEST_CASE("direction: send-only type is absent from decoded messages",
          "[integration][direction]") {
    // Build raw bytes with tag=1 (shared discriminator)
    conduit::io::BitWriter w;
    w.write_u8(direction_qualified::DownlinkPayload::ID_VALUE);
    w.write_u32(0x00000000);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto session = direction_qualified::create_frame_session();
    auto decoded = session->decode_frame(bytes);
    REQUIRE(decoded.has_value());

    for (const auto& dm : *decoded) {
        CHECK(dm.type_name != "UplinkPayload");
    }
}
