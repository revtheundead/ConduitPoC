// SPDX-License-Identifier: MIT
// Bgen tests - Frame auto="count(payload)" roundtrip tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <any>
#include <cstdint>
#include <string>
#include <vector>

#include "frame_count/messages.hpp"
#include "frame_count/sessions.hpp"

namespace {

// Look up an auto-field value by key, or return "<missing>".
std::string auto_field(const conduit::traits::EncodeResult& r, const std::string& key) {
    for (const auto& [k, v] : r.auto_fields) {
        if (k == key) return v;
    }
    return "<missing>";
}

} // namespace

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

TEST_CASE("frame count - encode_wrap records count in auto_fields", "[frame_count][session]") {
    auto session = frame_count::create_count_frame_session();

    frame_count::DataItem d;
    d.set_value(0x1234);

    auto encoded = session->encode_wrap(frame_count::DataItem::TYPE_ID, std::any{d});
    REQUIRE(encoded.has_value());

    // The count field is patched onto the wire during encode; without recording
    // it in auto_fields the logger would render it as the zero-initialized
    // member. A single wrap is always exactly one payload element.
    CHECK(auto_field(*encoded, "count") == "1");

    // format_outbound must then surface that count rather than 0.
    auto formatted = session->format_outbound(frame_count::DataItem::TYPE_ID,
                                              std::any{d}, encoded->auto_fields);
    INFO(formatted);
    CHECK(formatted.find("count=1") != std::string::npos);
    CHECK(formatted.find("count=0") == std::string::npos);
}

TEST_CASE("frame count - encode_batch records payload count in auto_fields", "[frame_count][session]") {
    auto session = frame_count::create_count_frame_session();

    std::vector<std::any> payloads;
    for (int i = 0; i < 3; ++i) {
        frame_count::DataItem d;
        d.set_value(static_cast<uint16_t>(i));
        payloads.push_back(std::any{d});
    }

    auto encoded = session->encode_batch(frame_count::DataItem::TYPE_ID, payloads);
    REQUIRE(encoded.has_value());
    CHECK(auto_field(*encoded, "count") == "3");

    auto formatted = session->format_outbound(frame_count::DataItem::TYPE_ID,
                                              payloads[0], encoded->auto_fields);
    INFO(formatted);
    CHECK(formatted.find("count=3") != std::string::npos);
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
