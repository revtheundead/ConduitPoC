// SPDX-License-Identifier: MIT
// Bgen - AST Dump implementation

#include "ast_dump.hpp"

#include <sstream>
#include <string>

namespace bgen::model {

// ============================================================================
// Helpers
// ============================================================================

static std::string indent(int level) {
    return std::string(static_cast<size_t>(level) * 2, ' ');
}

static const char* primitive_base_str(PrimitiveBase b) {
    switch (b) {
        case PrimitiveBase::Uint:   return "uint";
        case PrimitiveBase::Int:    return "int";
        case PrimitiveBase::Float:  return "float";
        case PrimitiveBase::Bool:   return "bool";
        case PrimitiveBase::Bytes:  return "bytes";
        case PrimitiveBase::String: return "string";
    }
    return "?";
}

static const char* endian_str(Endian e) {
    switch (e) {
        case Endian::Big:    return "big";
        case Endian::Little: return "little";
    }
    return "?";
}

static const char* wire_encoding_str(WireEncoding w) {
    switch (w) {
        case WireEncoding::Default: return "default";
        case WireEncoding::CB2:     return "CB2";
        case WireEncoding::BNR:     return "BNR";
        case WireEncoding::BNR_S:   return "BNR_S";
        case WireEncoding::BCD:     return "BCD";
        case WireEncoding::BCD_S:   return "BCD_S";
    }
    return "?";
}

static const char* string_encoding_str(StringEncoding e) {
    switch (e) {
        case StringEncoding::Ascii:  return "ascii";
        case StringEncoding::Utf8:   return "utf8";
        case StringEncoding::Ia5:    return "ia5";
        case StringEncoding::Ebcdic: return "ebcdic";
    }
    return "?";
}

static const char* string_padding_str(StringPadding p) {
    switch (p) {
        case StringPadding::Null:  return "null";
        case StringPadding::Space: return "space";
        case StringPadding::None:  return "none";
    }
    return "?";
}

static const char* string_trim_str(StringTrim t) {
    switch (t) {
        case StringTrim::Left:  return "left";
        case StringTrim::Right: return "right";
        case StringTrim::Both:  return "both";
        case StringTrim::None:  return "none";
    }
    return "?";
}

static const char* display_format_str(DisplayFormat f) {
    switch (f) {
        case DisplayFormat::Decimal: return "decimal";
        case DisplayFormat::Hex:     return "hex";
        case DisplayFormat::Octal:   return "octal";
        case DisplayFormat::Binary:  return "binary";
    }
    return "?";
}

static const char* validate_timing_str(ValidateTiming t) {
    switch (t) {
        case ValidateTiming::Immediate: return "immediate";
        case ValidateTiming::Deferred:  return "deferred";
    }
    return "?";
}

static const char* direction_str(Direction d) {
    switch (d) {
        case Direction::Both:    return "both";
        case Direction::Send:    return "send";
        case Direction::Receive: return "receive";
    }
    return "?";
}

// ============================================================================
// Expression printer
// ============================================================================

static std::string expr_to_string(const Expr& e) {
    switch (e.op) {
        case ExprOp::NumberLit:
            return std::to_string(e.number_value);
        case ExprOp::BoolLit:
            return e.bool_value ? "true" : "false";
        case ExprOp::FieldRef:
            return e.name;
        case ExprOp::ConstantRef:
            return "const:" + e.name;
        case ExprOp::Remaining:
            return "remaining";

        // Binary ops
        case ExprOp::Add:
        case ExprOp::Sub:
        case ExprOp::Mul:
        case ExprOp::Div:
        case ExprOp::Mod:
        case ExprOp::BitAnd:
        case ExprOp::BitOr:
        case ExprOp::BitXor:
        case ExprOp::ShiftLeft:
        case ExprOp::ShiftRight:
        case ExprOp::Eq:
        case ExprOp::Neq:
        case ExprOp::Lt:
        case ExprOp::Lte:
        case ExprOp::Gt:
        case ExprOp::Gte:
        case ExprOp::LogAnd:
        case ExprOp::LogOr: {
            const char* op_str = "?";
            switch (e.op) {
                case ExprOp::Add:        op_str = "+"; break;
                case ExprOp::Sub:        op_str = "-"; break;
                case ExprOp::Mul:        op_str = "*"; break;
                case ExprOp::Div:        op_str = "/"; break;
                case ExprOp::Mod:        op_str = "%"; break;
                case ExprOp::BitAnd:     op_str = "&"; break;
                case ExprOp::BitOr:      op_str = "|"; break;
                case ExprOp::BitXor:     op_str = "^"; break;
                case ExprOp::ShiftLeft:  op_str = "<<"; break;
                case ExprOp::ShiftRight: op_str = ">>"; break;
                case ExprOp::Eq:         op_str = "=="; break;
                case ExprOp::Neq:        op_str = "!="; break;
                case ExprOp::Lt:         op_str = "<"; break;
                case ExprOp::Lte:        op_str = "<="; break;
                case ExprOp::Gt:         op_str = ">"; break;
                case ExprOp::Gte:        op_str = ">="; break;
                case ExprOp::LogAnd:     op_str = "&&"; break;
                case ExprOp::LogOr:      op_str = "||"; break;
                default: break;
            }
            std::string l = e.left  ? expr_to_string(*e.left)  : "?";
            std::string r = e.right ? expr_to_string(*e.right) : "?";
            return "(" + l + " " + op_str + " " + r + ")";
        }

        // Unary ops
        case ExprOp::Negate:
            return "-(" + (e.left ? expr_to_string(*e.left) : "?") + ")";
        case ExprOp::BitNot:
            return "~(" + (e.left ? expr_to_string(*e.left) : "?") + ")";
        case ExprOp::LogNot:
            return "!(" + (e.left ? expr_to_string(*e.left) : "?") + ")";
    }
    return "?";
}

// ============================================================================
// Constraint printer
// ============================================================================

static void dump_constraint(const Constraint& c, std::ostream& out, int level) {
    out << indent(level) << "constraint:";
    if (c.equals) out << " equals=" << *c.equals;
    if (c.min)    out << " min=" << *c.min;
    if (c.max)    out << " max=" << *c.max;
    if (c.validate != ValidateTiming::Immediate)
        out << " validate=" << validate_timing_str(c.validate);
    out << "\n";
}

// ============================================================================
// Forward declare for recursive StructChild visitor
// ============================================================================

static void dump_children(const std::vector<StructChild>& children,
                          std::ostream& out, int level);

// ============================================================================
// Field printer
// ============================================================================

static void dump_field(const Field& f, std::ostream& out, int level) {
    out << indent(level) << "field \"" << f.name << "\"";
    if (!f.type_ref.empty()) out << " type=" << f.type_ref;
    if (f.type_is_inline) out << " [inline-type]";
    if (f.bits) out << " bits=" << *f.bits;
    if (f.bytes_attr) out << " bytes=" << *f.bytes_attr;
    if (f.is_signed) out << " signed";
    if (f.endian_explicit) out << " endian=" << endian_str(f.endian);
    if (f.wire_encoding) out << " wire=" << wire_encoding_str(*f.wire_encoding);
    if (f.format_explicit) out << " format=" << display_format_str(f.format);
    if (f.scale) out << " scale=" << *f.scale;
    if (f.offset) out << " offset=" << *f.offset;
    if (f.unit) out << " unit=\"" << *f.unit << "\"";
    if (f.length) out << " length=" << *f.length;
    if (f.length_star) out << " length=*";
    if (f.length_from) out << " length-from=" << expr_to_string(*f.length_from);
    if (f.length_prefix) out << " length-prefix=" << *f.length_prefix;
    if (f.length_includes_prefix) out << " length-includes-prefix";
    if (f.encoding) out << " encoding=" << string_encoding_str(*f.encoding);
    if (f.padding) out << " padding=" << string_padding_str(*f.padding);
    if (f.trim) out << " trim=" << string_trim_str(*f.trim);
    if (f.terminated) out << " terminated=\"" << *f.terminated << "\"";
    if (f.max_length) out << " max-length=" << *f.max_length;
    if (f.char_bits) out << " char-bits=" << *f.char_bits;
    if (f.is_inline) out << " inline";
    if (f.default_value) out << " default=\"" << *f.default_value << "\"";
    if (f.auto_attr) out << " auto=\"" << *f.auto_attr << "\"";
    if (f.bit) out << " bit=" << *f.bit;
    if (f.present_when) out << " present-when=" << expr_to_string(*f.present_when);
    out << "\n";

    if (f.constraint) {
        dump_constraint(*f.constraint, out, level + 1);
    }
    for (const auto& ev : f.enum_values) {
        out << indent(level + 1) << "enum " << ev.name << " = " << ev.id << "\n";
    }
    for (const auto& fl : f.flags) {
        out << indent(level + 1) << "flag " << fl.name << " bit=" << fl.bit << "\n";
    }
}

// ============================================================================
// Struct printer
// ============================================================================

static void dump_struct_def(const StructDef& s, std::ostream& out, int level) {
    out << indent(level) << "struct \"" << s.name << "\"";
    if (s.is_bitmap) {
        out << " bitmap bits=" << s.bitmap_bits;
        if (s.bitmap_ext) out << " ext=" << *s.bitmap_ext;
    }
    if (s.bit) out << " bit=" << *s.bit;
    if (s.present_when) out << " present-when=" << expr_to_string(*s.present_when);
    out << "\n";

    dump_children(s.children, out, level + 1);
}

// ============================================================================
// Array printer
// ============================================================================

static void dump_array_def(const ArrayDef& a, std::ostream& out, int level) {
    out << indent(level) << "array \"" << a.name << "\"";
    if (!a.type_ref.empty()) out << " type=" << a.type_ref;
    if (a.fixed_count) out << " count=" << *a.fixed_count;
    if (a.count_from) out << " count-from=" << expr_to_string(*a.count_from);
    if (a.count_star) out << " count=*";
    if (a.count_fx) out << " count=fx";
    if (a.length) out << " length=" << *a.length;
    if (a.length_from) out << " length-from=" << expr_to_string(*a.length_from);
    if (a.bit) out << " bit=" << *a.bit;
    if (a.present_when) out << " present-when=" << expr_to_string(*a.present_when);
    out << "\n";

    dump_children(a.children, out, level + 1);
}

// ============================================================================
// Choice printer
// ============================================================================

static void dump_choice_def(const ChoiceDef& c, std::ostream& out, int level) {
    out << indent(level) << "choice \"" << c.name << "\"";
    if (c.switch_expr) out << " switch=" << expr_to_string(*c.switch_expr);
    if (c.length) out << " length=" << *c.length;
    if (c.length_from) out << " length-from=" << expr_to_string(*c.length_from);
    if (c.bit) out << " bit=" << *c.bit;
    if (c.present_when) out << " present-when=" << expr_to_string(*c.present_when);
    out << "\n";

    for (const auto& cs : c.cases) {
        out << indent(level + 1) << "case \"" << cs.name << "\"";
        if (!cs.type_ref.empty()) out << " type=" << cs.type_ref;
        if (cs.value) out << " value=" << *cs.value;
        if (cs.range) out << " range=" << *cs.range;
        if (cs.direction != Direction::Both) out << " direction=" << direction_str(cs.direction);
        out << "\n";

        dump_children(cs.children, out, level + 2);
    }

    if (c.otherwise) {
        out << indent(level + 1) << "otherwise \"" << c.otherwise->name << "\"";
        if (!c.otherwise->type_ref.empty()) out << " type=" << c.otherwise->type_ref;
        out << "\n";

        dump_children(c.otherwise->children, out, level + 2);
    }
}

// ============================================================================
// FX block printer
// ============================================================================

static void dump_fx_block(const FxBlock& fx, std::ostream& out, int level) {
    out << indent(level) << "fx\n";
    dump_children(fx.children, out, level + 1);
}

// ============================================================================
// Reserved / Align
// ============================================================================

static void dump_reserved(const Reserved& r, std::ostream& out, int level) {
    out << indent(level) << "reserved bits=" << r.bits << "\n";
}

static void dump_align(const Align& a, std::ostream& out, int level) {
    out << indent(level) << "align to=" << a.to << "\n";
}

// ============================================================================
// StructChild visitor (recursive)
// ============================================================================

static void dump_children(const std::vector<StructChild>& children,
                          std::ostream& out, int level) {
    for (const auto& child : children) {
        std::visit([&](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, Field>) {
                dump_field(c, out, level);
            } else if constexpr (std::is_same_v<T, StructDef>) {
                dump_struct_def(c, out, level);
            } else if constexpr (std::is_same_v<T, ArrayDef>) {
                dump_array_def(c, out, level);
            } else if constexpr (std::is_same_v<T, ChoiceDef>) {
                dump_choice_def(c, out, level);
            } else if constexpr (std::is_same_v<T, FxBlock>) {
                dump_fx_block(c, out, level);
            } else if constexpr (std::is_same_v<T, Reserved>) {
                dump_reserved(c, out, level);
            } else if constexpr (std::is_same_v<T, Align>) {
                dump_align(c, out, level);
            }
        }, child);
    }
}

// ============================================================================
// Top-level: dump_protocol
// ============================================================================

void dump_protocol(const Protocol& protocol, std::ostream& out) {
    out << "protocol \"" << protocol.name << "\" version=\"" << protocol.version << "\"";
    if (!protocol.bmdl_version.empty()) out << " bmdl=\"" << protocol.bmdl_version << "\"";
    out << "\n";

    // Doc
    if (!protocol.doc.empty()) {
        out << indent(1) << "doc: \"" << protocol.doc << "\"\n";
    }

    // Defaults
    out << indent(1) << "defaults endian=" << endian_str(protocol.defaults.endian)
        << " string-encoding=" << string_encoding_str(protocol.defaults.string_encoding)
        << " string-padding=" << string_padding_str(protocol.defaults.string_padding)
        << " string-trim=" << string_trim_str(protocol.defaults.string_trim)
        << "\n";

    // Imports
    if (!protocol.imports.empty()) {
        out << "\n";
        out << indent(1) << "--- imports (" << protocol.imports.size() << ") ---\n";
        for (const auto& imp : protocol.imports) {
            out << indent(1) << "import href=\"" << imp.href << "\"";
            if (imp.ns) out << " ns=\"" << *imp.ns << "\"";
            out << "\n";
        }
    }

    // Constants
    if (!protocol.constants.empty()) {
        out << "\n";
        out << indent(1) << "--- constants (" << protocol.constants.size() << ") ---\n";
        for (const auto& c : protocol.constants) {
            out << indent(1) << "const \"" << c.name << "\" type=" << c.type_ref
                << " value=\"" << c.value << "\"\n";
        }
    }

    // Types
    if (!protocol.types.empty()) {
        out << "\n";
        out << indent(1) << "--- types (" << protocol.types.size() << ") ---\n";
        for (const auto& t : protocol.types) {
            out << indent(1) << "type \"" << t.name << "\" base="
                << primitive_base_str(t.base) << " bits=" << t.bits;
            if (t.endian_explicit) out << " endian=" << endian_str(t.endian);
            if (t.wire_encoding_explicit)
                out << " wire=" << wire_encoding_str(t.wire_encoding);
            if (t.format_explicit) out << " format=" << display_format_str(t.format);
            if (t.scale) out << " scale=" << *t.scale;
            if (t.offset) out << " offset=" << *t.offset;
            if (t.unit) out << " unit=\"" << *t.unit << "\"";
            if (t.length) out << " length=" << *t.length;
            if (t.encoding_explicit)
                out << " encoding=" << string_encoding_str(t.encoding);
            if (t.padding_explicit) out << " padding=" << string_padding_str(t.padding);
            if (t.trim_explicit) out << " trim=" << string_trim_str(t.trim);
            if (t.terminated) out << " terminated=\"" << *t.terminated << "\"";
            if (t.max_length) out << " max-length=" << *t.max_length;
            if (t.char_bits) out << " char-bits=" << *t.char_bits;
            out << "\n";

            if (t.constraint) {
                dump_constraint(*t.constraint, out, 2);
            }
            for (const auto& ev : t.enum_values) {
                out << indent(2) << "enum " << ev.name << " = " << ev.id << "\n";
            }
            for (const auto& fl : t.flags) {
                out << indent(2) << "flag " << fl.name << " bit=" << fl.bit << "\n";
            }
        }
    }

    // Structs
    if (!protocol.structs.empty()) {
        out << "\n";
        out << indent(1) << "--- structs (" << protocol.structs.size() << ") ---\n";
        for (const auto& s : protocol.structs) {
            dump_struct_def(s, out, 1);
        }
    }

    // Messages
    if (!protocol.messages.empty()) {
        out << "\n";
        out << indent(1) << "--- messages (" << protocol.messages.size() << ") ---\n";
        for (const auto& m : protocol.messages) {
            out << indent(1) << "message \"" << m.name << "\"";
            out << "\n";

            dump_children(m.children, out, 2);
        }
    }
}

} // namespace bgen::model
