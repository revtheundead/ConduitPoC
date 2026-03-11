// SPDX-License-Identifier: MIT
// Bgen - Expression Parser Implementation

#include "expression_parser.hpp"
#include <cctype>
#include <charconv>

namespace bgen::parser {

namespace {

// ============================================================================
// Token types
// ============================================================================

enum class TokenKind {
    // Literals
    Number, True, False, Remaining,
    // Identifiers (field refs use lower/mixed, constants are UPPER_SNAKE)
    Identifier,
    // Operators
    Plus, Minus, Star, Slash, Percent,
    Amp, Pipe, Caret, Tilde,
    ShiftLeft, ShiftRight,
    Eq, Neq, Lt, Lte, Gt, Gte,
    Bang,
    // Keywords
    And, Or, Not,
    // Delimiters
    LParen, RParen, Dot,
    // End
    Eof,
};

struct Token {
    TokenKind kind = TokenKind::Eof;
    std::string_view text;
    int64_t number = 0;
    int pos = 0;
    bool has_error = false;
};

// ============================================================================
// Lexer
// ============================================================================

class Lexer {
public:
    explicit Lexer(std::string_view input) : input_(input), pos_(0) {}

    Token next() {
        skip_whitespace();
        if (pos_ >= static_cast<int>(input_.size())) {
            return Token{TokenKind::Eof, {}, 0, pos_};
        }

        int start = pos_;
        char c = input_[static_cast<size_t>(pos_)];

        // Number literals
        if (std::isdigit(static_cast<unsigned char>(c))) {
            return lex_number(start);
        }

        // Identifiers and keywords
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            return lex_identifier(start);
        }

        // Two-character operators
        if (pos_ + 1 < static_cast<int>(input_.size())) {
            char c2 = input_[static_cast<size_t>(pos_) + 1];
            if (c == '<' && c2 == '<') { pos_ += 2; return Token{TokenKind::ShiftLeft, input_.substr(static_cast<size_t>(start), 2), 0, start}; }
            if (c == '>' && c2 == '>') { pos_ += 2; return Token{TokenKind::ShiftRight, input_.substr(static_cast<size_t>(start), 2), 0, start}; }
            if (c == '=' && c2 == '=') { pos_ += 2; return Token{TokenKind::Eq, input_.substr(static_cast<size_t>(start), 2), 0, start}; }
            if (c == '!' && c2 == '=') { pos_ += 2; return Token{TokenKind::Neq, input_.substr(static_cast<size_t>(start), 2), 0, start}; }
            if (c == '<' && c2 == '=') { pos_ += 2; return Token{TokenKind::Lte, input_.substr(static_cast<size_t>(start), 2), 0, start}; }
            if (c == '>' && c2 == '=') { pos_ += 2; return Token{TokenKind::Gte, input_.substr(static_cast<size_t>(start), 2), 0, start}; }
        }

        // Single-character operators
        pos_++;
        auto sv = input_.substr(static_cast<size_t>(start), 1);
        switch (c) {
            case '+': return Token{TokenKind::Plus, sv, 0, start};
            case '-': return Token{TokenKind::Minus, sv, 0, start};
            case '*': return Token{TokenKind::Star, sv, 0, start};
            case '/': return Token{TokenKind::Slash, sv, 0, start};
            case '%': return Token{TokenKind::Percent, sv, 0, start};
            case '&': return Token{TokenKind::Amp, sv, 0, start};
            case '|': return Token{TokenKind::Pipe, sv, 0, start};
            case '^': return Token{TokenKind::Caret, sv, 0, start};
            case '~': return Token{TokenKind::Tilde, sv, 0, start};
            case '<': return Token{TokenKind::Lt, sv, 0, start};
            case '>': return Token{TokenKind::Gt, sv, 0, start};
            case '!': return Token{TokenKind::Bang, sv, 0, start};
            case '(': return Token{TokenKind::LParen, sv, 0, start};
            case ')': return Token{TokenKind::RParen, sv, 0, start};
            case '.': return Token{TokenKind::Dot, sv, 0, start};
            default: break;
        }

        // Unrecognized character — return Eof with error flag so the parser
        // reports an error instead of silently truncating the expression.
        return Token{TokenKind::Eof, sv, 0, start, /*has_error=*/true};
    }

    Token peek() {
        int saved = pos_;
        auto tok = next();
        pos_ = saved;
        return tok;
    }

    int position() const { return pos_; }

private:
    void skip_whitespace() {
        while (pos_ < static_cast<int>(input_.size()) &&
               std::isspace(static_cast<unsigned char>(input_[static_cast<size_t>(pos_)]))) {
            pos_++;
        }
    }

    Token lex_number(int start) {
        // Check for hex
        if (pos_ + 1 < static_cast<int>(input_.size()) &&
            input_[static_cast<size_t>(pos_)] == '0' &&
            (input_[static_cast<size_t>(pos_) + 1] == 'x' || input_[static_cast<size_t>(pos_) + 1] == 'X')) {
            pos_ += 2;
            while (pos_ < static_cast<int>(input_.size()) &&
                   std::isxdigit(static_cast<unsigned char>(input_[static_cast<size_t>(pos_)]))) {
                pos_++;
            }
        } else {
            while (pos_ < static_cast<int>(input_.size()) &&
                   std::isdigit(static_cast<unsigned char>(input_[static_cast<size_t>(pos_)]))) {
                pos_++;
            }
        }

        auto text = input_.substr(static_cast<size_t>(start), static_cast<size_t>(pos_ - start));
        int64_t val = 0;
        std::errc ec{};
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            auto hex = text.substr(2);
            auto [ptr, err] = std::from_chars(hex.data(), hex.data() + hex.size(), val, 16);
            ec = err;
        } else {
            auto [ptr, err] = std::from_chars(text.data(), text.data() + text.size(), val, 10);
            ec = err;
        }
        if (ec != std::errc{}) {
            return Token{TokenKind::Number, text, 0, start, true};
        }
        return Token{TokenKind::Number, text, val, start};
    }

    Token lex_identifier(int start) {
        // Identifier: [a-zA-Z_][a-zA-Z0-9_-]*
        // Maximal munch for hyphens: a-b is a single identifier if no whitespace around '-'
        while (pos_ < static_cast<int>(input_.size())) {
            char ch = input_[static_cast<size_t>(pos_)];
            if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') {
                pos_++;
            } else if (ch == '-') {
                // Hyphen is part of identifier only if next char is alnum/underscore
                // (maximal munch: total-length is one token, but "x - 3" is subtraction)
                if (pos_ + 1 < static_cast<int>(input_.size())) {
                    char next_ch = input_[static_cast<size_t>(pos_) + 1];
                    if (std::isalnum(static_cast<unsigned char>(next_ch)) || next_ch == '_') {
                        pos_++;
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            } else {
                break;
            }
        }

        auto text = input_.substr(static_cast<size_t>(start), static_cast<size_t>(pos_ - start));

        // Keywords
        if (text == "true") return Token{TokenKind::True, text, 0, start};
        if (text == "false") return Token{TokenKind::False, text, 0, start};
        if (text == "remaining") return Token{TokenKind::Remaining, text, 0, start};
        if (text == "and") return Token{TokenKind::And, text, 0, start};
        if (text == "or") return Token{TokenKind::Or, text, 0, start};
        if (text == "not") return Token{TokenKind::Not, text, 0, start};

        return Token{TokenKind::Identifier, text, 0, start};
    }

    std::string_view input_;
    int pos_;
};

// ============================================================================
// Recursive descent parser
// ============================================================================

class ExprParser {
public:
    ExprParser(std::string_view input, const model::SourceLoc& base_loc)
        : lexer_(input), base_loc_(base_loc) {
        advance();
    }

    ExprResult parse() {
        auto result = parse_or();
        if (!result) return result;
        if (current_.kind != TokenKind::Eof) {
            return error("unexpected token after expression: " + std::string(current_.text));
        }
        if (current_.has_error) {
            return error("unexpected character in expression: " + std::string(current_.text));
        }
        return result;
    }

private:
    static constexpr int MAX_EXPR_DEPTH = 128;
    int depth_ = 0;

    struct DepthGuard {
        int& depth;
        bool overflow = false;
        DepthGuard(int& d, int max) : depth(d) { overflow = (++depth > max); }
        ~DepthGuard() { --depth; }
    };

    void advance() {
        current_ = lexer_.next();
    }

    model::SourceLoc loc_at(int pos) const {
        model::SourceLoc loc = base_loc_;
        loc.offset += pos;
        return loc;
    }

    std::unexpected<ParseError> error(std::string msg) {
        return std::unexpected(ParseError{std::move(msg), loc_at(current_.pos)});
    }

    std::unique_ptr<model::Expr> make_number(int64_t val, int pos) {
        auto e = std::make_unique<model::Expr>();
        e->op = model::ExprOp::NumberLit;
        e->number_value = val;
        e->loc = loc_at(pos);
        return e;
    }

    std::unique_ptr<model::Expr> make_bool(bool val, int pos) {
        auto e = std::make_unique<model::Expr>();
        e->op = model::ExprOp::BoolLit;
        e->bool_value = val;
        e->loc = loc_at(pos);
        return e;
    }

    std::unique_ptr<model::Expr> make_ref(const std::string& name, int pos) {
        auto e = std::make_unique<model::Expr>();
        // Dotted paths (field.subfield) are always field references
        bool has_dot = name.find('.') != std::string::npos;
        // Determine if constant or field ref by naming convention
        // UPPER_SNAKE_CASE = constant (but never for dotted paths)
        bool is_const = !has_dot;
        if (is_const) {
            for (char c : name) {
                if (c != '_' && !std::isupper(static_cast<unsigned char>(c)) &&
                    !std::isdigit(static_cast<unsigned char>(c))) {
                    is_const = false;
                    break;
                }
            }
        }
        // Must start with uppercase letter for constant
        if (name.empty() || !std::isupper(static_cast<unsigned char>(name[0]))) {
            is_const = false;
        }

        e->op = is_const ? model::ExprOp::ConstantRef : model::ExprOp::FieldRef;
        e->name = name;
        e->loc = loc_at(pos);
        return e;
    }

    std::unique_ptr<model::Expr> make_binary(model::ExprOp op,
            std::unique_ptr<model::Expr> left,
            std::unique_ptr<model::Expr> right, int pos) {
        auto e = std::make_unique<model::Expr>();
        e->op = op;
        e->left = std::move(left);
        e->right = std::move(right);
        e->loc = loc_at(pos);
        return e;
    }

    std::unique_ptr<model::Expr> make_unary(model::ExprOp op,
            std::unique_ptr<model::Expr> operand, int pos) {
        auto e = std::make_unique<model::Expr>();
        e->op = op;
        e->left = std::move(operand);
        e->loc = loc_at(pos);
        return e;
    }

    // or-expr = and-expr ("or" and-expr)*
    ExprResult parse_or() {
        DepthGuard dg(depth_, MAX_EXPR_DEPTH);
        if (dg.overflow) return error("expression nesting too deep (>" + std::to_string(MAX_EXPR_DEPTH) + " levels)");

        auto left = parse_and();
        if (!left) return left;

        while (current_.kind == TokenKind::Or) {
            int pos = current_.pos;
            advance();
            auto right = parse_and();
            if (!right) return right;
            *left = make_binary(model::ExprOp::LogOr, std::move(*left), std::move(*right), pos);
        }
        return left;
    }

    // and-expr = cmp-expr ("and" cmp-expr)*
    ExprResult parse_and() {
        auto left = parse_cmp();
        if (!left) return left;

        while (current_.kind == TokenKind::And) {
            int pos = current_.pos;
            advance();
            auto right = parse_cmp();
            if (!right) return right;
            *left = make_binary(model::ExprOp::LogAnd, std::move(*left), std::move(*right), pos);
        }
        return left;
    }

    // cmp-expr = bitor-expr (cmp-op bitor-expr)?
    ExprResult parse_cmp() {
        auto left = parse_bitor();
        if (!left) return left;

        model::ExprOp op{};
        bool found = true;
        switch (current_.kind) {
            case TokenKind::Eq: op = model::ExprOp::Eq; break;
            case TokenKind::Neq: op = model::ExprOp::Neq; break;
            case TokenKind::Lt: op = model::ExprOp::Lt; break;
            case TokenKind::Lte: op = model::ExprOp::Lte; break;
            case TokenKind::Gt: op = model::ExprOp::Gt; break;
            case TokenKind::Gte: op = model::ExprOp::Gte; break;
            default: found = false; break;
        }

        if (found) {
            int pos = current_.pos;
            advance();
            auto right = parse_bitor();
            if (!right) return right;
            *left = make_binary(op, std::move(*left), std::move(*right), pos);
        }
        return left;
    }

    // bitor-expr = xor-expr ("|" xor-expr)*
    ExprResult parse_bitor() {
        auto left = parse_xor();
        if (!left) return left;

        while (current_.kind == TokenKind::Pipe) {
            int pos = current_.pos;
            advance();
            auto right = parse_xor();
            if (!right) return right;
            *left = make_binary(model::ExprOp::BitOr, std::move(*left), std::move(*right), pos);
        }
        return left;
    }

    // xor-expr = bitand-expr ("^" bitand-expr)*
    ExprResult parse_xor() {
        auto left = parse_bitand();
        if (!left) return left;

        while (current_.kind == TokenKind::Caret) {
            int pos = current_.pos;
            advance();
            auto right = parse_bitand();
            if (!right) return right;
            *left = make_binary(model::ExprOp::BitXor, std::move(*left), std::move(*right), pos);
        }
        return left;
    }

    // bitand-expr = shift-expr ("&" shift-expr)*
    ExprResult parse_bitand() {
        auto left = parse_shift();
        if (!left) return left;

        while (current_.kind == TokenKind::Amp) {
            int pos = current_.pos;
            advance();
            auto right = parse_shift();
            if (!right) return right;
            *left = make_binary(model::ExprOp::BitAnd, std::move(*left), std::move(*right), pos);
        }
        return left;
    }

    // shift-expr = add-expr (("<<" | ">>") add-expr)*
    ExprResult parse_shift() {
        auto left = parse_add();
        if (!left) return left;

        while (current_.kind == TokenKind::ShiftLeft || current_.kind == TokenKind::ShiftRight) {
            auto op = current_.kind == TokenKind::ShiftLeft
                ? model::ExprOp::ShiftLeft : model::ExprOp::ShiftRight;
            int pos = current_.pos;
            advance();
            auto right = parse_add();
            if (!right) return right;
            *left = make_binary(op, std::move(*left), std::move(*right), pos);
        }
        return left;
    }

    // add-expr = mul-expr (("+" | "-") mul-expr)*
    ExprResult parse_add() {
        auto left = parse_mul();
        if (!left) return left;

        while (current_.kind == TokenKind::Plus || current_.kind == TokenKind::Minus) {
            auto op = current_.kind == TokenKind::Plus
                ? model::ExprOp::Add : model::ExprOp::Sub;
            int pos = current_.pos;
            advance();
            auto right = parse_mul();
            if (!right) return right;
            *left = make_binary(op, std::move(*left), std::move(*right), pos);
        }
        return left;
    }

    // mul-expr = unary (("*" | "/" | "%") unary)*
    ExprResult parse_mul() {
        auto left = parse_unary();
        if (!left) return left;

        while (current_.kind == TokenKind::Star || current_.kind == TokenKind::Slash ||
               current_.kind == TokenKind::Percent) {
            model::ExprOp op;
            if (current_.kind == TokenKind::Star) op = model::ExprOp::Mul;
            else if (current_.kind == TokenKind::Slash) op = model::ExprOp::Div;
            else op = model::ExprOp::Mod;
            int pos = current_.pos;
            advance();
            auto right = parse_unary();
            if (!right) return right;
            *left = make_binary(op, std::move(*left), std::move(*right), pos);
        }
        return left;
    }

    // unary = ("not" | "!" | "-" | "~") unary | primary
    ExprResult parse_unary() {
        if (current_.kind == TokenKind::Not || current_.kind == TokenKind::Bang) {
            int pos = current_.pos;
            advance();
            auto operand = parse_unary();
            if (!operand) return operand;
            return ExprResult(make_unary(model::ExprOp::LogNot, std::move(*operand), pos));
        }
        if (current_.kind == TokenKind::Minus) {
            int pos = current_.pos;
            advance();
            auto operand = parse_unary();
            if (!operand) return operand;
            return ExprResult(make_unary(model::ExprOp::Negate, std::move(*operand), pos));
        }
        if (current_.kind == TokenKind::Tilde) {
            int pos = current_.pos;
            advance();
            auto operand = parse_unary();
            if (!operand) return operand;
            return ExprResult(make_unary(model::ExprOp::BitNot, std::move(*operand), pos));
        }
        return parse_primary();
    }

    // primary = number | "true" | "false" | "remaining" | identifier(.identifier)* | "(" expr ")"
    ExprResult parse_primary() {
        if (current_.kind == TokenKind::Number) {
            if (current_.has_error) {
                return error("numeric literal too large: " + std::string(current_.text));
            }
            auto e = make_number(current_.number, current_.pos);
            advance();
            return ExprResult(std::move(e));
        }

        if (current_.kind == TokenKind::True) {
            auto e = make_bool(true, current_.pos);
            advance();
            return ExprResult(std::move(e));
        }

        if (current_.kind == TokenKind::False) {
            auto e = make_bool(false, current_.pos);
            advance();
            return ExprResult(std::move(e));
        }

        if (current_.kind == TokenKind::Remaining) {
            auto e = std::make_unique<model::Expr>();
            e->op = model::ExprOp::Remaining;
            e->loc = loc_at(current_.pos);
            advance();
            return ExprResult(std::move(e));
        }

        if (current_.kind == TokenKind::Identifier) {
            std::string name(current_.text);
            int pos = current_.pos;
            advance();

            // Handle dotted paths: field.subfield.subsubfield
            while (current_.kind == TokenKind::Dot) {
                advance();
                if (current_.kind != TokenKind::Identifier) {
                    return error("expected identifier after '.'");
                }
                name += ".";
                name += current_.text;
                advance();
            }

            return ExprResult(make_ref(name, pos));
        }

        if (current_.kind == TokenKind::LParen) {
            advance();
            auto e = parse_or();
            if (!e) return e;
            if (current_.kind != TokenKind::RParen) {
                return error("expected ')'");
            }
            advance();
            return e;
        }

        if (current_.kind == TokenKind::Eof && current_.has_error) {
            return error("unexpected character: " + std::string(current_.text));
        }
        return error("expected expression, got: " +
            (current_.kind == TokenKind::Eof ? std::string("end of input") : std::string(current_.text)));
    }

    Lexer lexer_;
    model::SourceLoc base_loc_;
    Token current_;
};

} // anonymous namespace

ExprResult parse_expression(std::string_view input, const model::SourceLoc& base_loc) {
    ExprParser parser(input, base_loc);
    return parser.parse();
}

} // namespace bgen::parser
