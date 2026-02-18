// SPDX-License-Identifier: MIT
// Bgen tests - XML Parser

#include <catch2/catch_test_macros.hpp>
#include "../src/parser/xml_parser.hpp"
#include "../src/parser/expression_parser.hpp"
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

static std::string fixture_path(const std::string& name) {
    return (fs::path(BGEN_TEST_FIXTURES_DIR) / name).string();
}

TEST_CASE("Parse minimal BMDL file", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("minimal.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    CHECK(bmdl.has_protocol);
    CHECK(bmdl.protocol_name == "minimal");
    CHECK(bmdl.bmdl_version == "2.0");
    CHECK(bmdl.types.size() == 2);
    CHECK(bmdl.messages.size() == 1);
    CHECK(bmdl.messages[0].name == "SimpleMessage");
}

TEST_CASE("Parse choice protocol BMDL file", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("choice_protocol.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    CHECK(bmdl.protocol_name == "choice_test");
    CHECK(bmdl.constants.size() == 1); // SYNC
    CHECK(bmdl.types.size() == 3); // uint8, uint16, uint32

}

TEST_CASE("Parse message direction attributes", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("choice_protocol.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    REQUIRE(bmdl.messages.size() == 2);
    // AlphaBody has no direction (Both)
    CHECK(bmdl.messages[0].name == "AlphaBody");
    CHECK(bmdl.messages[0].direction == bgen::model::Direction::Both);
    // BetaBody has direction="receive"
    CHECK(bmdl.messages[1].name == "BetaBody");
    CHECK(bmdl.messages[1].direction == bgen::model::Direction::Receive);
}

TEST_CASE("Parse annotations", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("choice_protocol.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    // AlphaBody message should have a "group" annotation
    bool found_alpha = false;
    for (const auto& md : bmdl.messages) {
        if (md.name == "AlphaBody") {
            found_alpha = true;
            REQUIRE(md.annotations.size() == 1);
            CHECK(md.annotations[0].name == "group");
            CHECK(md.annotations[0].value == "control");
        }
    }
    CHECK(found_alpha);
}

TEST_CASE("Parse struct_features defaults and constants", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("struct_features.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    CHECK(bmdl.protocol_name == "struct_features");
    CHECK(bmdl.constants.size() == 2); // MAGIC, VERSION

    // Verify constants parsed correctly
    bool found_magic = false, found_version = false;
    for (const auto& c : bmdl.constants) {
        if (c.name == "MAGIC") {
            found_magic = true;
            CHECK(c.value == "0xCAFE");
            CHECK(c.type_ref == "uint16");
        }
        if (c.name == "VERSION") {
            found_version = true;
            CHECK(c.value == "3");
        }
    }
    CHECK(found_magic);
    CHECK(found_version);
}

TEST_CASE("Parse reserved and align elements", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("struct_features.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    // ConstrainedMessage has <reserved bits="8"/>
    bool found_reserved = false;
    for (const auto& msg : bmdl.messages) {
        if (msg.name == "ConstrainedMessage") {
            for (const auto& child : msg.children) {
                if (auto* r = std::get_if<bgen::model::Reserved>(&child)) {
                    found_reserved = true;
                    CHECK(r->bits == 8);
                }
            }
        }
    }
    CHECK(found_reserved);

    // AlignedMessage has <align to="2"/>
    bool found_align = false;
    for (const auto& msg : bmdl.messages) {
        if (msg.name == "AlignedMessage") {
            for (const auto& child : msg.children) {
                if (auto* a = std::get_if<bgen::model::Align>(&child)) {
                    found_align = true;
                    CHECK(a->to == 2);
                }
            }
        }
    }
    CHECK(found_align);
}

TEST_CASE("Parse present-when expression", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("struct_features.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    // ConditionalMessage has present-when="has-extra != 0"
    for (const auto& msg : bmdl.messages) {
        if (msg.name == "ConditionalMessage") {
            for (const auto& child : msg.children) {
                if (auto* f = std::get_if<bgen::model::Field>(&child)) {
                    if (f->name == "extra-value") {
                        REQUIRE(f->present_when != nullptr);
                        CHECK(f->present_when->op == bgen::model::ExprOp::Neq);
                    }
                }
            }
        }
    }
}

TEST_CASE("Parse count and count-from attributes", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("arrays_choices.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    for (const auto& msg : bmdl.messages) {
        if (msg.name == "FixedArrayMsg") {
            for (const auto& child : msg.children) {
                if (auto* a = std::get_if<bgen::model::ArrayDef>(&child)) {
                    CHECK(a->name == "points");
                    REQUIRE(a->fixed_count.has_value());
                    CHECK(*a->fixed_count == 3);
                }
            }
        }
        if (msg.name == "CountFromArrayMsg") {
            for (const auto& child : msg.children) {
                if (auto* a = std::get_if<bgen::model::ArrayDef>(&child)) {
                    CHECK(a->name == "items");
                    REQUIRE(a->count_from != nullptr);
                }
            }
        }
    }
}

TEST_CASE("Parse length-from on choice", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("arrays_choices.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    for (const auto& msg : bmdl.messages) {
        if (msg.name == "ChoiceMsg") {
            for (const auto& child : msg.children) {
                if (auto* c = std::get_if<bgen::model::ChoiceDef>(&child)) {
                    CHECK(c->name == "body");
                    REQUIRE(c->length_from != nullptr);
                    CHECK(c->switch_expr != nullptr);
                }
            }
        }
    }
}

TEST_CASE("Parse auto attribute", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("session_protocol.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    REQUIRE(bmdl.frames.size() == 1);
    const auto& frame = bmdl.frames[0];
    CHECK(frame.name == "Packet");
    for (const auto& child : frame.header_fields) {
        if (auto* f = std::get_if<bgen::model::Field>(&child)) {
            if (f->name == "seq") {
                REQUIRE(f->auto_expr.has_value());
                CHECK(f->auto_expr->kind == bgen::model::AutoKind::Increment);
            }
        }
    }
}

TEST_CASE("Parse bitmap struct with ext attribute", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("bitmap_fx.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    // BitmapItems struct should have bitmap with ext
    bool found_bitmap = false;
    for (const auto& sd : bmdl.structs) {
        if (sd.name == "BitmapItems") {
            found_bitmap = true;
            CHECK(sd.is_bitmap);
            CHECK(sd.bitmap_bits == 8);
            REQUIRE(sd.bitmap_ext.has_value());
            CHECK(*sd.bitmap_ext == 0);
        }
    }
    CHECK(found_bitmap);
}

TEST_CASE("Reject non-existent file", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file("nonexistent_file.bmdl.xml");
    REQUIRE_FALSE(result.has_value());
}

// ============================================================================
// Expression feature parsing
// ============================================================================

TEST_CASE("Parse arithmetic expression in length-from", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("expr_features.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    for (const auto& msg : bmdl.messages) {
        if (msg.name == "ArithmeticLengthMsg") {
            for (const auto& child : msg.children) {
                if (auto* c = std::get_if<bgen::model::ChoiceDef>(&child)) {
                    CHECK(c->name == "body");
                    REQUIRE(c->length_from != nullptr);
                    // Should be Sub(FieldRef("total-length"), FieldRef("header-size"))
                    CHECK(c->length_from->op == bgen::model::ExprOp::Sub);
                    REQUIRE(c->length_from->left != nullptr);
                    CHECK(c->length_from->left->op == bgen::model::ExprOp::FieldRef);
                    CHECK(c->length_from->left->name == "total-length");
                    REQUIRE(c->length_from->right != nullptr);
                    CHECK(c->length_from->right->op == bgen::model::ExprOp::FieldRef);
                    CHECK(c->length_from->right->name == "header-size");
                }
            }
        }
    }
}

TEST_CASE("Parse comparison operators in present-when", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("expr_features.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    for (const auto& msg : bmdl.messages) {
        if (msg.name == "ComparisonMsg") {
            std::map<std::string, bgen::model::ExprOp> expected_ops = {
                {"opt-a", bgen::model::ExprOp::Neq},
                {"opt-b", bgen::model::ExprOp::Gt},
                {"opt-c", bgen::model::ExprOp::Gte},
                {"opt-d", bgen::model::ExprOp::Lt},
            };
            for (const auto& child : msg.children) {
                if (auto* f = std::get_if<bgen::model::Field>(&child)) {
                    auto it = expected_ops.find(f->name);
                    if (it != expected_ops.end()) {
                        REQUIRE(f->present_when != nullptr);
                        CHECK(f->present_when->op == it->second);
                    }
                }
            }
        }
    }
}

TEST_CASE("Parse bitwise AND expression in present-when", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("expr_features.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    for (const auto& msg : bmdl.messages) {
        if (msg.name == "BitwiseMsg") {
            for (const auto& child : msg.children) {
                if (auto* f = std::get_if<bgen::model::Field>(&child)) {
                    if (f->name == "extended") {
                        REQUIRE(f->present_when != nullptr);
                        // (mask & 0x01) != 0 -> Neq with left = BitAnd
                        CHECK(f->present_when->op == bgen::model::ExprOp::Neq);
                        REQUIRE(f->present_when->left != nullptr);
                        CHECK(f->present_when->left->op == bgen::model::ExprOp::BitAnd);
                    }
                }
            }
        }
    }
}

TEST_CASE("Parse logical AND in present-when", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("expr_features.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    for (const auto& msg : bmdl.messages) {
        if (msg.name == "LogicalMsg") {
            for (const auto& child : msg.children) {
                if (auto* f = std::get_if<bgen::model::Field>(&child)) {
                    if (f->name == "conditional") {
                        REQUIRE(f->present_when != nullptr);
                        CHECK(f->present_when->op == bgen::model::ExprOp::LogAnd);
                        // Left and right should be Neq comparisons
                        REQUIRE(f->present_when->left != nullptr);
                        CHECK(f->present_when->left->op == bgen::model::ExprOp::Neq);
                        REQUIRE(f->present_when->right != nullptr);
                        CHECK(f->present_when->right->op == bgen::model::ExprOp::Neq);
                    }
                }
            }
        }
    }
}

TEST_CASE("Parse multiplication in count-from", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("expr_features.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    for (const auto& msg : bmdl.messages) {
        if (msg.name == "MulCountMsg") {
            for (const auto& child : msg.children) {
                if (auto* a = std::get_if<bgen::model::ArrayDef>(&child)) {
                    CHECK(a->name == "cells");
                    REQUIRE(a->count_from != nullptr);
                    CHECK(a->count_from->op == bgen::model::ExprOp::Mul);
                    REQUIRE(a->count_from->left != nullptr);
                    CHECK(a->count_from->left->name == "rows");
                    REQUIRE(a->count_from->right != nullptr);
                    CHECK(a->count_from->right->name == "cols");
                }
            }
        }
    }
}

TEST_CASE("Parse remaining keyword in length-from", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("expr_features.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    for (const auto& msg : bmdl.messages) {
        if (msg.name == "RemainingMsg") {
            for (const auto& child : msg.children) {
                if (auto* f = std::get_if<bgen::model::Field>(&child)) {
                    if (f->name == "payload") {
                        REQUIRE(f->length_from != nullptr);
                        CHECK(f->length_from->op == bgen::model::ExprOp::Remaining);
                    }
                }
            }
        }
    }
}

// ============================================================================
// String feature parsing
// ============================================================================

TEST_CASE("Parse string type attributes", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("string_features.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;

    bool found_name = false, found_label = false, found_packed = false, found_term = false;
    for (const auto& t : bmdl.types) {
        if (t.name == "name-str") {
            found_name = true;
            CHECK(t.base == bgen::model::PrimitiveBase::String);
            REQUIRE(t.length.has_value());
            CHECK(*t.length == 20);
            CHECK(t.encoding == bgen::model::StringEncoding::Ascii);
            CHECK(t.padding == bgen::model::StringPadding::Null);
            CHECK(t.trim == bgen::model::StringTrim::Right);
        }
        if (t.name == "label-str") {
            found_label = true;
            CHECK(t.encoding == bgen::model::StringEncoding::Utf8);
            CHECK(t.padding == bgen::model::StringPadding::Space);
            CHECK(t.trim == bgen::model::StringTrim::Both);
        }
        if (t.name == "packed-str") {
            found_packed = true;
            REQUIRE(t.char_bits.has_value());
            CHECK(*t.char_bits == 6);
        }
        if (t.name == "term-str") {
            found_term = true;
            REQUIRE(t.terminated.has_value());
            CHECK(*t.terminated == "0x00");
        }
    }
    CHECK(found_name);
    CHECK(found_label);
    CHECK(found_packed);
    CHECK(found_term);
}

TEST_CASE("Parse string max-length attribute", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("string_features.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    for (const auto& t : bmdl.types) {
        if (t.name == "bounded-str") {
            REQUIRE(t.max_length.has_value());
            CHECK(*t.max_length == 32);
        }
    }
}

TEST_CASE("Parse inline string overrides on field", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("string_features.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    for (const auto& msg : bmdl.messages) {
        if (msg.name == "InlineStringMsg") {
            for (const auto& child : msg.children) {
                if (auto* f = std::get_if<bgen::model::Field>(&child)) {
                    if (f->name == "inline-name") {
                        REQUIRE(f->encoding.has_value());
                        CHECK(*f->encoding == bgen::model::StringEncoding::Utf8);
                        REQUIRE(f->padding.has_value());
                        CHECK(*f->padding == bgen::model::StringPadding::Space);
                        REQUIRE(f->trim.has_value());
                        CHECK(*f->trim == bgen::model::StringTrim::Left);
                    }
                    if (f->name == "prefix-str") {
                        REQUIRE(f->length_prefix.has_value());
                        CHECK(*f->length_prefix == "uint8");
                    }
                }
            }
        }
    }
}

// ============================================================================
// FX block parsing
// ============================================================================

TEST_CASE("Parse FX extension block", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("fx_block.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    REQUIRE(bmdl.messages.size() >= 1);

    for (const auto& msg : bmdl.messages) {
        if (msg.name == "FxMessage") {
            bool found_fx = false;
            for (const auto& child : msg.children) {
                if (auto* fx = std::get_if<bgen::model::FxBlock>(&child)) {
                    found_fx = true;
                    // FX block should have 3 children
                    CHECK(fx->children.size() == 3);
                }
            }
            CHECK(found_fx);
        }
    }
}

// ============================================================================
// Default and initial value parsing
// ============================================================================

TEST_CASE("Parse default attribute on field", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("default_initial.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    for (const auto& msg : bmdl.messages) {
        if (msg.name == "DefaultMsg") {
            for (const auto& child : msg.children) {
                if (auto* f = std::get_if<bgen::model::Field>(&child)) {
                    if (f->name == "version") {
                        REQUIRE(f->default_value.has_value());
                        CHECK(*f->default_value == "1");
                    }
                    if (f->name == "priority") {
                        REQUIRE(f->default_value.has_value());
                        CHECK(*f->default_value == "0");
                    }
                    if (f->name == "data") {
                        CHECK_FALSE(f->default_value.has_value());
                    }
                }
            }
        }
    }
}

TEST_CASE("Parse initial attribute on field is rejected", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("invalid_initial_attr.bmdl.xml"));
    REQUIRE_FALSE(result.has_value());
    bool found = false;
    for (const auto& e : result.error()) {
        if (e.message.find("initial") != std::string::npos &&
            e.message.find("not supported") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("Parse default attribute on InitialMsg fields (migrated from initial)", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("default_initial.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    for (const auto& msg : bmdl.messages) {
        if (msg.name == "InitialMsg") {
            for (const auto& child : msg.children) {
                if (auto* f = std::get_if<bgen::model::Field>(&child)) {
                    if (f->name == "counter") {
                        REQUIRE(f->default_value.has_value());
                        CHECK(*f->default_value == "100");
                    }
                    if (f->name == "status") {
                        REQUIRE(f->default_value.has_value());
                        CHECK(*f->default_value == "0");
                    }
                    if (f->name == "payload") {
                        CHECK_FALSE(f->default_value.has_value());
                    }
                }
            }
        }
    }
}

// ============================================================================
// Inline field parsing
// ============================================================================

TEST_CASE("Parse inline field attribute", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("invalid_inline_dup.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    for (const auto& msg : bmdl.messages) {
        if (msg.name == "Msg") {
            for (const auto& child : msg.children) {
                if (auto* f = std::get_if<bgen::model::Field>(&child)) {
                    if (f->name == "hdr") {
                        CHECK(f->is_inline);
                        CHECK(f->type_ref == "Header");
                    }
                }
            }
        }
    }
}

// ============================================================================
// Protocol defaults parsing
// ============================================================================

TEST_CASE("Reserved with zero bits rejected", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("reserved_zero.bmdl.xml"));
    REQUIRE_FALSE(result.has_value());
    bool found_error = false;
    for (const auto& e : result.error()) {
        if (e.message.find("non-zero") != std::string::npos) {
            found_error = true;
        }
    }
    CHECK(found_error);
}

TEST_CASE("Bytes attribute overflow rejected", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("bytes_overflow.bmdl.xml"));
    REQUIRE_FALSE(result.has_value());
    bool found_error = false;
    for (const auto& e : result.error()) {
        if (e.message.find("too large") != std::string::npos) {
            found_error = true;
        }
    }
    CHECK(found_error);
}

TEST_CASE("Parse protocol defaults", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("string_features.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    CHECK(bmdl.defaults.endian == bgen::model::Endian::Big);
    CHECK(bmdl.defaults.string_encoding == bgen::model::StringEncoding::Ascii);
    CHECK(bmdl.defaults.string_padding == bgen::model::StringPadding::Null);
    CHECK(bmdl.defaults.string_trim == bgen::model::StringTrim::Right);
}

// ============================================================================
// P2: Missing enum id auto-increments from previous value
// ============================================================================

TEST_CASE("Enum value missing id attribute auto-increments (P2)", "[parser]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("missing_enum_id.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    // Find the "status" type
    for (const auto& t : bmdl.types) {
        if (t.name == "status") {
            REQUIRE(t.enum_values.size() == 2);
            // "ok" has id=0 explicitly
            CHECK(t.enum_values[0].name == "ok");
            CHECK(t.enum_values[0].id == 0);
            // "missing-id-val" should auto-increment to 1
            CHECK(t.enum_values[1].name == "missing-id-val");
            CHECK(t.enum_values[1].id == 1);
            break;
        }
    }
}

// ============================================================================
// PA1: Dotted uppercase path parsed as FieldRef, not ConstantRef
// ============================================================================

TEST_CASE("Dotted uppercase path parsed as FieldRef (PA1)", "[parser]") {
    // "Header.type" starts with uppercase and could be misclassified as ConstantRef
    // PA1 fix ensures dotted paths are always FieldRef
    auto result = bgen::parser::parse_expression("Header.type");
    REQUIRE(result.has_value());
    CHECK((*result)->op == bgen::model::ExprOp::FieldRef);
    CHECK((*result)->name == "Header.type");
}

TEST_CASE("Uppercase non-dotted name is ConstantRef", "[parser]") {
    auto result = bgen::parser::parse_expression("SOME_CONSTANT");
    REQUIRE(result.has_value());
    CHECK((*result)->op == bgen::model::ExprOp::ConstantRef);
    CHECK((*result)->name == "SOME_CONSTANT");
}

TEST_CASE("Lowercase dotted path is FieldRef", "[parser]") {
    auto result = bgen::parser::parse_expression("header.length");
    REQUIRE(result.has_value());
    CHECK((*result)->op == bgen::model::ExprOp::FieldRef);
    CHECK((*result)->name == "header.length");
}

TEST_CASE("Dotted uppercase path in comparison is FieldRef (PA1)", "[parser]") {
    auto result = bgen::parser::parse_expression("Header.type != 0");
    REQUIRE(result.has_value());
    CHECK((*result)->op == bgen::model::ExprOp::Neq);
    REQUIRE((*result)->left != nullptr);
    CHECK((*result)->left->op == bgen::model::ExprOp::FieldRef);
    CHECK((*result)->left->name == "Header.type");
}

// ============================================================================
// Frame parsing (v2)
// ============================================================================

TEST_CASE("Parse v2 flat file with frame", "[parser][frame]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("frame_basic.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    CHECK(bmdl.has_protocol);
    CHECK(bmdl.protocol_name == "frame_basic");

    // Frame
    REQUIRE(bmdl.frames.size() == 1);
    const auto& frame = bmdl.frames[0];
    CHECK(frame.name == "SimpleFrame");
    CHECK(frame.header_fields.size() == 2); // msg-type + length
    CHECK(frame.footer_fields.empty());

    // Header field: msg-type with auto="id"
    const auto& f0 = std::get<bgen::model::Field>(frame.header_fields[0]);
    CHECK(f0.name == "msg-type");
    CHECK(f0.type_ref == "uint8");
    REQUIRE(f0.auto_expr.has_value());
    CHECK(f0.auto_expr->kind == bgen::model::AutoKind::Id);

    // Header field: length with auto="length"
    const auto& f1 = std::get<bgen::model::Field>(frame.header_fields[1]);
    CHECK(f1.name == "length");
    REQUIRE(f1.auto_expr.has_value());
    CHECK(f1.auto_expr->kind == bgen::model::AutoKind::Length);

    // Messages
    REQUIRE(bmdl.messages.size() == 2);
    CHECK(bmdl.messages[0].name == "Heartbeat");
    CHECK(bmdl.messages[0].id == "1");
    CHECK(bmdl.messages[1].name == "Status");
    CHECK(bmdl.messages[1].id == "2");

    // Defaults
    CHECK(bmdl.defaults.namespace_.has_value());
    CHECK(*bmdl.defaults.namespace_ == "frame_basic");
}

TEST_CASE("Parse v2 frame with config fields", "[parser][frame]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("frame_config.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    REQUIRE(bmdl.frames.size() == 1);
    const auto& frame = bmdl.frames[0];
    CHECK(frame.name == "ConfigFrame");
    CHECK(frame.header_fields.size() == 3); // system-id + msg-type + length

    const auto& f0 = std::get<bgen::model::Field>(frame.header_fields[0]);
    CHECK(f0.name == "system-id");
    REQUIRE(f0.auto_expr.has_value());
    CHECK(f0.auto_expr->kind == bgen::model::AutoKind::Config);
    CHECK(f0.auto_expr->key == "system-id");
}

TEST_CASE("Parse message id and direction attributes", "[parser][frame]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("frame_basic.bmdl.xml"));
    REQUIRE(result.has_value());

    const auto& bmdl = *result;
    REQUIRE(bmdl.messages.size() == 2);

    // Default direction is Both
    CHECK(bmdl.messages[0].direction == bgen::model::Direction::Both);
    CHECK(bmdl.messages[1].direction == bgen::model::Direction::Both);
}

TEST_CASE("Parse namespace in defaults", "[parser][frame]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("frame_basic.bmdl.xml"));
    REQUIRE(result.has_value());
    CHECK(result->defaults.namespace_.has_value());
    CHECK(*result->defaults.namespace_ == "frame_basic");
}

// ============================================================================
// Dispatch attribute removed
// ============================================================================

TEST_CASE("dispatch attribute on array is rejected as unknown", "[parser][dispatch]") {
    // After removing dispatch support, the parser should report it as an unknown attribute
    auto result = bgen::parser::parse_bmdl_file(fixture_path("invalid_dispatch_removed.bmdl.xml"));
    REQUIRE_FALSE(result.has_value());
    // The parse errors should mention "dispatch" as unrecognized
    bool found_dispatch_error = false;
    for (const auto& e : result.error()) {
        if (e.message.find("dispatch") != std::string::npos) {
            found_dispatch_error = true;
            break;
        }
    }
    CHECK(found_dispatch_error);
}

// ============================================================================
// Payload length-from expression on <payload> element
// ============================================================================

TEST_CASE("Parse payload length-from expression", "[parser][frame][payload_length_from]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("frame_payload_length_from.bmdl.xml"));
    REQUIRE(result.has_value());

    REQUIRE(result->frames.size() == 1);
    const auto& frame = result->frames[0];
    CHECK(frame.name == "ExprFrame");

    // payload should have length_from set
    REQUIRE(frame.payload.length_from != nullptr);
    CHECK(frame.payload.length_from->op == bgen::model::ExprOp::FieldRef);
    CHECK(frame.payload.length_from->name == "body-size");
}

TEST_CASE("Parse payload without length-from has null expression", "[parser][frame][payload_length_from]") {
    auto result = bgen::parser::parse_bmdl_file(fixture_path("frame_basic.bmdl.xml"));
    REQUIRE(result.has_value());

    REQUIRE(result->frames.size() == 1);
    const auto& frame = result->frames[0];

    // payload should NOT have length_from set
    CHECK(frame.payload.length_from == nullptr);
}
