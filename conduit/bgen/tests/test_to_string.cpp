// SPDX-License-Identifier: MIT
// Bgen tests - to_string() method verification
//
// Tests that generated to_string() methods produce useful output for:
// - Bitmap structs (with various field types)
// - Plain structs with inner struct fields (recursive to_string)
// - Messages with nested struct fields

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <string>

#include "bitmap_advanced/messages.hpp"
#include "bitmap_fx/messages.hpp"
#include "struct_features/messages.hpp"

// ============================================================================
// Bitmap struct to_string
// ============================================================================

TEST_CASE("Bitmap to_string: no fields set", "[to_string][bitmap]") {
    bitmap_advanced::AdvancedBitmap bm;
    auto s = bm.to_string();
    CHECK(s.find("AdvancedBitmap{") != std::string::npos);
    CHECK(s.find("}") != std::string::npos);
}

TEST_CASE("Bitmap to_string: enum field", "[to_string][bitmap]") {
    bitmap_advanced::AdvancedBitmap bm;
    bm.set_status(bitmap_advanced::status_code::warning);
    auto s = bm.to_string();
    CHECK(s.find("status=warning") != std::string::npos);
}

TEST_CASE("Bitmap to_string: string field", "[to_string][bitmap]") {
    bitmap_advanced::AdvancedBitmap bm;
    bm.set_label("TestStr");
    auto s = bm.to_string();
    CHECK(s.find("label=TestStr") != std::string::npos);
}

TEST_CASE("Bitmap to_string: bytes field", "[to_string][bitmap]") {
    bitmap_advanced::AdvancedBitmap bm;
    std::array<uint8_t, 4> raw = {0xDE, 0xAD, 0xBE, 0xEF};
    bm.set_data(raw);
    auto s = bm.to_string();
    CHECK(s.find("data=[bytes]") != std::string::npos);
}

TEST_CASE("Bitmap to_string: numeric field", "[to_string][bitmap]") {
    bitmap_advanced::AdvancedBitmap bm;
    bm.set_counter(0x1234);
    auto s = bm.to_string();
    CHECK(s.find("counter=4660") != std::string::npos);
}

TEST_CASE("Bitmap to_string: multiple fields", "[to_string][bitmap]") {
    bitmap_advanced::AdvancedBitmap bm;
    bm.set_status(bitmap_advanced::status_code::error);
    bm.set_counter(42);
    bm.set_label("hi");
    auto s = bm.to_string();
    CHECK(s.find("AdvancedBitmap{") != std::string::npos);
    CHECK(s.find("status=error") != std::string::npos);
    CHECK(s.find("counter=42") != std::string::npos);
    CHECK(s.find("label=hi") != std::string::npos);
    // Check comma separation between fields
    CHECK(s.find(", ") != std::string::npos);
}

TEST_CASE("Bitmap to_string: struct item fields", "[to_string][bitmap]") {
    // bitmap_fx has BitmapItems with struct-type fields (DataItem010, etc.)
    bitmap_fx::BitmapItems items;
    bitmap_fx::DataItem010 d010;
    d010.set_sac(1);
    d010.set_sic(2);
    items.set_item010(d010);
    auto s = items.to_string();
    CHECK(s.find("BitmapItems{") != std::string::npos);
    // Nested struct should be recursively expanded, not [struct]
    CHECK(s.find("DataItem010{") != std::string::npos);
    CHECK(s.find("sac=1") != std::string::npos);
    CHECK(s.find("sic=2") != std::string::npos);
}

// ============================================================================
// Plain struct: inner struct to_string (recursive)
// ============================================================================

TEST_CASE("Struct to_string: inner struct recursive", "[to_string][struct]") {
    // ConstrainedMessage has a "position" field of type GpsCoord (struct)
    struct_features::ConstrainedMessage msg;
    (void)msg.set_magic(0xCAFE);
    (void)msg.set_version(3);
    (void)msg.set_value(100);
    msg.mutable_position().set_latitude(12345);
    msg.mutable_position().set_longitude(67890);
    auto s = msg.to_string();
    CHECK(s.find("ConstrainedMessage{") != std::string::npos);
    // The position field should show GpsCoord's to_string, not [struct]
    CHECK(s.find("GpsCoord{") != std::string::npos);
    CHECK(s.find("latitude=12345") != std::string::npos);
    CHECK(s.find("longitude=67890") != std::string::npos);
    CHECK(s.find("[struct]") == std::string::npos);
}

// ============================================================================
// Message to_string: bitmap field inside message
// ============================================================================

TEST_CASE("Message to_string: contains bitmap struct", "[to_string][message]") {
    bitmap_advanced::BitmapAdvancedMsg msg;
    msg.set_header(0xAA);
    msg.mutable_bitmap_data().set_status(bitmap_advanced::status_code::ok);
    msg.mutable_bitmap_data().set_counter(99);
    auto s = msg.to_string();
    CHECK(s.find("BitmapAdvancedMsg{") != std::string::npos);
    CHECK(s.find("header=") != std::string::npos);
    // Bitmap nested inside message should show its to_string output
    CHECK(s.find("AdvancedBitmap{") != std::string::npos);
    CHECK(s.find("status=ok") != std::string::npos);
    CHECK(s.find("counter=99") != std::string::npos);
}

TEST_CASE("Message to_string: bitmap with struct items", "[to_string][message]") {
    bitmap_fx::Category msg;
    bitmap_fx::DataItem010 d010;
    d010.set_sac(10);
    d010.set_sic(20);
    msg.mutable_items().set_item010(d010);
    bitmap_fx::DataItem030 d030;
    d030.set_value(999);
    msg.mutable_items().set_item030(d030);
    auto s = msg.to_string();
    CHECK(s.find("Category{") != std::string::npos);
    CHECK(s.find("BitmapItems{") != std::string::npos);
    CHECK(s.find("sac=10") != std::string::npos);
    CHECK(s.find("sic=20") != std::string::npos);
    CHECK(s.find("value=999") != std::string::npos);
}

// ============================================================================
// Roundtrip: decode then to_string
// ============================================================================

TEST_CASE("Bitmap to_string after roundtrip", "[to_string][roundtrip]") {
    bitmap_advanced::BitmapAdvancedMsg original;
    original.set_header(0x55);
    original.mutable_bitmap_data().set_status(bitmap_advanced::status_code::critical);
    original.mutable_bitmap_data().set_label("RndTrip");
    original.mutable_bitmap_data().set_counter(7777);

    auto enc = original.encode_bytes();
    REQUIRE(enc.has_value());
    auto decoded = bitmap_advanced::BitmapAdvancedMsg::decode_bytes(*enc);
    REQUIRE(decoded.has_value());

    auto s = decoded->bitmap_data().to_string();
    CHECK(s.find("status=critical") != std::string::npos);
    CHECK(s.find("label=RndTrip") != std::string::npos);
    CHECK(s.find("counter=7777") != std::string::npos);
}
