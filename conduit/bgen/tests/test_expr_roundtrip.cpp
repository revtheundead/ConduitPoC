// SPDX-License-Identifier: MIT
// Bgen tests - Expression feature roundtrip verification

#include <catch2/catch_test_macros.hpp>
#include <conduit/core/error.hpp>
#include <conduit/io/bit_reader.hpp>
#include <conduit/io/bit_writer.hpp>
#include <cstdint>
#include <vector>

#include "expr_features/messages.hpp"
#include "expr_features/constants.hpp"

// ============================================================================
// Section: Arithmetic expression in length-from
// ============================================================================

TEST_CASE("ArithmeticLengthMsg TypeA roundtrip", "[roundtrip][expr]") {
    expr_features::ArithmeticLengthMsg msg;
    msg.set_total_length(6);   // header-size(4) + ItemA body(2) = 6
    msg.set_header_size(4);
    msg.set_tag(expr_features::TYPE_A);
    expr_features::ItemA item;
    item.set_val(0x1234);
    msg.set_body(expr_features::ArithmeticLengthMsg_bodyVariant{item});

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = expr_features::ArithmeticLengthMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->total_length() == 6);
    CHECK(decoded->header_size() == 4);
    CHECK(decoded->tag() == expr_features::TYPE_A);
    auto& body = std::get<expr_features::ItemA>(decoded->body());
    CHECK(body.val() == 0x1234);
}

TEST_CASE("ArithmeticLengthMsg TypeB roundtrip", "[roundtrip][expr]") {
    expr_features::ArithmeticLengthMsg msg;
    msg.set_total_length(8);   // header-size(4) + ItemB body(4) = 8
    msg.set_header_size(4);
    msg.set_tag(expr_features::TYPE_B);
    expr_features::ItemB item;
    item.set_tag(0xDEADBEEF);
    msg.set_body(expr_features::ArithmeticLengthMsg_bodyVariant{item});

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = expr_features::ArithmeticLengthMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    auto& body = std::get<expr_features::ItemB>(decoded->body());
    CHECK(body.tag() == 0xDEADBEEF);
}

// ============================================================================
// Section: Comparison operators in present-when
// ============================================================================

TEST_CASE("ComparisonMsg all conditionals present", "[roundtrip][expr]") {
    expr_features::ComparisonMsg msg;
    msg.set_flags(1);
    msg.set_level(5);
    msg.set_opt_a(0x1111);  // flags != 0 → present
    msg.set_opt_b(0x2222);  // level > 3 → present
    msg.set_opt_c(0x3333);  // level >= 5 → present
    msg.set_opt_d(0x4444);  // level < 10 → present

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = expr_features::ComparisonMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->flags() == 1);
    CHECK(decoded->level() == 5);
    CHECK(decoded->opt_a() == 0x1111);
    CHECK(decoded->opt_b() == 0x2222);
    CHECK(decoded->opt_c() == 0x3333);
    CHECK(decoded->opt_d() == 0x4444);
}

TEST_CASE("ComparisonMsg some conditionals absent", "[roundtrip][expr]") {
    expr_features::ComparisonMsg msg;
    msg.set_flags(0);    // flags == 0 → opt-a absent
    msg.set_level(2);    // level <= 3 → opt-b absent; level < 5 → opt-c absent; level < 10 → opt-d present
    msg.set_opt_d(0x9999);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = expr_features::ComparisonMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->flags() == 0);
    CHECK(decoded->level() == 2);
    CHECK_FALSE(decoded->has_opt_a());
    CHECK_FALSE(decoded->has_opt_b());
    CHECK_FALSE(decoded->has_opt_c());
    CHECK(decoded->opt_d() == 0x9999);
}

// ============================================================================
// Section: Bitwise AND in present-when
// ============================================================================

TEST_CASE("BitwiseMsg extended present", "[roundtrip][expr]") {
    expr_features::BitwiseMsg msg;
    msg.set_mask(0x01);
    msg.set_base(0xAAAA);
    msg.set_extended(0xBBBB);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = expr_features::BitwiseMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->mask() == 0x01);
    CHECK(decoded->base() == 0xAAAA);
    CHECK(decoded->extended() == 0xBBBB);
}

TEST_CASE("BitwiseMsg extended absent", "[roundtrip][expr]") {
    expr_features::BitwiseMsg msg;
    msg.set_mask(0x00);
    msg.set_base(0xCCCC);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = expr_features::BitwiseMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->mask() == 0x00);
    CHECK(decoded->base() == 0xCCCC);
    CHECK_FALSE(decoded->has_extended());
}

// ============================================================================
// Section: Logical operators in present-when
// ============================================================================

TEST_CASE("LogicalMsg conditional present", "[roundtrip][expr]") {
    expr_features::LogicalMsg msg;
    msg.set_flag_a(1);
    msg.set_flag_b(1);
    msg.set_value(0x5555);
    msg.set_conditional(0x6666);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = expr_features::LogicalMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->flag_a() == 1);
    CHECK(decoded->flag_b() == 1);
    CHECK(decoded->value() == 0x5555);
    CHECK(decoded->conditional() == 0x6666);
}

TEST_CASE("LogicalMsg conditional absent (one flag zero)", "[roundtrip][expr]") {
    expr_features::LogicalMsg msg;
    msg.set_flag_a(1);
    msg.set_flag_b(0);
    msg.set_value(0x7777);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = expr_features::LogicalMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->flag_a() == 1);
    CHECK(decoded->flag_b() == 0);
    CHECK(decoded->value() == 0x7777);
    CHECK_FALSE(decoded->has_conditional());
}

// ============================================================================
// Section: Multiplication in count-from
// ============================================================================

TEST_CASE("MulCountMsg 3x4 cells roundtrip", "[roundtrip][expr]") {
    expr_features::MulCountMsg msg;
    msg.set_rows(3);
    msg.set_cols(4);
    auto& cells = msg.mutable_cells();
    for (int i = 0; i < 12; i++) {
        cells.push_back(static_cast<uint16_t>(i * 100));
    }

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = expr_features::MulCountMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->rows() == 3);
    CHECK(decoded->cols() == 4);
    REQUIRE(decoded->cells().size() == 12);
    for (int i = 0; i < 12; i++) {
        CHECK(decoded->cells()[static_cast<size_t>(i)] == static_cast<uint16_t>(i * 100));
    }
}

// ============================================================================
// Section: Wire-byte verification
// ============================================================================

TEST_CASE("ArithmeticLengthMsg TypeA wire bytes", "[wire][expr]") {
    expr_features::ArithmeticLengthMsg msg;
    msg.set_total_length(6);   // BE16: 0x00, 0x06
    msg.set_header_size(4);    // 0x04
    msg.set_tag(expr_features::TYPE_A); // 0x01
    expr_features::ItemA item;
    item.set_val(0x1234);      // BE16: 0x12, 0x34
    msg.set_body(expr_features::ArithmeticLengthMsg_bodyVariant{item});

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    REQUIRE(bytes.size() == 6);
    CHECK(bytes[0] == 0x00); // total_length high
    CHECK(bytes[1] == 0x06); // total_length low
    CHECK(bytes[2] == 0x04); // header_size
    CHECK(bytes[3] == 0x01); // tag = TYPE_A
    CHECK(bytes[4] == 0x12); // ItemA.val high
    CHECK(bytes[5] == 0x34); // ItemA.val low
}

TEST_CASE("ComparisonMsg variable size - all present", "[wire][expr]") {
    expr_features::ComparisonMsg msg;
    msg.set_flags(1);
    msg.set_level(5);
    msg.set_opt_a(0x1111);
    msg.set_opt_b(0x2222);
    msg.set_opt_c(0x3333);
    msg.set_opt_d(0x4444);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // flags(1) + level(1) + 4×uint16(8) = 10 bytes
    CHECK(bytes.size() == 10);
}

TEST_CASE("ComparisonMsg variable size - some absent", "[wire][expr]") {
    expr_features::ComparisonMsg msg;
    msg.set_flags(0);    // opt_a absent
    msg.set_level(2);    // opt_b absent (level > 3 false), opt_c absent (level >= 5 false), opt_d present (level < 10)
    msg.set_opt_d(0x9999);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // flags(1) + level(1) + opt_d(2) = 4 bytes
    CHECK(bytes.size() == 4);
}

TEST_CASE("MulCountMsg wire bytes", "[wire][expr]") {
    expr_features::MulCountMsg msg;
    msg.set_rows(2);
    msg.set_cols(3);
    auto& cells = msg.mutable_cells();
    for (uint16_t i = 1; i <= 6; i++) {
        cells.push_back(i);
    }

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // rows(1) + cols(1) + 6×uint16(12) = 14 bytes
    REQUIRE(bytes.size() == 14);
    CHECK(bytes[0] == 2);    // rows
    CHECK(bytes[1] == 3);    // cols
    CHECK(bytes[2] == 0x00); CHECK(bytes[3] == 0x01);   // cell 1
    CHECK(bytes[4] == 0x00); CHECK(bytes[5] == 0x02);   // cell 2
    CHECK(bytes[6] == 0x00); CHECK(bytes[7] == 0x03);   // cell 3
    CHECK(bytes[8] == 0x00); CHECK(bytes[9] == 0x04);   // cell 4
    CHECK(bytes[10] == 0x00); CHECK(bytes[11] == 0x05); // cell 5
    CHECK(bytes[12] == 0x00); CHECK(bytes[13] == 0x06); // cell 6
}

// ============================================================================
// Section: Error-path tests
// ============================================================================

TEST_CASE("ArithmeticLengthMsg truncated body", "[error][expr]") {
    // Craft wire data: total_length=6 but only provide 5 bytes
    std::vector<uint8_t> data = {0x00, 0x06, 0x04, 0x01, 0x12};
    auto decoded = expr_features::ArithmeticLengthMsg::decode_bytes(data);
    REQUIRE(!decoded.has_value());
}

TEST_CASE("ComparisonMsg truncated optional", "[error][expr]") {
    // flags=1 (opt_a present), level=5 (opt_b,c,d present)
    // Need flags(1)+level(1)+opt_a(2)+opt_b(2)+opt_c(2)+opt_d(2)=10 bytes
    // Provide only 4 bytes: flags + level + opt_a, missing opt_b
    std::vector<uint8_t> data = {0x01, 0x05, 0x11, 0x11};
    auto decoded = expr_features::ComparisonMsg::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::BufferUnderrun);
}

TEST_CASE("MulCountMsg truncated cells", "[error][expr]") {
    // rows=2, cols=3 → expect 6 cells = 12 bytes after rows+cols
    // Provide rows+cols+4 bytes = 6 bytes total (missing 8 bytes of cells)
    std::vector<uint8_t> data = {0x02, 0x03, 0x00, 0x01, 0x00, 0x02};
    auto decoded = expr_features::MulCountMsg::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::BufferUnderrun);
}

// ============================================================================
// Section: Edge-case tests
// ============================================================================

TEST_CASE("ComparisonMsg boundary level=3", "[edge][expr]") {
    expr_features::ComparisonMsg msg;
    msg.set_flags(1);  // opt_a present (flags != 0)
    msg.set_level(3);  // opt_b absent (level > 3 false), opt_c absent (level >= 5 false), opt_d present (level < 10)
    msg.set_opt_a(0xAAAA);
    msg.set_opt_d(0xDDDD);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = expr_features::ComparisonMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->has_opt_a());
    CHECK_FALSE(decoded->has_opt_b());
    CHECK_FALSE(decoded->has_opt_c());
    CHECK(decoded->has_opt_d());
    CHECK(decoded->opt_a() == 0xAAAA);
    CHECK(decoded->opt_d() == 0xDDDD);
}

TEST_CASE("MulCountMsg zero dimensions", "[edge][expr]") {
    expr_features::MulCountMsg msg;
    msg.set_rows(0);
    msg.set_cols(5);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    // rows(1) + cols(1) + 0 cells = 2 bytes
    CHECK(bytes.size() == 2);

    auto decoded = expr_features::MulCountMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->cells().empty());
}

// ============================================================================
// Section: RemainingMsg tests
// ============================================================================

TEST_CASE("RemainingMsg roundtrip", "[roundtrip][expr]") {
    expr_features::RemainingMsg msg;
    msg.set_header(0xAABBCCDD);
    msg.set_payload(0x42);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    auto decoded = expr_features::RemainingMsg::decode_bytes(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->header() == 0xAABBCCDD);
    CHECK(decoded->payload() == 0x42);
}

TEST_CASE("RemainingMsg wire bytes", "[wire][expr]") {
    expr_features::RemainingMsg msg;
    msg.set_header(0xAABBCCDD);
    msg.set_payload(0x42);

    auto enc_result = msg.encode_bytes();
    REQUIRE(enc_result.has_value());
    auto bytes = std::move(*enc_result);
    CHECK(bytes.size() == 5);
    CHECK(bytes[0] == 0xAA);
    CHECK(bytes[1] == 0xBB);
    CHECK(bytes[2] == 0xCC);
    CHECK(bytes[3] == 0xDD);
    CHECK(bytes[4] == 0x42);
}

TEST_CASE("RemainingMsg truncated decode fails", "[error][expr]") {
    std::vector<uint8_t> data = {0xAA, 0xBB};
    auto decoded = expr_features::RemainingMsg::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::BufferUnderrun);
}

TEST_CASE("BitwiseMsg truncated decode fails", "[error][expr]") {
    // mask=0x01 (extended present), need mask(1)+base(2)+extended(2)=5 bytes
    // Provide only 3 bytes
    std::vector<uint8_t> data = {0x01, 0xAA, 0xAA};
    auto decoded = expr_features::BitwiseMsg::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::BufferUnderrun);
}

TEST_CASE("LogicalMsg truncated decode fails", "[error][expr]") {
    // flag_a=1, flag_b=1 (conditional present), need flag_a(1)+flag_b(1)+value(2)+conditional(2)=6 bytes
    // Provide only 4 bytes
    std::vector<uint8_t> data = {0x01, 0x01, 0x55, 0x55};
    auto decoded = expr_features::LogicalMsg::decode_bytes(data);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::BufferUnderrun);
}

TEST_CASE("ArithmeticLengthMsg body size mismatch fails", "[error][expr]") {
    // total_length=10, header_size=4, tag=TYPE_A
    // sub_reader gets 10-4=6 bytes, but TypeA body is only 2 bytes
    // Should fail with ExactConsumptionFailed
    conduit::io::BitWriter w;
    w.write_u16(10, conduit::io::Endian::Big);  // total_length
    w.write_u8(4);                                // header_size
    w.write_u8(expr_features::TYPE_A);            // tag
    w.write_u16(0x1234, conduit::io::Endian::Big); // ItemA.val (2 bytes)
    w.write_u32(0, conduit::io::Endian::Big);      // 4 extra bytes
    auto finish_result = w.finish();
    REQUIRE(finish_result.has_value());
    auto bytes = std::move(*finish_result);

    auto decoded = expr_features::ArithmeticLengthMsg::decode_bytes(bytes);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == conduit::ErrorCode::ExactConsumptionFailed);
}
