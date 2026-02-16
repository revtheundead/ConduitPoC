// SPDX-License-Identifier: MIT
// Bgen - AST Model for BMDL

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace bgen::model {

// ============================================================================
// Source location for error reporting
// ============================================================================

struct SourceLoc {
    std::string file;
    int offset = 0;
    int line = 0;
    int column = 0;

    std::string to_string() const {
        if (file.empty()) return "<unknown>";
        if (line > 0)
            return file + ":" + std::to_string(line) + ":" + std::to_string(column);
        return file + ":offset(" + std::to_string(offset) + ")";
    }
};

// ============================================================================
// Enumerations
// ============================================================================

enum class PrimitiveBase { Uint, Int, Float, Bool, Bytes, String };
enum class Endian { Big, Little };
enum class StringEncoding { Ascii, Utf8, Ia5, Ebcdic };
enum class StringPadding { Null, Space, None };
enum class StringTrim { Left, Right, Both, None };
enum class DisplayFormat { Decimal, Hex, Octal, Binary };
enum class ValidateTiming { Immediate, Deferred };
enum class Direction { Both, Send, Receive };
enum class Dispatch { Batch, PerRecord };
enum class WireEncoding { Default, CB2, BNR, BNR_S, BCD, BCD_S };
enum class AutoKind { Id, Length, Count, Increment, Config, Timestamp };
enum class ArithOp { None, Add, Sub, Mul, Div, Mod };

// ============================================================================
// Arithmetic modifier for auto="length" expressions
// ============================================================================

struct ArithModifier {
    ArithOp op = ArithOp::None;
    int64_t literal = 0;       // numeric operand (when field_ref is empty)
    std::string field_ref;     // field operand (mutually exclusive with literal)

    bool has_modifier() const { return op != ArithOp::None; }
    bool is_field_operand() const { return !field_ref.empty(); }
};

// ============================================================================
// Auto expression (parsed form of auto="..." attribute)
// ============================================================================

struct AutoExpr {
    AutoKind kind = AutoKind::Id;
    std::string field_ref;   // length(field), count(field)
    std::string key;         // config(key)
    ArithModifier modifier;  // length * 2, length(field) - header_len, etc.
};

// ============================================================================
// Expression AST
// ============================================================================

enum class ExprOp {
    // Literals
    NumberLit, BoolLit, FieldRef, ConstantRef, Remaining,
    // Binary
    Add, Sub, Mul, Div, Mod,
    BitAnd, BitOr, BitXor, ShiftLeft, ShiftRight,
    Eq, Neq, Lt, Lte, Gt, Gte,
    LogAnd, LogOr,
    // Unary
    Negate, BitNot, LogNot,
};

struct Expr {
    ExprOp op = ExprOp::NumberLit;  // Default to NumberLit (leaf node)
    int64_t number_value = 0;
    bool bool_value = false;
    std::string name;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    SourceLoc loc;

    // Deep copy
    std::unique_ptr<Expr> clone() const {
        auto e = std::make_unique<Expr>();
        e->op = op;
        e->number_value = number_value;
        e->bool_value = bool_value;
        e->name = name;
        if (left) e->left = left->clone();
        if (right) e->right = right->clone();
        e->loc = loc;
        return e;
    }
};

// ============================================================================
// Constraint
// ============================================================================

struct Constraint {
    std::optional<std::string> equals;
    std::optional<std::string> min;
    std::optional<std::string> max;
    ValidateTiming validate = ValidateTiming::Immediate;
    SourceLoc loc;
};

// ============================================================================
// Enum / Flags
// ============================================================================

struct EnumValue {
    std::string name;
    int64_t id = 0;
    SourceLoc loc;
};

struct FlagDef {
    std::string name;
    int bit = 0;
    SourceLoc loc;
};

// ============================================================================
// Annotation
// ============================================================================

struct Annotation {
    std::string name;
    std::string value;
};

// ============================================================================
// Type Definition
// ============================================================================

struct TypeDef {
    std::string name;
    PrimitiveBase base = PrimitiveBase::Uint;
    int bits = 0;
    Endian endian = Endian::Big;
    bool endian_explicit = false;
    DisplayFormat format = DisplayFormat::Decimal;
    bool format_explicit = false;
    WireEncoding wire_encoding = WireEncoding::Default;
    bool wire_encoding_explicit = false;

    std::optional<double> scale;
    std::optional<double> offset;
    std::optional<std::string> unit;

    std::vector<EnumValue> enum_values;
    std::vector<FlagDef> flags;
    std::optional<Constraint> constraint;

    // String attributes
    std::optional<int> length;
    StringEncoding encoding = StringEncoding::Ascii;
    bool encoding_explicit = false;
    StringPadding padding = StringPadding::Null;
    bool padding_explicit = false;
    StringTrim trim = StringTrim::Right;
    bool trim_explicit = false;
    std::optional<std::string> terminated;
    std::optional<int> max_length;
    std::optional<int> char_bits;

    std::vector<Annotation> annotations;
    std::string doc;
    SourceLoc loc;
};

// ============================================================================
// Forward declarations for recursive types
// ============================================================================

struct Field;
struct StructDef;
struct ArrayDef;
struct ChoiceDef;
struct FxBlock;
struct Reserved;
struct Align;

using StructChild = std::variant<
    Field,
    StructDef,
    ArrayDef,
    ChoiceDef,
    FxBlock,
    Reserved,
    Align
>;

// ============================================================================
// Field
// ============================================================================

struct Field {
    std::string name;
    std::string type_ref;
    bool type_is_inline = false;

    std::optional<int> bits;
    std::optional<int> bytes_attr;
    bool is_signed = false;
    std::optional<PrimitiveBase> base;  // explicit base type for inline fields

    std::optional<double> scale;
    std::optional<double> offset;
    std::optional<std::string> unit;

    std::vector<EnumValue> enum_values;
    std::vector<FlagDef> flags;
    std::optional<Constraint> constraint;

    std::optional<int> bit;
    std::unique_ptr<Expr> present_when;

    std::optional<int> length;
    bool length_star = false;
    std::unique_ptr<Expr> length_from;
    std::optional<std::string> length_prefix;
    bool length_includes_prefix = false;

    std::optional<StringEncoding> encoding;
    std::optional<StringPadding> padding;
    std::optional<StringTrim> trim;
    std::optional<std::string> terminated;
    std::optional<int> max_length;
    std::optional<int> char_bits;

    Endian endian = Endian::Big;
    bool endian_explicit = false;
    DisplayFormat format = DisplayFormat::Decimal;
    bool format_explicit = false;
    std::optional<WireEncoding> wire_encoding;
    bool is_inline = false;
    std::optional<std::string> default_value;
    std::optional<std::string> initial_value;
    std::optional<std::string> auto_attr;
    std::optional<AutoExpr> auto_expr;

    std::vector<Annotation> annotations;
    std::string doc;
    SourceLoc loc;
};

// ============================================================================
// Struct Definition
// ============================================================================

struct StructDef {
    std::string name;
    std::vector<StructChild> children;

    bool is_bitmap = false;
    int bitmap_bits = 8;
    std::optional<int> bitmap_ext;

    std::optional<int> bit;
    std::unique_ptr<Expr> present_when;

    std::vector<Annotation> annotations;
    std::string doc;
    SourceLoc loc;
};

// ============================================================================
// Array Definition
// ============================================================================

struct ArrayDef {
    std::string name;
    std::string type_ref;
    std::vector<StructChild> children;

    std::optional<int> fixed_count;
    std::unique_ptr<Expr> count_from;
    bool count_star = false;

    std::optional<int> length;
    std::unique_ptr<Expr> length_from;

    std::optional<int> bit;
    std::unique_ptr<Expr> present_when;

    std::optional<Dispatch> dispatch;  // nullopt = use default (batch for session arrays)

    std::vector<Annotation> annotations;
    std::string doc;
    SourceLoc loc;
};

// ============================================================================
// Choice / Case / Otherwise
// ============================================================================

struct CaseDef {
    std::string name;
    std::string type_ref;
    std::vector<StructChild> children;

    std::optional<std::string> value;
    std::optional<std::string> range;

    Direction direction = Direction::Both;

    std::vector<Annotation> annotations;
    std::string doc;
    SourceLoc loc;
};

struct OtherwiseDef {
    std::string name;
    std::string type_ref;
    std::vector<StructChild> children;

    std::vector<Annotation> annotations;
    std::string doc;
    SourceLoc loc;
};

struct ChoiceDef {
    std::string name;
    std::unique_ptr<Expr> switch_expr;
    std::vector<CaseDef> cases;
    std::optional<OtherwiseDef> otherwise;

    std::optional<int> length;
    std::unique_ptr<Expr> length_from;

    std::optional<int> bit;
    std::unique_ptr<Expr> present_when;

    std::vector<Annotation> annotations;
    std::string doc;
    SourceLoc loc;
};

// ============================================================================
// FX Extension Block
// ============================================================================

struct FxBlock {
    std::vector<StructChild> children;
    SourceLoc loc;
};

// ============================================================================
// Reserved / Align
// ============================================================================

struct Reserved {
    int bits = 0;
    SourceLoc loc;
};

struct Align {
    int to = 0;
    SourceLoc loc;
};

// ============================================================================
// Constant
// ============================================================================

struct ConstDef {
    std::string name;
    std::string type_ref;
    std::string value;
    std::vector<Annotation> annotations;
    std::string doc;
    SourceLoc loc;
};

// ============================================================================
// Message
// ============================================================================

struct MessageDef {
    std::string name;
    std::vector<StructChild> children;

    std::string id;  // required when frame exists
    Direction direction = Direction::Both;

    std::vector<Annotation> annotations;
    std::string doc;
    SourceLoc loc;
};

// ============================================================================
// Import
// ============================================================================

struct ImportDef {
    std::string href;
    std::optional<std::string> ns;
    SourceLoc loc;
};

// ============================================================================
// Protocol Defaults
// ============================================================================

struct Defaults {
    Endian endian = Endian::Big;
    StringEncoding string_encoding = StringEncoding::Ascii;
    StringPadding string_padding = StringPadding::Null;
    StringTrim string_trim = StringTrim::Right;
    std::optional<std::string> namespace_;
};

inline const Defaults BUILTIN_DEFAULTS{};

// ============================================================================
// Frame / Payload (v2)
// ============================================================================

struct PayloadDef {
    bool is_array = false;   // count="*"
    std::unique_ptr<Expr> length_from;  // escape hatch
    SourceLoc loc;
};

struct FrameDef {
    std::string name;
    std::vector<StructChild> header_fields;  // before <payload/>
    PayloadDef payload;
    std::vector<StructChild> footer_fields;  // after <payload/>
    std::vector<Annotation> annotations;
    std::string doc;
    SourceLoc loc;
};

// ============================================================================
// BMDL File (parsed representation of one .bmdl.xml file)
// ============================================================================

struct BmdlFile {
    std::string file_path;
    std::string bmdl_version;

    bool has_protocol = false;
    std::string protocol_name;
    std::string protocol_version;
    Defaults defaults;
    std::string doc;

    std::vector<ImportDef> imports;
    std::vector<ConstDef> constants;
    std::vector<TypeDef> types;
    std::vector<StructDef> structs;
    std::vector<MessageDef> messages;
    std::vector<FrameDef> frames;

    std::vector<std::pair<std::string, SourceLoc>> warnings;

    SourceLoc loc;
};

// ============================================================================
// Protocol (merged root - output of ast_builder)
// ============================================================================

struct Protocol {
    std::string name;
    std::string version;
    std::string bmdl_version;
    Defaults defaults;

    std::vector<ImportDef> imports;
    std::vector<ConstDef> constants;
    std::vector<TypeDef> types;
    std::vector<StructDef> structs;
    std::vector<MessageDef> messages;
    std::vector<FrameDef> frames;

    std::vector<std::pair<std::string, SourceLoc>> warnings;

    std::string doc;
    SourceLoc loc;
};

} // namespace bgen::model
