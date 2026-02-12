// SPDX-License-Identifier: MIT
// Bgen tests - Expression parser edge case tests

#include <catch2/catch_test_macros.hpp>
#include "../src/parser/expression_parser.hpp"

using namespace bgen::parser;
using namespace bgen::model;

// ============================================================================
// Section: Modulo operator
// ============================================================================

TEST_CASE("Expression parser: modulo operator", "[expr][parser]") {
    auto result = parse_expression("10 % 3");
    REQUIRE(result.has_value());
    REQUIRE(result->get()->op == ExprOp::Mod);
    CHECK(result->get()->left->number_value == 10);
    CHECK(result->get()->right->number_value == 3);
}

TEST_CASE("Expression parser: modulo with field ref", "[expr][parser]") {
    auto result = parse_expression("count % 4");
    REQUIRE(result.has_value());
    REQUIRE(result->get()->op == ExprOp::Mod);
    CHECK(result->get()->left->op == ExprOp::FieldRef);
    CHECK(result->get()->right->number_value == 4);
}

// ============================================================================
// Section: Logical operators
// ============================================================================

TEST_CASE("Expression parser: logical OR", "[expr][parser]") {
    auto result = parse_expression("a or b");
    REQUIRE(result.has_value());
    REQUIRE(result->get()->op == ExprOp::LogOr);
    CHECK(result->get()->left->op == ExprOp::FieldRef);
    CHECK(result->get()->right->op == ExprOp::FieldRef);
}

TEST_CASE("Expression parser: logical NOT", "[expr][parser]") {
    auto result = parse_expression("not flag");
    REQUIRE(result.has_value());
    REQUIRE(result->get()->op == ExprOp::LogNot);
    CHECK(result->get()->left->op == ExprOp::FieldRef);
}

TEST_CASE("Expression parser: logical AND with comparison", "[expr][parser]") {
    auto result = parse_expression("x > 0 and y < 10");
    REQUIRE(result.has_value());
    REQUIRE(result->get()->op == ExprOp::LogAnd);
    CHECK(result->get()->left->op == ExprOp::Gt);
    CHECK(result->get()->right->op == ExprOp::Lt);
}

// ============================================================================
// Section: Parenthesized expressions
// ============================================================================

TEST_CASE("Expression parser: parenthesized addition", "[expr][parser]") {
    auto result = parse_expression("(a + b) * 2");
    REQUIRE(result.has_value());
    REQUIRE(result->get()->op == ExprOp::Mul);
    CHECK(result->get()->left->op == ExprOp::Add);
    CHECK(result->get()->right->number_value == 2);
}

TEST_CASE("Expression parser: nested parentheses", "[expr][parser]") {
    auto result = parse_expression("((a + b))");
    REQUIRE(result.has_value());
    REQUIRE(result->get()->op == ExprOp::Add);
}

// ============================================================================
// Section: Deeply nested expressions (DepthGuard)
// ============================================================================

TEST_CASE("Expression parser: deep nesting rejects overflow", "[expr][parser]") {
    // Build an expression with 200 nested parens: (((((...0...)))))
    std::string deep(200, '(');
    deep += "0";
    deep += std::string(200, ')');
    auto result = parse_expression(deep);
    // DepthGuard limit is 128, so this should fail
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("Expression parser: nesting within limit succeeds", "[expr][parser]") {
    // 50 levels — well within limit
    std::string ok(50, '(');
    ok += "42";
    ok += std::string(50, ')');
    auto result = parse_expression(ok);
    REQUIRE(result.has_value());
    CHECK(result->get()->op == ExprOp::NumberLit);
    CHECK(result->get()->number_value == 42);
}

// ============================================================================
// Section: Operator precedence
// ============================================================================

TEST_CASE("Expression parser: mul before add", "[expr][parser]") {
    auto result = parse_expression("1 + 2 * 3");
    REQUIRE(result.has_value());
    // Top should be Add(1, Mul(2,3))
    REQUIRE(result->get()->op == ExprOp::Add);
    CHECK(result->get()->left->number_value == 1);
    REQUIRE(result->get()->right->op == ExprOp::Mul);
    CHECK(result->get()->right->left->number_value == 2);
    CHECK(result->get()->right->right->number_value == 3);
}

// ============================================================================
// Section: Bitwise operators
// ============================================================================

TEST_CASE("Expression parser: bitwise AND", "[expr][parser]") {
    auto result = parse_expression("flags & 0xFF");
    REQUIRE(result.has_value());
    REQUIRE(result->get()->op == ExprOp::BitAnd);
}

TEST_CASE("Expression parser: bitwise OR", "[expr][parser]") {
    auto result = parse_expression("a | b");
    REQUIRE(result.has_value());
    REQUIRE(result->get()->op == ExprOp::BitOr);
}

TEST_CASE("Expression parser: shift operators", "[expr][parser]") {
    auto result = parse_expression("x << 2");
    REQUIRE(result.has_value());
    REQUIRE(result->get()->op == ExprOp::ShiftLeft);
}

// ============================================================================
// Section: Error cases
// ============================================================================

TEST_CASE("Expression parser: empty input", "[expr][parser]") {
    auto result = parse_expression("");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("Expression parser: unmatched paren", "[expr][parser]") {
    auto result = parse_expression("(a + b");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("Expression parser: trailing garbage", "[expr][parser]") {
    auto result = parse_expression("1 + 2 3");
    REQUIRE_FALSE(result.has_value());
}
