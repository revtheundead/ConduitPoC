// SPDX-License-Identifier: MIT
// Bgen tests - Miscellaneous fixture roundtrip verification
//
// Tests generated code from: default_initial, string_prefix_incl,
// otherwise_protocol, nested_choice, multi_entry, auto_sequence fixtures.

#include <catch2/catch_test_macros.hpp>
#include <conduit/core/error.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <conduit/traits/session_traits.hpp>
#include <any>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

#include "default_initial/messages.hpp"
#include "string_prefix_incl/messages.hpp"
#include "otherwise_protocol/messages.hpp"
#include "otherwise_protocol/constants.hpp"
#include "nested_choice/messages.hpp"
#include "nested_choice/constants.hpp"
#include "multi_entry/messages.hpp"
#include "multi_entry/constants.hpp"
#include "auto_sequence/messages.hpp"
#include "auto_sequence/sessions.hpp"
#include "auto_sequence/constants.hpp"
#include "enum_arrays/messages.hpp"
#include "signed_length/messages.hpp"
#include "signed_length/constants.hpp"
#include "send_only_leaf/messages.hpp"
#include "send_only_leaf/constants.hpp"
#include "non_overlap_ranges/messages.hpp"
#include "non_overlap_ranges/constants.hpp"

// ============================================================================
// Section: Default / Initial values (default_initial fixture)
// ============================================================================

TEST_CASE("DefaultMsg roundtrip", "[roundtrip][default_initial]") {
    default_initial::DefaultMsg msg;
    msg.set_version(1);
    msg.set_priority(0);
    msg.set_data(0x12345678);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes.size() == 6);
    auto decoded = default_initial::DefaultMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->version() == 1);
    CHECK(decoded->priority() == 0);
    CHECK(decoded->data() == 0x12345678);
}

TEST_CASE("DefaultMsg wire format", "[roundtrip][default_initial][wire]") {
    default_initial::DefaultMsg msg;
    msg.set_version(5);
    msg.set_priority(3);
    msg.set_data(0);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() == 6);
    CHECK(bytes[0] == 5);  // version
    CHECK(bytes[1] == 3);  // priority
    CHECK(bytes[2] == 0);  // data high byte
}

TEST_CASE("InitialMsg has initial values", "[roundtrip][default_initial]") {
    // InitialMsg should have counter=100 and status=0 by default
    default_initial::InitialMsg msg;
    CHECK(msg.counter() == 100);
    CHECK(msg.status() == 0);
}

TEST_CASE("InitialMsg roundtrip", "[roundtrip][default_initial]") {
    default_initial::InitialMsg msg;
    // Use default initial values
    msg.set_payload(0xAABBCCDD);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes.size() == 7);
    auto decoded = default_initial::InitialMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->counter() == 100);
    CHECK(decoded->status() == 0);
    CHECK(decoded->payload() == 0xAABBCCDD);
}

TEST_CASE("InitialMsg override initial values", "[roundtrip][default_initial]") {
    default_initial::InitialMsg msg;
    msg.set_counter(999);
    msg.set_status(42);
    msg.set_payload(0);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = default_initial::InitialMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->counter() == 999);
    CHECK(decoded->status() == 42);
}

// ============================================================================
// Section: String with length-includes-prefix (string_prefix_incl fixture)
// ============================================================================

TEST_CASE("PrefixMsg roundtrip", "[roundtrip][string_prefix]") {
    string_prefix_incl::PrefixMsg msg;
    msg.set_label("Hello");
    msg.set_tag(42);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = string_prefix_incl::PrefixMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->label() == "Hello");
    CHECK(decoded->tag() == 42);
}

TEST_CASE("PrefixMsg wire format length includes prefix", "[roundtrip][string_prefix][wire]") {
    string_prefix_incl::PrefixMsg msg;
    msg.set_label("AB");
    msg.set_tag(0);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // Wire: uint16 length prefix (includes self=2 bytes) + string bytes + tag
    // Length prefix = 2 + 2 = 4 for "AB"
    REQUIRE(bytes.size() >= 2);
    uint16_t wire_len = (static_cast<uint16_t>(bytes[0]) << 8) | bytes[1];
    CHECK(wire_len == 4); // 2 (prefix size) + 2 (string length)
}

TEST_CASE("PrefixMsg empty string roundtrip", "[roundtrip][string_prefix]") {
    string_prefix_incl::PrefixMsg msg;
    msg.set_label("");
    msg.set_tag(99);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // Length prefix should be 2 (just the prefix itself, no string data)
    REQUIRE(bytes.size() >= 2);
    uint16_t wire_len = (static_cast<uint16_t>(bytes[0]) << 8) | bytes[1];
    CHECK(wire_len == 2);

    auto decoded = string_prefix_incl::PrefixMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->label().empty());
    CHECK(decoded->tag() == 99);
}

TEST_CASE("PrefixMsg long string roundtrip", "[roundtrip][string_prefix]") {
    string_prefix_incl::PrefixMsg msg;
    std::string long_str(100, 'X');
    msg.set_label(long_str);
    msg.set_tag(7);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = string_prefix_incl::PrefixMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->label() == long_str);
    CHECK(decoded->tag() == 7);
}

// ============================================================================
// Section: Otherwise (otherwise_protocol fixture)
// ============================================================================

TEST_CASE("Otherwise Frame with known type roundtrip", "[roundtrip][otherwise]") {
    otherwise_test::BodyA body;
    body.set_x(0x1234);

    auto frame = otherwise_test::Frame::wrap(body);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = otherwise_test::Frame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->tag() == otherwise_test::TYPE_A);
    auto& decoded_body = std::get<otherwise_test::BodyA>(decoded->body());
    CHECK(decoded_body.x() == 0x1234);
}

TEST_CASE("Otherwise Frame with unknown tag falls back", "[roundtrip][otherwise]") {
    // Build wire bytes with unknown tag value
    conduit::io::BitWriter w;
    w.write_u8(99);        // tag = 99 (not TYPE_A=1)
    w.write_u16(0xBEEF);   // FallbackBody.y
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = otherwise_test::Frame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->tag() == 99);
    auto& fallback = std::get<otherwise_test::FallbackBody>(decoded->body());
    CHECK(fallback.y() == 0xBEEF);
}

TEST_CASE("Otherwise Frame wrap BodyA sets discriminator", "[roundtrip][otherwise][wire]") {
    otherwise_test::BodyA body;
    body.set_x(0);
    auto frame = otherwise_test::Frame::wrap(body);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // First byte is the tag
    REQUIRE(!bytes.empty());
    CHECK(bytes[0] == otherwise_test::TYPE_A);
}

// ============================================================================
// Section: Nested Choice (nested_choice fixture)
// ============================================================================

TEST_CASE("Nested choice Envelope -> CategoryA -> LeafX", "[roundtrip][nested_choice]") {
    nested_choice::LeafX leaf;
    leaf.set_data_x(0xABCD);
    auto envelope = nested_choice::Envelope::wrap(leaf);

    auto enc_result = envelope.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = nested_choice::Envelope::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->category() == nested_choice::CAT_A);
    auto& cat_a = std::get<nested_choice::CategoryA>(decoded->payload());
    CHECK(cat_a.sub_type() == nested_choice::SUB_X);
    auto& leaf_x = std::get<nested_choice::LeafX>(cat_a.sub_body());
    CHECK(leaf_x.data_x() == 0xABCD);
}

TEST_CASE("Nested choice Envelope -> CategoryA -> LeafY", "[roundtrip][nested_choice]") {
    nested_choice::LeafY leaf;
    leaf.set_data_y(0x5678);
    auto envelope = nested_choice::Envelope::wrap(leaf);

    auto enc_result = envelope.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = nested_choice::Envelope::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->category() == nested_choice::CAT_A);
    auto& cat_a = std::get<nested_choice::CategoryA>(decoded->payload());
    CHECK(cat_a.sub_type() == nested_choice::SUB_Y);
    auto& leaf_y = std::get<nested_choice::LeafY>(cat_a.sub_body());
    CHECK(leaf_y.data_y() == 0x5678);
}

TEST_CASE("Nested choice Envelope -> CategoryB", "[roundtrip][nested_choice]") {
    nested_choice::CategoryB cat_b;
    cat_b.set_simple(0xF00D);
    auto envelope = nested_choice::Envelope::wrap(cat_b);

    auto enc_result = envelope.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = nested_choice::Envelope::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->category() == nested_choice::CAT_B);
    auto& decoded_b = std::get<nested_choice::CategoryB>(decoded->payload());
    CHECK(decoded_b.simple() == 0xF00D);
}

TEST_CASE("Nested choice unknown category fails", "[roundtrip][nested_choice][errors]") {
    conduit::io::BitWriter w;
    w.write_u8(99);       // unknown category
    w.write_u16(0x1234);  // some data
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = nested_choice::Envelope::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("Nested choice wire format", "[roundtrip][nested_choice][wire]") {
    nested_choice::LeafX leaf;
    leaf.set_data_x(0x1234);
    auto envelope = nested_choice::Envelope::wrap(leaf);
    auto enc_result = envelope.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    // category(1) + sub_type(1) + data_x(2) = 4 bytes
    CHECK(bytes.size() == 4);
    CHECK(bytes[0] == nested_choice::CAT_A);
    CHECK(bytes[1] == nested_choice::SUB_X);
    CHECK(bytes[2] == 0x12);
    CHECK(bytes[3] == 0x34);
}

// ============================================================================
// Section: Multi-Entry (multi_entry fixture)
// ============================================================================

TEST_CASE("Multi-entry CommandFrame Cmd1Body roundtrip", "[roundtrip][multi_entry]") {
    multi_entry::Cmd1Body cmd1;
    cmd1.set_param(0xDEADBEEF);
    auto frame = multi_entry::CommandFrame::wrap(cmd1);

    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = multi_entry::CommandFrame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->sync() == multi_entry::SYNC_A);
    CHECK(decoded->cmd_id() == multi_entry::CMD_1);
    auto& body = std::get<multi_entry::Cmd1Body>(decoded->cmd());
    CHECK(body.param() == 0xDEADBEEF);
}

TEST_CASE("Multi-entry CommandFrame Cmd2Body roundtrip", "[roundtrip][multi_entry]") {
    multi_entry::Cmd2Body cmd2;
    cmd2.set_flags(0xAAAA);
    auto frame = multi_entry::CommandFrame::wrap(cmd2);

    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = multi_entry::CommandFrame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->sync() == multi_entry::SYNC_A);
    CHECK(decoded->cmd_id() == multi_entry::CMD_2);
    auto& body = std::get<multi_entry::Cmd2Body>(decoded->cmd());
    CHECK(body.flags() == 0xAAAA);
}

TEST_CASE("Multi-entry EventFrame Evt1Body roundtrip", "[roundtrip][multi_entry]") {
    multi_entry::Evt1Body evt1;
    evt1.set_value(0xCAFEBABE);
    auto frame = multi_entry::EventFrame::wrap(evt1);

    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = multi_entry::EventFrame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->sync() == multi_entry::SYNC_B);
    CHECK(decoded->evt_id() == multi_entry::EVT_1);
    auto& body = std::get<multi_entry::Evt1Body>(decoded->evt());
    CHECK(body.value() == 0xCAFEBABE);
}

TEST_CASE("Multi-entry CommandFrame wrong sync fails", "[roundtrip][multi_entry][errors]") {
    conduit::io::BitWriter w;
    w.write_u16(0xDEAD);  // wrong sync (should be SYNC_A = 0xAA55)
    w.write_u8(1);         // cmd_id
    w.write_u32(0);        // param
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = multi_entry::CommandFrame::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("Multi-entry EventFrame wrong sync fails", "[roundtrip][multi_entry][errors]") {
    conduit::io::BitWriter w;
    w.write_u16(0xAA55);  // wrong sync for event (should be SYNC_B = 0x55AA)
    w.write_u8(10);        // evt_id
    w.write_u32(0);        // value
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = multi_entry::EventFrame::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("Multi-entry CommandFrame unknown cmd_id fails", "[roundtrip][multi_entry][errors]") {
    conduit::io::BitWriter w;
    w.write_u16(0xAA55);  // SYNC_A
    w.write_u8(99);        // unknown cmd_id
    w.write_u32(0);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = multi_entry::CommandFrame::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("Multi-entry CommandFrame wire format", "[roundtrip][multi_entry][wire]") {
    multi_entry::Cmd1Body cmd1;
    cmd1.set_param(0x01020304);
    auto frame = multi_entry::CommandFrame::wrap(cmd1);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    // sync(2) + cmd_id(1) + param(4) = 7
    CHECK(bytes.size() == 7);
    CHECK(bytes[0] == 0xAA); // SYNC_A high byte
    CHECK(bytes[1] == 0x55); // SYNC_A low byte
    CHECK(bytes[2] == multi_entry::CMD_1);
    CHECK(bytes[3] == 0x01); // param big-endian
}

// ============================================================================
// Section: Auto Sequence (auto_sequence fixture)
// ============================================================================

TEST_CASE("Auto-sequence Frame roundtrip", "[roundtrip][auto_seq]") {
    auto_seq::BodyA body;
    body.set_data(0x5678);
    auto frame = auto_seq::Frame::wrap(body);
    frame.set_seq(42);

    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = auto_seq::Frame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->sync() == auto_seq::SYNC);
    CHECK(decoded->seq() == 42);
    CHECK(decoded->tag() == auto_seq::MSG_A);
    auto& decoded_body = std::get<auto_seq::BodyA>(decoded->body());
    CHECK(decoded_body.data() == 0x5678);
}

TEST_CASE("Auto-sequence Frame wire format", "[roundtrip][auto_seq][wire]") {
    auto_seq::BodyA body;
    body.set_data(0);
    auto frame = auto_seq::Frame::wrap(body);
    frame.set_seq(0);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);

    // sync(2) + seq(2) + tag(1) + data(2) = 7 bytes
    CHECK(bytes.size() == 7);
    CHECK(bytes[0] == 0xBE); // SYNC = 0xBEEF
    CHECK(bytes[1] == 0xEF);
    CHECK(bytes[4] == auto_seq::MSG_A); // tag
}

TEST_CASE("Auto-sequence session increments sequence", "[roundtrip][auto_seq][session]") {
    auto session = auto_seq::create_frame_session();
    REQUIRE(session != nullptr);

    auto_seq::BodyA body;
    body.set_data(100);

    uint64_t body_a_id = 0;
    for (auto id : session->leaf_type_ids()) {
        if (session->type_name(id) == "BodyA") {
            body_a_id = id;
            break;
        }
    }
    REQUIRE(body_a_id != 0);

    // First encode: seq should be 0
    auto wrap1 = session->encode_wrap(body_a_id, std::any{body});
    REQUIRE(wrap1.has_value());
    auto dec1 = auto_seq::Frame::decode_bytes(*wrap1);
    REQUIRE(dec1.has_value());
    CHECK(dec1->seq() == 0);

    // Second encode: seq should be 1
    auto wrap2 = session->encode_wrap(body_a_id, std::any{body});
    REQUIRE(wrap2.has_value());
    auto dec2 = auto_seq::Frame::decode_bytes(*wrap2);
    REQUIRE(dec2.has_value());
    CHECK(dec2->seq() == 1);

    // Third encode: seq should be 2
    auto wrap3 = session->encode_wrap(body_a_id, std::any{body});
    REQUIRE(wrap3.has_value());
    auto dec3 = auto_seq::Frame::decode_bytes(*wrap3);
    REQUIRE(dec3.has_value());
    CHECK(dec3->seq() == 2);
}

TEST_CASE("Auto-sequence session reset resets sequence", "[roundtrip][auto_seq][session]") {
    auto session = auto_seq::create_frame_session();

    auto_seq::BodyA body;
    body.set_data(0);

    uint64_t body_a_id = 0;
    for (auto id : session->leaf_type_ids()) {
        if (session->type_name(id) == "BodyA") {
            body_a_id = id;
            break;
        }
    }
    REQUIRE(body_a_id != 0);

    // Advance sequence
    session->encode_wrap(body_a_id, std::any{body});
    session->encode_wrap(body_a_id, std::any{body});

    // Reset
    session->reset();

    // Next encode should restart at 0
    auto wrapped = session->encode_wrap(body_a_id, std::any{body});
    REQUIRE(wrapped.has_value());
    auto decoded = auto_seq::Frame::decode_bytes(*wrapped);
    REQUIRE(decoded.has_value());
    CHECK(decoded->seq() == 0);
}

TEST_CASE("Auto-sequence session decode_frame roundtrip", "[roundtrip][auto_seq][session]") {
    auto session = auto_seq::create_frame_session();

    auto_seq::BodyA body;
    body.set_data(0x9999);

    uint64_t body_a_id = 0;
    for (auto id : session->leaf_type_ids()) {
        if (session->type_name(id) == "BodyA") {
            body_a_id = id;
            break;
        }
    }
    REQUIRE(body_a_id != 0);

    auto wrapped = session->encode_wrap(body_a_id, std::any{body});
    REQUIRE(wrapped.has_value());

    auto decoded = session->decode_frame(*wrapped);
    REQUIRE(decoded.has_value());
    REQUIRE(!decoded->empty());
    CHECK(decoded->front().type_name == "BodyA");

    auto* payload = std::any_cast<auto_seq::BodyA>(&decoded->front().payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->data() == 0x9999);
}

TEST_CASE("Auto-sequence Frame wrong sync fails", "[roundtrip][auto_seq][errors]") {
    conduit::io::BitWriter w;
    w.write_u16(0xDEAD);  // wrong sync (should be 0xBEEF)
    w.write_u16(0);       // seq
    w.write_u8(1);        // tag
    w.write_u16(0);       // data
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = auto_seq::Frame::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

// ============================================================================
// Section: Enum Arrays (enum_arrays fixture)
// ============================================================================

TEST_CASE("EnumArrayMsg fixed array roundtrip", "[roundtrip][enum_arrays]") {
    using cc = enum_arrays::command_code;
    enum_arrays::EnumArrayMsg msg;
    msg.set_count(2);

    // Fixed-size array of 4 enums
    msg.set_commands({cc::read, cc::write, cc::reset, cc::nop});

    // Variable-size array of 2 enums (count-from="count")
    msg.set_dynamic_commands({cc::read, cc::reset});

    // Fixed BCD array of 3 values
    msg.set_bcd_values({12, 34, 56});

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = enum_arrays::EnumArrayMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->count() == 2);

    auto& cmds = decoded->commands();
    REQUIRE(cmds.size() == 4);
    CHECK(cmds[0] == cc::read);
    CHECK(cmds[1] == cc::write);
    CHECK(cmds[2] == cc::reset);
    CHECK(cmds[3] == cc::nop);

    auto& dyn = decoded->dynamic_commands();
    REQUIRE(dyn.size() == 2);
    CHECK(dyn[0] == cc::read);
    CHECK(dyn[1] == cc::reset);

    auto& bcd = decoded->bcd_values();
    REQUIRE(bcd.size() == 3);
    CHECK(bcd[0] == 12);
    CHECK(bcd[1] == 34);
    CHECK(bcd[2] == 56);
}

TEST_CASE("EnumArrayMsg all-zero roundtrip", "[roundtrip][enum_arrays]") {
    using cc = enum_arrays::command_code;
    enum_arrays::EnumArrayMsg msg;
    msg.set_count(0);
    msg.set_commands({cc::nop, cc::nop, cc::nop, cc::nop});
    msg.set_dynamic_commands({});
    msg.set_bcd_values({0, 0, 0});

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = enum_arrays::EnumArrayMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->count() == 0);
    CHECK(decoded->commands().size() == 4);
    CHECK(decoded->dynamic_commands().empty());
    CHECK(decoded->bcd_values().size() == 3);
}

TEST_CASE("EnumArrayMsg wire format size", "[roundtrip][enum_arrays][wire]") {
    using cc = enum_arrays::command_code;
    enum_arrays::EnumArrayMsg msg;
    msg.set_count(1);
    msg.set_commands({cc::nop, cc::nop, cc::nop, cc::nop});   // 4 * 1 byte = 4 bytes
    msg.set_dynamic_commands({cc::nop});      // 1 * 1 byte = 1 byte
    msg.set_bcd_values({0, 0, 0});     // 3 * 1 byte = 3 bytes (8-bit BCD)

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // count(1) + commands(4) + dynamic(1) + bcd(3) = 9
    CHECK(bytes.size() == 9);
}

// ============================================================================
// Section: Signed Length (signed_length fixture)
// ============================================================================

TEST_CASE("SignedFrame DataBody roundtrip", "[roundtrip][signed_length]") {
    signed_length::DataBody body;
    body.set_value(0x1234);
    auto frame = signed_length::SignedFrame::wrap(body);
    frame.set_length(2);  // DataBody is 2 bytes

    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = signed_length::SignedFrame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->sync() == signed_length::SYNC);
    CHECK(decoded->length() == 2);
    CHECK(decoded->msg_type() == signed_length::MSG_A);
    auto& decoded_body = std::get<signed_length::DataBody>(decoded->body());
    CHECK(decoded_body.value() == 0x1234);
}

TEST_CASE("SignedFrame negative length roundtrip", "[roundtrip][signed_length]") {
    // Signed length field can hold negative values (unusual but valid per spec)
    signed_length::DataBody body;
    body.set_value(0xFFFF);
    auto frame = signed_length::SignedFrame::wrap(body);
    frame.set_length(-1);  // Unusual but tests signed field handling

    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = signed_length::SignedFrame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->length() == -1);
}

TEST_CASE("SignedFrame wire format", "[roundtrip][signed_length][wire]") {
    signed_length::DataBody body;
    body.set_value(0);
    auto frame = signed_length::SignedFrame::wrap(body);
    frame.set_length(0);

    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // sync(2) + length(2) + msg-type(1) + value(2) = 7
    CHECK(bytes.size() == 7);
    // sync = 0xDEAD
    CHECK(bytes[0] == 0xDE);
    CHECK(bytes[1] == 0xAD);
}

TEST_CASE("SignedFrame wrong sync fails", "[roundtrip][signed_length][errors]") {
    conduit::io::BitWriter w;
    w.write_u16(0x0000);  // wrong sync (should be 0xDEAD)
    w.write_u16(2);       // length
    w.write_u8(1);        // msg-type = MSG_A
    w.write_u16(0);       // value
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = signed_length::SignedFrame::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

// ============================================================================
// Section: Send-only leaf (send_only_leaf fixture)
// ============================================================================

TEST_CASE("SendOnly Frame wrap SendBody roundtrip", "[roundtrip][send_only_leaf]") {
    send_only::SendBody body;
    body.set_x(0x1234);

    auto frame = send_only::Frame::wrap(body);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = send_only::Frame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->tag() == send_only::TYPE_A);
    auto& decoded_body = std::get<send_only::SendBody>(decoded->body());
    CHECK(decoded_body.x() == 0x1234);
}

TEST_CASE("SendOnly Frame wrap RecvBody roundtrip", "[roundtrip][send_only_leaf]") {
    send_only::RecvBody body;
    body.set_y(0xABCD);

    auto frame = send_only::Frame::wrap(body);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = send_only::Frame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->tag() == send_only::TYPE_B);
    auto& decoded_body = std::get<send_only::RecvBody>(decoded->body());
    CHECK(decoded_body.y() == 0xABCD);
}

TEST_CASE("SendOnly Frame wire format", "[roundtrip][send_only_leaf][wire]") {
    send_only::SendBody body;
    body.set_x(0x0102);

    auto frame = send_only::Frame::wrap(body);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // tag(1) + x(2) = 3 bytes
    CHECK(bytes.size() == 3);
    CHECK(bytes[0] == send_only::TYPE_A);  // tag = 1
}

TEST_CASE("SendOnly Frame decode with unknown tag fails", "[roundtrip][send_only_leaf][errors]") {
    conduit::io::BitWriter w;
    w.write_u8(99);       // unknown tag value
    w.write_u16(0x0000);  // payload
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = send_only::Frame::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("SendOnly Frame decode truncated buffer fails", "[roundtrip][send_only_leaf][errors]") {
    conduit::io::BitWriter w;
    w.write_u8(send_only::TYPE_A);  // tag for SendBody
    // Missing payload bytes
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = send_only::Frame::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("SendOnly Frame decode empty buffer fails", "[roundtrip][send_only_leaf][errors]") {
    std::vector<uint8_t> empty;
    auto decoded = send_only::Frame::decode_bytes(empty);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("SendOnly Frame boundary values", "[roundtrip][send_only_leaf]") {
    // Test with max uint16 value
    send_only::SendBody body;
    body.set_x(0xFFFF);
    auto frame = send_only::Frame::wrap(body);
    auto enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = send_only::Frame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    auto& decoded_body = std::get<send_only::SendBody>(decoded->body());
    CHECK(decoded_body.x() == 0xFFFF);

    // Test with zero value
    body.set_x(0);
    frame = send_only::Frame::wrap(body);
    enc_result = frame.encode_bytes();
    REQUIRE(enc_result.has_value());
    bytes = std::move(*enc_result);
    decoded = send_only::Frame::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    auto& decoded_zero = std::get<send_only::SendBody>(decoded->body());
    CHECK(decoded_zero.x() == 0);
}

// ============================================================================
// Section: Non-overlapping ranges (non_overlap_ranges fixture)
// ============================================================================

TEST_CASE("NonOverlapRanges Msg with tag in range A roundtrip", "[roundtrip][non_overlap_ranges]") {
    // Tag value 1 (LO_A) through 5 (HI_A) should decode as BodyA
    conduit::io::BitWriter w;
    w.write_u8(1);        // tag = LO_A
    w.write_u16(0x1234);  // BodyA.x
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = non_overlap_ranges::Msg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->tag() == 1);
    auto& body = std::get<non_overlap_ranges::BodyA>(decoded->body());
    CHECK(body.x() == 0x1234);
}

TEST_CASE("NonOverlapRanges Msg with tag in range B roundtrip", "[roundtrip][non_overlap_ranges]") {
    // Tag value 6 (LO_B) through 10 (HI_B) should decode as BodyB
    conduit::io::BitWriter w;
    w.write_u8(6);        // tag = LO_B
    w.write_u16(0xABCD);  // BodyB.y
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = non_overlap_ranges::Msg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->tag() == 6);
    auto& body = std::get<non_overlap_ranges::BodyB>(decoded->body());
    CHECK(body.y() == 0xABCD);
}

TEST_CASE("NonOverlapRanges Msg range A upper boundary", "[roundtrip][non_overlap_ranges]") {
    // Tag = 5 (HI_A) should still decode as BodyA
    conduit::io::BitWriter w;
    w.write_u8(5);        // tag = HI_A
    w.write_u16(0x9999);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = non_overlap_ranges::Msg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->tag() == 5);
    auto& body = std::get<non_overlap_ranges::BodyA>(decoded->body());
    CHECK(body.x() == 0x9999);
}

TEST_CASE("NonOverlapRanges Msg range B upper boundary", "[roundtrip][non_overlap_ranges]") {
    // Tag = 10 (HI_B) should decode as BodyB
    conduit::io::BitWriter w;
    w.write_u8(10);       // tag = HI_B
    w.write_u16(0x7777);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = non_overlap_ranges::Msg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->tag() == 10);
    auto& body = std::get<non_overlap_ranges::BodyB>(decoded->body());
    CHECK(body.y() == 0x7777);
}

TEST_CASE("NonOverlapRanges Msg range mid-values", "[roundtrip][non_overlap_ranges]") {
    // Tag = 3 (mid of range A: 1..5) should decode as BodyA
    conduit::io::BitWriter w;
    w.write_u8(3);
    w.write_u16(0x5555);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = non_overlap_ranges::Msg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    auto& body_a = std::get<non_overlap_ranges::BodyA>(decoded->body());
    CHECK(body_a.x() == 0x5555);

    // Tag = 8 (mid of range B: 6..10) should decode as BodyB
    conduit::io::BitWriter w2;
    w2.write_u8(8);
    w2.write_u16(0x6666);
    auto finish_result2 = w2.finish();
    REQUIRE(finish_result2.has_value());
    auto bytes2 = std::move(*finish_result2);

    auto decoded2 = non_overlap_ranges::Msg::decode_bytes(bytes2);
    REQUIRE(decoded2.has_value());
    auto& body_b = std::get<non_overlap_ranges::BodyB>(decoded2->body());
    CHECK(body_b.y() == 0x6666);
}

TEST_CASE("NonOverlapRanges Msg tag outside all ranges fails", "[roundtrip][non_overlap_ranges][errors]") {
    // Tag = 0 is below range A (1..5), should fail
    conduit::io::BitWriter w;
    w.write_u8(0);
    w.write_u16(0x0000);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = non_overlap_ranges::Msg::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("NonOverlapRanges Msg tag in gap between ranges fails", "[roundtrip][non_overlap_ranges][errors]") {
    // There is no gap between ranges A (1..5) and B (6..10) — they are adjacent.
    // But tag = 11 is above HI_B, so it should fail.
    conduit::io::BitWriter w;
    w.write_u8(11);
    w.write_u16(0x0000);
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = non_overlap_ranges::Msg::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("NonOverlapRanges Msg wire format", "[roundtrip][non_overlap_ranges][wire]") {
    conduit::io::BitWriter w;
    w.write_u8(2);        // tag in range A
    w.write_u16(0x0102);  // BodyA.x
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    // tag(1) + x(2) = 3 bytes
    CHECK(bytes.size() == 3);

    auto decoded = non_overlap_ranges::Msg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    auto& body = std::get<non_overlap_ranges::BodyA>(decoded->body());
    CHECK(body.x() == 0x0102);
}

TEST_CASE("NonOverlapRanges Msg decode empty buffer fails", "[roundtrip][non_overlap_ranges][errors]") {
    std::vector<uint8_t> empty;
    auto decoded = non_overlap_ranges::Msg::decode_bytes(empty);
    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("NonOverlapRanges Msg decode truncated buffer fails", "[roundtrip][non_overlap_ranges][errors]") {
    conduit::io::BitWriter w;
    w.write_u8(1);  // tag = LO_A, but no payload bytes
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = non_overlap_ranges::Msg::decode_bytes(bytes);
    CHECK_FALSE(decoded.has_value());
}
