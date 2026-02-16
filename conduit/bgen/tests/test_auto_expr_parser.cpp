// SPDX-License-Identifier: MIT
// Tests for the auto expression parser

#include <catch2/catch_test_macros.hpp>
#include "../src/parser/auto_expr_parser.hpp"

using namespace bgen::parser;
using namespace bgen::model;

TEST_CASE("Auto expr: id", "[auto_expr]") {
    auto r = parse_auto_expr("id");
    REQUIRE(r.has_value());
    CHECK(r->kind == AutoKind::Id);
    CHECK(r->field_ref.empty());
    CHECK(r->key.empty());
    CHECK(!r->modifier.has_modifier());
}

TEST_CASE("Auto expr: id with whitespace", "[auto_expr]") {
    auto r = parse_auto_expr("  id  ");
    REQUIRE(r.has_value());
    CHECK(r->kind == AutoKind::Id);
}

TEST_CASE("Auto expr: increment", "[auto_expr]") {
    auto r = parse_auto_expr("increment");
    REQUIRE(r.has_value());
    CHECK(r->kind == AutoKind::Increment);
}

TEST_CASE("Auto expr: timestamp", "[auto_expr]") {
    auto r = parse_auto_expr("timestamp");
    REQUIRE(r.has_value());
    CHECK(r->kind == AutoKind::Timestamp);
}

TEST_CASE("Auto expr: length (bare)", "[auto_expr]") {
    auto r = parse_auto_expr("length");
    REQUIRE(r.has_value());
    CHECK(r->kind == AutoKind::Length);
    CHECK(r->field_ref.empty());
    CHECK(!r->modifier.has_modifier());
}

TEST_CASE("Auto expr: length(payload)", "[auto_expr]") {
    auto r = parse_auto_expr("length(payload)");
    REQUIRE(r.has_value());
    CHECK(r->kind == AutoKind::Length);
    CHECK(r->field_ref == "payload");
    CHECK(!r->modifier.has_modifier());
}

TEST_CASE("Auto expr: length - 3", "[auto_expr]") {
    auto r = parse_auto_expr("length - 3");
    REQUIRE(r.has_value());
    CHECK(r->kind == AutoKind::Length);
    CHECK(r->field_ref.empty());
    CHECK(r->modifier.op == ArithOp::Sub);
    CHECK(r->modifier.literal == 3);
    CHECK(!r->modifier.is_field_operand());
}

TEST_CASE("Auto expr: length + 2", "[auto_expr]") {
    auto r = parse_auto_expr("length + 2");
    REQUIRE(r.has_value());
    CHECK(r->kind == AutoKind::Length);
    CHECK(r->field_ref.empty());
    CHECK(r->modifier.op == ArithOp::Add);
    CHECK(r->modifier.literal == 2);
}

TEST_CASE("Auto expr: length(payload) + 2", "[auto_expr]") {
    auto r = parse_auto_expr("length(payload) + 2");
    REQUIRE(r.has_value());
    CHECK(r->kind == AutoKind::Length);
    CHECK(r->field_ref == "payload");
    CHECK(r->modifier.op == ArithOp::Add);
    CHECK(r->modifier.literal == 2);
}

TEST_CASE("Auto expr: length(payload) - 5", "[auto_expr]") {
    auto r = parse_auto_expr("length(payload) - 5");
    REQUIRE(r.has_value());
    CHECK(r->kind == AutoKind::Length);
    CHECK(r->field_ref == "payload");
    CHECK(r->modifier.op == ArithOp::Sub);
    CHECK(r->modifier.literal == 5);
}

// New arithmetic operators

TEST_CASE("Auto expr: length * 2", "[auto_expr]") {
    auto r = parse_auto_expr("length * 2");
    REQUIRE(r.has_value());
    CHECK(r->kind == AutoKind::Length);
    CHECK(r->modifier.op == ArithOp::Mul);
    CHECK(r->modifier.literal == 2);
}

TEST_CASE("Auto expr: length(payload) / 4", "[auto_expr]") {
    auto r = parse_auto_expr("length(payload) / 4");
    REQUIRE(r.has_value());
    CHECK(r->kind == AutoKind::Length);
    CHECK(r->field_ref == "payload");
    CHECK(r->modifier.op == ArithOp::Div);
    CHECK(r->modifier.literal == 4);
}

TEST_CASE("Auto expr: length(data) % 256", "[auto_expr]") {
    auto r = parse_auto_expr("length(data) % 256");
    REQUIRE(r.has_value());
    CHECK(r->kind == AutoKind::Length);
    CHECK(r->field_ref == "data");
    CHECK(r->modifier.op == ArithOp::Mod);
    CHECK(r->modifier.literal == 256);
}

// Field operands

TEST_CASE("Auto expr: length(data) - header-size", "[auto_expr]") {
    auto r = parse_auto_expr("length(data) - header-size");
    REQUIRE(r.has_value());
    CHECK(r->kind == AutoKind::Length);
    CHECK(r->field_ref == "data");
    CHECK(r->modifier.op == ArithOp::Sub);
    CHECK(r->modifier.is_field_operand());
    CHECK(r->modifier.field_ref == "header-size");
}

TEST_CASE("Auto expr: length + prefix_len", "[auto_expr]") {
    auto r = parse_auto_expr("length + prefix_len");
    REQUIRE(r.has_value());
    CHECK(r->kind == AutoKind::Length);
    CHECK(r->modifier.op == ArithOp::Add);
    CHECK(r->modifier.is_field_operand());
    CHECK(r->modifier.field_ref == "prefix_len");
}

TEST_CASE("Auto expr: count(items)", "[auto_expr]") {
    auto r = parse_auto_expr("count(items)");
    REQUIRE(r.has_value());
    CHECK(r->kind == AutoKind::Count);
    CHECK(r->field_ref == "items");
}

TEST_CASE("Auto expr: config(system-id)", "[auto_expr]") {
    auto r = parse_auto_expr("config(system-id)");
    REQUIRE(r.has_value());
    CHECK(r->kind == AutoKind::Config);
    CHECK(r->key == "system-id");
}

TEST_CASE("Auto expr: whitespace around parens", "[auto_expr]") {
    auto r = parse_auto_expr("length( payload )");
    REQUIRE(r.has_value());
    CHECK(r->kind == AutoKind::Length);
    CHECK(r->field_ref == "payload");
}

// Error cases

TEST_CASE("Auto expr: empty string", "[auto_expr]") {
    auto r = parse_auto_expr("");
    REQUIRE(!r.has_value());
    CHECK(r.error().message.find("empty") != std::string::npos);
}

TEST_CASE("Auto expr: unknown keyword", "[auto_expr]") {
    auto r = parse_auto_expr("foobar");
    REQUIRE(!r.has_value());
    CHECK(r.error().message.find("unknown") != std::string::npos);
}

TEST_CASE("Auto expr: config without parens", "[auto_expr]") {
    auto r = parse_auto_expr("config");
    REQUIRE(!r.has_value());
}

TEST_CASE("Auto expr: config with empty key", "[auto_expr]") {
    auto r = parse_auto_expr("config()");
    REQUIRE(!r.has_value());
}

TEST_CASE("Auto expr: count without parens", "[auto_expr]") {
    auto r = parse_auto_expr("count");
    REQUIRE(!r.has_value());
}

TEST_CASE("Auto expr: length with invalid modifier", "[auto_expr]") {
    auto r = parse_auto_expr("length ^ 2");
    REQUIRE(!r.has_value());
}

TEST_CASE("Auto expr: config with trailing text", "[auto_expr]") {
    auto r = parse_auto_expr("config(key) extra");
    REQUIRE(!r.has_value());
}

TEST_CASE("Auto expr: length * (missing operand)", "[auto_expr]") {
    auto r = parse_auto_expr("length *");
    REQUIRE(!r.has_value());
}
