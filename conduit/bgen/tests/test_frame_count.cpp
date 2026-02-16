// SPDX-License-Identifier: MIT
// Bgen tests - Frame auto="count(payload)" roundtrip tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <vector>

#include "frame_count/messages.hpp"

TEST_CASE("frame count - single item roundtrip", "[frame_count]") {
    frame_count::DataItem d;
    d.set_value(0x1234);

    auto frame = frame_count::CountFrame::wrap(d);
    auto enc = frame.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = frame_count::CountFrame::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->count() == 1);
    REQUIRE(dec->payload().size() == 1);
    auto& item = std::get<frame_count::DataItem>(dec->payload()[0]);
    CHECK(item.value() == 0x1234);
}

TEST_CASE("frame count - batch wrap 3 items", "[frame_count]") {
    std::vector<frame_count::DataItem> items(3);
    items[0].set_value(100);
    items[1].set_value(200);
    items[2].set_value(300);

    auto frame = frame_count::CountFrame::wrap(
        std::span<const frame_count::DataItem>(items));
    auto enc = frame.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = frame_count::CountFrame::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->count() == 3);
    REQUIRE(dec->payload().size() == 3);
    CHECK(std::get<frame_count::DataItem>(dec->payload()[0]).value() == 100);
    CHECK(std::get<frame_count::DataItem>(dec->payload()[1]).value() == 200);
    CHECK(std::get<frame_count::DataItem>(dec->payload()[2]).value() == 300);
}

TEST_CASE("frame count - wire bytes verification", "[frame_count][wire]") {
    frame_count::DataItem d;
    d.set_value(0xABCD);

    auto frame = frame_count::CountFrame::wrap(d);
    auto enc = frame.encode_bytes();
    REQUIRE(enc.has_value());
    auto& bytes = *enc;

    // Header: msg-type(1) + count(1) + length(2) = 4 bytes
    // Payload: DataItem.value(2) = 2 bytes
    // Total: 6 bytes
    REQUIRE(bytes.size() == 6);
    CHECK(bytes[0] == frame_count::DataItem::ID_VALUE); // msg-type (auto="id")
    CHECK(bytes[1] == 1);                                // count = 1
    // bytes[2..3] = length (total frame = 6)
    CHECK(bytes[4] == 0xAB);                             // value high
    CHECK(bytes[5] == 0xCD);                             // value low
}

TEST_CASE("frame count - empty payload", "[frame_count]") {
    frame_count::CountFrame frame;
    frame.set_msg_type(frame_count::DataItem::ID_VALUE);
    // payload is empty

    auto enc = frame.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = frame_count::CountFrame::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->count() == 0);
    CHECK(dec->payload().empty());
}
