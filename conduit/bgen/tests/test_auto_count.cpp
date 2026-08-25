// SPDX-License-Identifier: MIT
// Bgen tests - auto="count(field)" tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <vector>

#include "auto_count/structs.hpp"
#include "auto_count/messages.hpp"

TEST_CASE("auto count - message with 3 entries roundtrip", "[auto_count]") {
    auto_count::CountMsg msg;
    msg.set_id(42);
    msg.mutable_entries().push_back(100);
    msg.mutable_entries().push_back(200);
    msg.mutable_entries().push_back(300);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = auto_count::CountMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->id() == 42);
    CHECK(dec->num_entries() == 3);
    REQUIRE(dec->entries().size() == 3);
    CHECK(dec->entries()[0] == 100);
    CHECK(dec->entries()[1] == 200);
    CHECK(dec->entries()[2] == 300);
}

TEST_CASE("auto count - empty array gives count 0", "[auto_count]") {
    auto_count::CountMsg msg;
    msg.set_id(1);
    // entries is empty

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto dec = auto_count::CountMsg::decode_bytes(*enc);
    REQUIRE(dec.has_value());
    CHECK(dec->num_entries() == 0);
    CHECK(dec->entries().empty());
}

TEST_CASE("auto count - struct Container with items", "[auto_count]") {
    auto_count::Container c;
    c.set_tag(0xAA);
    auto& items = c.mutable_items();
    auto_count::Record r1; r1.set_value(1111);
    auto_count::Record r2; r2.set_value(2222);
    items.push_back(r1);
    items.push_back(r2);

    conduit::io::BitWriter w;
    auto enc_result = c.encode(w);
    REQUIRE(enc_result.has_value());
    auto finish = w.finish();
    REQUIRE(finish.has_value());
    auto bytes = std::move(*finish);

    conduit::io::BitReader r(bytes);
    auto dec = auto_count::Container::decode(r);
    REQUIRE(dec.has_value());
    CHECK(dec->tag() == 0xAA);
    CHECK(dec->count() == 2);
    REQUIRE(dec->items().size() == 2);
    CHECK(dec->items()[0].value() == 1111);
    CHECK(dec->items()[1].value() == 2222);
}

TEST_CASE("auto count - to_string reports element count before encode",
          "[auto_count][to_string]") {
    auto_count::CountMsg msg;
    msg.set_id(42);
    msg.mutable_entries().push_back(100);
    msg.mutable_entries().push_back(200);
    msg.mutable_entries().push_back(300);

    // num-entries = count(entries) is patched during encode; the member is 0
    // here. to_string must report the referenced array's element count (3).
    CHECK(msg.num_entries() == 0);
    auto s = msg.to_string();
    INFO(s);
    CHECK(s.find("num-entries=3") != std::string::npos);

    // Same for a struct-level count field.
    auto_count::Container c;
    c.set_tag(0xAA);
    auto_count::Record r1; r1.set_value(1111);
    auto_count::Record r2; r2.set_value(2222);
    c.mutable_items().push_back(r1);
    c.mutable_items().push_back(r2);
    CHECK(c.count() == 0);
    auto cs = c.to_string();
    INFO(cs);
    CHECK(cs.find("count=2") != std::string::npos);
}

TEST_CASE("auto count - wire bytes verification", "[auto_count][wire]") {
    auto_count::CountMsg msg;
    msg.set_id(7);
    msg.mutable_entries().push_back(0x0001);
    msg.mutable_entries().push_back(0x0002);

    auto enc = msg.encode_bytes();
    REQUIRE(enc.has_value());
    auto& bytes = *enc;
    // id(1) + num_entries(2) + 2*entry(2 each) = 7 bytes
    REQUIRE(bytes.size() == 7);
    CHECK(bytes[0] == 7);    // id
    CHECK(bytes[1] == 0x00); // num_entries high = 2
    CHECK(bytes[2] == 0x02); // num_entries low
    CHECK(bytes[3] == 0x00); // entries[0] high
    CHECK(bytes[4] == 0x01); // entries[0] low
    CHECK(bytes[5] == 0x00); // entries[1] high
    CHECK(bytes[6] == 0x02); // entries[1] low
}
