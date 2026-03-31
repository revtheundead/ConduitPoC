// SPDX-License-Identifier: MIT
// Bgen - Java Code Generation Backend Implementation
//
// Generates a self-contained Java codec package from BMDL protocol
// definitions. The output is wire-compatible with C++-generated code.

#include "java_backend.hpp"
#include "cpp_structs_helpers.hpp"
#include "emit_context.hpp"
#include "name_utils.hpp"
#include "../logger.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace bgen::codegen {

namespace {

bool write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out) { Logger::error("cannot write to " + path.string()); return false; }
    out << content;
    out.flush();
    if (!out) { Logger::error("write failed for " + path.string()); return false; }
    return true;
}

// ============================================================================
// Java naming helpers
// ============================================================================

std::string j_class(std::string_view name) { return to_pascal_case(name); }

std::string j_field(std::string_view name) {
    auto s = to_snake_case(name);
    // camelCase for Java
    std::string r;
    bool cap = false;
    for (char c : s) {
        if (c == '_') { cap = true; continue; }
        if (cap) { r += static_cast<char>(std::toupper(static_cast<unsigned char>(c))); cap = false; }
        else r += c;
    }
    // Java keywords
    if (r == "class" || r == "default" || r == "switch" || r == "case" || r == "new" ||
        r == "return" || r == "int" || r == "long" || r == "float" || r == "double" ||
        r == "boolean" || r == "byte" || r == "short" || r == "char" || r == "void" ||
        r == "static" || r == "final" || r == "public" || r == "private" || r == "protected" ||
        r == "abstract" || r == "native" || r == "import" || r == "package")
        r += "_";
    return r;
}

std::string j_const(std::string_view name) {
    std::string r;
    for (size_t i = 0; i < name.size(); i++) {
        char c = name[i];
        if (c == '-' || c == '_') { r += '_'; continue; }
        if (std::isupper(static_cast<unsigned char>(c)) && i > 0 &&
            !std::isupper(static_cast<unsigned char>(name[i-1])) &&
            name[i-1] != '-' && name[i-1] != '_')
            r += '_';
        r += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return r;
}

// Emit a Javadoc comment block if the doc string is non-empty
void j_emit_doc(EmitContext& ctx, const std::string& doc) {
    if (doc.empty()) return;
    // Split multi-line doc into individual lines
    std::istringstream stream(doc);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(stream, line)) {
        // Trim trailing whitespace
        while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) line.pop_back();
        lines.push_back(line);
    }
    // Remove leading/trailing blank lines
    while (!lines.empty() && lines.front().empty()) lines.erase(lines.begin());
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    if (lines.empty()) return;
    if (lines.size() == 1) {
        ctx.line("/** " + lines[0] + " */");
    } else {
        ctx.line("/**");
        for (const auto& l : lines) ctx.line(" * " + l);
        ctx.line(" */");
    }
}

std::string j_hex64(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%016llxL", static_cast<unsigned long long>(v));
    return buf;
}

// Full-precision double literal for Java
std::string j_double(double v) {
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v);
    std::string s(buf, ptr);
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos
        && s.find('E') == std::string::npos)
        s += ".0";
    return s;
}

// Qualify bare constant names (uppercase-starting identifiers) with Constants.
// Constants are emitted as `long` so we add an (int) cast to avoid lossy-conversion
// errors when the value is used in int field assignments or comparisons.
std::string j_qualify_const(const std::string& val, bool is_long = false) {
    if (val.empty()) return val;
    char c = val[0];
    if (std::isalpha(static_cast<unsigned char>(c)) && std::isupper(static_cast<unsigned char>(c))) {
        return "(int) Constants." + val;
    }
    // For numeric literals, check if they need L suffix for Java long fields
    bool is_numeric = (c == '-' || std::isdigit(static_cast<unsigned char>(c)));
    if (is_numeric) {
        try {
            // Check if value exceeds signed long max (need unsigned hex representation)
            if (c != '-') {
                unsigned long long uv = std::stoull(val);
                if (uv > 9223372036854775807ULL) {
                    // Too large for signed long literal; use hex with L suffix
                    std::ostringstream oss;
                    oss << "0x" << std::hex << std::uppercase << uv << "L";
                    return oss.str();
                }
                if (uv > 2147483647ULL || is_long) {
                    return val + "L";
                }
            } else if (is_long) {
                return val + "L";
            }
        } catch (...) { // NOLINT(bugprone-empty-catch)
            // If parsing fails, return as-is
        }
    }
    return val;
}

// ============================================================================
// Java expression codegen
// ============================================================================

// Helper: check if a FieldRef's root name is an enum field
static bool j_is_enum_ref(const model::Expr& e, const std::set<std::string>* enum_fields) {
    if (!enum_fields || e.op != model::ExprOp::FieldRef) return false;
    auto dot = e.name.find('.');
    std::string root = (dot != std::string::npos) ? e.name.substr(0, dot) : e.name;
    return enum_fields->count(root) > 0;
}

std::string j_expr(const model::Expr& e, const std::string& obj = "this",
                    const std::set<std::string>* enum_fields = nullptr) {
    switch (e.op) {
        case model::ExprOp::NumberLit: return std::to_string(e.number_value);
        case model::ExprOp::BoolLit: return e.bool_value ? "true" : "false";
        case model::ExprOp::FieldRef: {
            std::string path = e.name;
            std::string r = obj;
            size_t pos = 0;
            while (pos < path.size()) {
                size_t dot = path.find('.', pos);
                std::string seg;
                if (dot == std::string::npos) { seg = path.substr(pos); pos = path.size(); }
                else { seg = path.substr(pos, dot - pos); pos = dot + 1; }
                r += "." + j_field(seg);
            }
            return r;
        }
        case model::ExprOp::ConstantRef: return "Constants." + j_const(e.name);
        case model::ExprOp::Remaining: return "((int)(r.remainingBits() / 8))";
        case model::ExprOp::Eq:
        case model::ExprOp::Neq:
        case model::ExprOp::Lt:
        case model::ExprOp::Lte:
        case model::ExprOp::Gt:
        case model::ExprOp::Gte: {
            std::string l = j_expr(*e.left, obj, enum_fields);
            std::string r = j_expr(*e.right, obj, enum_fields);
            // For enum fields in comparisons, access the raw .value for numeric comparison
            if (j_is_enum_ref(*e.left, enum_fields)) l += ".value";
            if (j_is_enum_ref(*e.right, enum_fields)) r += ".value";
            switch (e.op) {
                case model::ExprOp::Eq:  return "(" + l + " == " + r + ")";
                case model::ExprOp::Neq: return "(" + l + " != " + r + ")";
                case model::ExprOp::Lt:  return "(" + l + " < " + r + ")";
                case model::ExprOp::Lte: return "(" + l + " <= " + r + ")";
                case model::ExprOp::Gt:  return "(" + l + " > " + r + ")";
                case model::ExprOp::Gte: return "(" + l + " >= " + r + ")";
                default: break;
            }
            break;
        }
        case model::ExprOp::Add:    return "(" + j_expr(*e.left, obj, enum_fields) + " + " + j_expr(*e.right, obj, enum_fields) + ")";
        case model::ExprOp::Sub:    return "(" + j_expr(*e.left, obj, enum_fields) + " - " + j_expr(*e.right, obj, enum_fields) + ")";
        case model::ExprOp::Mul:    return "(" + j_expr(*e.left, obj, enum_fields) + " * " + j_expr(*e.right, obj, enum_fields) + ")";
        case model::ExprOp::Div:    return "(" + j_expr(*e.left, obj, enum_fields) + " / " + j_expr(*e.right, obj, enum_fields) + ")";
        case model::ExprOp::Mod:    return "(" + j_expr(*e.left, obj, enum_fields) + " % " + j_expr(*e.right, obj, enum_fields) + ")";
        case model::ExprOp::LogAnd: return "(" + j_expr(*e.left, obj, enum_fields) + " && " + j_expr(*e.right, obj, enum_fields) + ")";
        case model::ExprOp::LogOr:  return "(" + j_expr(*e.left, obj, enum_fields) + " || " + j_expr(*e.right, obj, enum_fields) + ")";
        case model::ExprOp::BitAnd: return "(" + j_expr(*e.left, obj, enum_fields) + " & " + j_expr(*e.right, obj, enum_fields) + ")";
        case model::ExprOp::BitOr:  return "(" + j_expr(*e.left, obj, enum_fields) + " | " + j_expr(*e.right, obj, enum_fields) + ")";
        case model::ExprOp::BitXor: return "(" + j_expr(*e.left, obj, enum_fields) + " ^ " + j_expr(*e.right, obj, enum_fields) + ")";
        case model::ExprOp::ShiftLeft:  return "(" + j_expr(*e.left, obj, enum_fields) + " << " + j_expr(*e.right, obj, enum_fields) + ")";
        case model::ExprOp::ShiftRight: return "(" + j_expr(*e.left, obj, enum_fields) + " >>> " + j_expr(*e.right, obj, enum_fields) + ")";
        case model::ExprOp::Negate: return "(-" + j_expr(*e.left, obj, enum_fields) + ")";
        case model::ExprOp::BitNot: return "(~" + j_expr(*e.left, obj, enum_fields) + ")";
        case model::ExprOp::LogNot: return "(!" + j_expr(*e.left, obj, enum_fields) + ")";
    }
    return "0";
}

// ============================================================================
// Java field type resolution
// ============================================================================

struct JFieldInfo {
    std::string j_type;      // Java type
    std::string j_boxed;     // Boxed type for generics
    bool is_struct = false;
    bool is_enum = false;
    bool is_string = false;
    bool is_bytes = false;
    bool is_float = false;
    bool is_bool = false;
    bool is_type_wrapper = false; // true for TypeDef wrappers (string/scaled/flags/constrained)
    bool is_string_wrapper = false;
    bool is_scaled_wrapper = false;
    int bits = 0;
    bool is_signed = false;
    bool has_scale = false;
    double scale = 1.0;
    double offset = 0.0;
    int raw_bits = 0;
    bool raw_signed = false;
    model::Endian endian = model::Endian::Big;
    model::WireEncoding wire_enc = model::WireEncoding::Default;
};

JFieldInfo j_resolve_field(const model::Field& f, const analyzer::TypeIndex& index) {
    auto cfi = resolve_field_type(f, index);
    JFieldInfo ji;
    ji.is_struct = cfi.is_struct;
    ji.is_enum = cfi.is_enum;
    ji.is_string = cfi.is_string;
    ji.is_bytes = cfi.is_bytes;
    ji.is_float = cfi.is_float;
    ji.is_bool = cfi.is_bool;
    ji.bits = cfi.bits;
    ji.is_signed = cfi.is_signed;
    ji.has_scale = cfi.has_field_scale;
    ji.scale = cfi.field_scale;
    ji.offset = cfi.field_offset;
    ji.raw_bits = cfi.raw_bits;
    ji.raw_signed = cfi.raw_signed;
    ji.endian = f.endian;
    ji.wire_enc = cfi.wire_encoding;
    // Detect type wrappers: TypeDef resolved as struct (not a real StructDef/MessageDef)
    if (ji.is_struct && !f.type_ref.empty()) {
        auto resolved = index.find(f.type_ref);
        if (resolved) {
            std::visit([&ji](const auto* def) {
                using T = std::decay_t<decltype(*def)>;
                if constexpr (std::is_same_v<T, model::TypeDef>) {
                    ji.is_type_wrapper = true;
                    ji.is_string_wrapper = (def->base == model::PrimitiveBase::String);
                    ji.is_scaled_wrapper = def->scale.has_value() || def->offset.has_value();
                }
            }, *resolved);
        }
    }

    if (ji.is_string) { ji.j_type = "String"; ji.j_boxed = "String"; }
    else if (ji.is_bytes) { ji.j_type = "byte[]"; ji.j_boxed = "byte[]"; }
    else if (ji.is_bool) { ji.j_type = "boolean"; ji.j_boxed = "Boolean"; }
    else if (ji.is_float || ji.has_scale) {
        if (ji.bits <= 32 && !ji.has_scale) { ji.j_type = "float"; ji.j_boxed = "Float"; }
        else { ji.j_type = "double"; ji.j_boxed = "Double"; }
    }
    else if (ji.is_enum || ji.is_struct) {
        ji.j_type = j_class(f.type_ref); ji.j_boxed = ji.j_type;
    }
    else if (ji.bits <= 32) { ji.j_type = "int"; ji.j_boxed = "Integer"; }
    else { ji.j_type = "long"; ji.j_boxed = "Long"; }
    return ji;
}

// ============================================================================
// Outer-scope analysis for nested choices
// ============================================================================

struct JOuterParam {
    std::string bmdl_name;
    std::string java_type;  // e.g., "int", "long"
};

// Map from inline type BMDL name → list of outer-scope decode params it needs
using JOuterScopeMap = std::unordered_map<std::string, std::vector<JOuterParam>>;

// Current decode context: BMDL field name → Java param variable name
using JOuterContext = std::unordered_map<std::string, std::string>;

// Map from BMDL inline type name → resolved Java class name (parent-prefixed or typeName-overridden)
using JInlineNameMap = std::unordered_map<std::string, std::string>;

// Byte alignment tracker for code generation (mirrors C++ StructEmitter::bit_mod8_).
// Tracks cumulative bit offset mod 8 so byte-optimized reads/writes are only
// emitted when the reader/writer is known to be byte-aligned.
static constexpr int J_BITS_PER_BYTE = 8;

struct JBitTracker {
    int bit_mod8 = 0; // -1 means unknown alignment

    bool is_byte_aligned() const { return bit_mod8 == 0; }

    void advance_bits(int bits) {
        if (bit_mod8 < 0) return; // already unknown
        bit_mod8 = (bit_mod8 + bits) % J_BITS_PER_BYTE;
    }

    void advance_bits_variable() {
        if (bit_mod8 != 0) bit_mod8 = -1;
        // else: stays 0 (byte-aligned -> still byte-aligned after whole-byte field)
    }

    void advance_field(const JFieldInfo& fi) {
        if (fi.is_struct || fi.is_string || fi.is_bytes) {
            advance_bits_variable();
        } else {
            advance_bits(fi.bits);
        }
    }
};

// Resolve the Java class name for an inline type, applying parent-prefix or typeName override.
// If type_name_override is set, it is used as-is (PascalCased). Otherwise, the name is
// prefixed with the parent class name to prevent collisions across messages/structs.
std::string j_resolve_inline_name(const std::string& bmdl_name,
                                   const std::string& parent_name,
                                   const std::optional<std::string>& type_name_override) {
    if (type_name_override && !type_name_override->empty())
        return j_class(*type_name_override);
    std::string name = j_class(bmdl_name);
    if (!parent_name.empty())
        name = j_class(parent_name) + name;
    return name;
}

// Look up the resolved class name for an inline type, falling back to j_class(bmdl_name)
std::string j_inline_class(const std::string& bmdl_name, const JInlineNameMap& name_map) {
    auto it = name_map.find(bmdl_name);
    if (it != name_map.end()) return it->second;
    return j_class(bmdl_name);
}

// Collect all FieldRef root names from an expression tree
void j_collect_expr_refs(const model::Expr* expr, std::set<std::string>& refs) {
    if (!expr) return;
    if (expr->op == model::ExprOp::FieldRef) {
        auto dot = expr->name.find('.');
        refs.insert(dot != std::string::npos ? expr->name.substr(0, dot) : expr->name);
    }
    j_collect_expr_refs(expr->left.get(), refs);
    j_collect_expr_refs(expr->right.get(), refs);
}

// Collect FieldRef root names from all direct-child expressions in a scope
void j_collect_scope_refs(const std::vector<model::StructChild>& children,
                           std::set<std::string>& refs) {
    for (const auto& child : children) {
        std::visit([&refs](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, model::Field>) {
                j_collect_expr_refs(c.present_when.get(), refs);
                j_collect_expr_refs(c.length_from.get(), refs);
            } else if constexpr (std::is_same_v<T, model::StructDef>) {
                j_collect_expr_refs(c.present_when.get(), refs);
            } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                j_collect_expr_refs(c.count_from.get(), refs);
                j_collect_expr_refs(c.length_from.get(), refs);
                j_collect_expr_refs(c.present_when.get(), refs);
            } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                j_collect_expr_refs(c.switch_expr.get(), refs);
                j_collect_expr_refs(c.present_when.get(), refs);
                j_collect_expr_refs(c.length_from.get(), refs);
                for (const auto& cs : c.cases) {
                    if (cs.type_ref.empty()) j_collect_scope_refs(cs.children, refs);
                }
                if (c.otherwise && c.otherwise->type_ref.empty()) {
                    j_collect_scope_refs(c.otherwise->children, refs);
                }
            } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                j_collect_scope_refs(c.children, refs);
            }
        }, child);
    }
}

// Collect locally-defined field names in a children list
void j_collect_local_names(const std::vector<model::StructChild>& children,
                            std::set<std::string>& names) {
    for (const auto& child : children) {
        std::visit([&names](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, model::Field>) {
                if (!c.name.empty()) names.insert(c.name);
            } else if constexpr (std::is_same_v<T, model::StructDef>) {
                if (!c.name.empty()) names.insert(c.name);
            } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                if (!c.name.empty()) names.insert(c.name);
            } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                if (!c.name.empty()) names.insert(c.name);
            } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                j_collect_local_names(c.children, names);
            }
        }, child);
    }
}

// Compute outer-scope params for a child inline type
void j_analyze_outer_scope(const std::string& child_name,
                            const std::vector<model::StructChild>& child_children,
                            const std::vector<model::StructChild>& parent_children,
                            const analyzer::TypeIndex& index,
                            const std::string& current_type_name,
                            JOuterScopeMap& map) {
    std::set<std::string> refs;
    j_collect_scope_refs(child_children, refs);

    std::set<std::string> local;
    j_collect_local_names(child_children, local);
    for (const auto& n : local) refs.erase(n);

    if (refs.empty()) return;

    std::vector<JOuterParam> params;
    for (const auto& ref : refs) {
        bool found = false;
        for (const auto& pc : parent_children) {
            if (auto* f = std::get_if<model::Field>(&pc)) {
                if (f->name == ref) {
                    auto fi = j_resolve_field(*f, index);
                    params.push_back({ref, fi.j_type});
                    found = true;
                    break;
                }
            }
        }
        // Transitive: check if parent already receives this as an outer-scope param
        if (!found && !current_type_name.empty()) {
            auto it = map.find(current_type_name);
            if (it != map.end()) {
                for (const auto& p : it->second) {
                    if (p.bmdl_name == ref) {
                        params.push_back(p);
                        found = true;
                        break;
                    }
                }
            }
        }
    }

    if (!params.empty()) {
        map[child_name] = std::move(params);
    }
}

// Evaluate an expression with outer-scope context awareness and enum field detection
std::string j_expr_ctx(const model::Expr& e, const std::string& obj,
                        const JOuterContext& outer_ctx,
                        const std::set<std::string>* enum_fields = nullptr) {
    if (outer_ctx.empty()) return j_expr(e, obj, enum_fields);
    if (e.op == model::ExprOp::FieldRef) {
        auto dot = e.name.find('.');
        std::string root = (dot != std::string::npos) ? e.name.substr(0, dot) : e.name;
        auto it = outer_ctx.find(root);
        if (it != outer_ctx.end()) {
            if (dot != std::string::npos) {
                // Resolve remaining path segments: e.g. "i080.startIndex" → "i080.startIndex"
                std::string rest = e.name.substr(dot);
                std::string result = it->second;
                // Convert remaining segments to Java field names
                size_t pos = 1; // skip the leading dot
                while (pos < rest.size()) {
                    size_t next_dot = rest.find('.', pos);
                    std::string seg;
                    if (next_dot == std::string::npos) { seg = rest.substr(pos); pos = rest.size(); }
                    else { seg = rest.substr(pos, next_dot - pos); pos = next_dot + 1; }
                    result += "." + j_field(seg);
                }
                return result;
            }
            return it->second;
        }
    }
    // For compound expressions, recurse so inner FieldRefs are resolved
    if (e.left && e.right) {
        std::string l = j_expr_ctx(*e.left, obj, outer_ctx, enum_fields);
        std::string r = j_expr_ctx(*e.right, obj, outer_ctx, enum_fields);
        switch (e.op) {
            case model::ExprOp::Add:    return "(" + l + " + " + r + ")";
            case model::ExprOp::Sub:    return "(" + l + " - " + r + ")";
            case model::ExprOp::Mul:    return "(" + l + " * " + r + ")";
            case model::ExprOp::Div:    return "(" + l + " / " + r + ")";
            case model::ExprOp::Mod:    return "(" + l + " % " + r + ")";
            case model::ExprOp::Eq:
            case model::ExprOp::Neq:
            case model::ExprOp::Lt:
            case model::ExprOp::Lte:
            case model::ExprOp::Gt:
            case model::ExprOp::Gte: {
                // For enum fields in comparisons, access the raw .value for numeric comparison
                if (j_is_enum_ref(*e.left, enum_fields)) l += ".value";
                if (j_is_enum_ref(*e.right, enum_fields)) r += ".value";
                switch (e.op) {
                    case model::ExprOp::Eq:  return "(" + l + " == " + r + ")";
                    case model::ExprOp::Neq: return "(" + l + " != " + r + ")";
                    case model::ExprOp::Lt:  return "(" + l + " < " + r + ")";
                    case model::ExprOp::Lte: return "(" + l + " <= " + r + ")";
                    case model::ExprOp::Gt:  return "(" + l + " > " + r + ")";
                    case model::ExprOp::Gte: return "(" + l + " >= " + r + ")";
                    default: break;
                }
                break;
            }
            case model::ExprOp::LogAnd: return "(" + l + " && " + r + ")";
            case model::ExprOp::LogOr:  return "(" + l + " || " + r + ")";
            case model::ExprOp::BitAnd:    return "(" + l + " & " + r + ")";
            case model::ExprOp::BitOr:     return "(" + l + " | " + r + ")";
            case model::ExprOp::BitXor:    return "(" + l + " ^ " + r + ")";
            case model::ExprOp::ShiftLeft: return "(" + l + " << " + r + ")";
            case model::ExprOp::ShiftRight:return "(" + l + " >>> " + r + ")";
            default: break;
        }
    }
    // Unary operators: left child only, no right child
    if (e.left && !e.right) {
        std::string operand = j_expr_ctx(*e.left, obj, outer_ctx, enum_fields);
        switch (e.op) {
            case model::ExprOp::Negate: return "(-" + operand + ")";
            case model::ExprOp::BitNot: return "(~" + operand + ")";
            case model::ExprOp::LogNot: return "(!" + operand + ")";
            default: break;
        }
    }
    return j_expr(e, obj, enum_fields);
}

// Build extra decode arguments for a type that has outer-scope params
std::string j_build_outer_args(const std::string& type_name,
                                const JOuterScopeMap& scope_map,
                                const std::string& pfx,
                                const JOuterContext& outer_ctx) {
    auto it = scope_map.find(type_name);
    if (it == scope_map.end()) return {};
    std::string args;
    for (const auto& p : it->second) {
        // Check if this param is itself an outer-scope param (forwarding)
        auto ctx_it = outer_ctx.find(p.bmdl_name);
        if (ctx_it != outer_ctx.end()) {
            args += ", " + ctx_it->second;
        } else {
            args += ", " + pfx + "." + j_field(p.bmdl_name);
        }
    }
    return args;
}

// ============================================================================
// Java read/write helpers
// ============================================================================

// Returns true when j_read_expr returns long but the field is int-typed,
// meaning an explicit (int) narrowing cast is needed.
bool j_needs_int_cast(const JFieldInfo& fi, bool byte_aligned = true) {
    if (fi.j_type != "int") return false;
    if (fi.is_float) return false;
    // readU8, readU16, readU32 already return int — no cast needed
    // But only when byte_aligned; otherwise readBits() returns long
    if (byte_aligned && !fi.is_signed && fi.wire_enc == model::WireEncoding::Default) {
        if (fi.bits == 8 || fi.bits == 16 || fi.bits == 32) return false;
    }
    // Signed byte-aligned reads for 32-bit already return int (readU32 returns int)
    if (byte_aligned && fi.is_signed && fi.wire_enc == model::WireEncoding::Default) {
        if (fi.bits == 32) return false;
    }
    return true;
}

std::string j_read_expr(const JFieldInfo& fi, bool byte_aligned = true) {
    if (fi.wire_enc == model::WireEncoding::BCD) return std::string(fi.bits > 32 ? "(long)" : "(int)") + " r.readBcd(" + std::to_string(fi.bits) + ")";
    if (fi.wire_enc == model::WireEncoding::BCD_S) return std::string(fi.bits > 32 ? "(long)" : "(int)") + " r.readBcdSigned(" + std::to_string(fi.bits) + ")";
    if (fi.wire_enc == model::WireEncoding::BNR_S) return "r.readSignMagnitude(" + std::to_string(fi.bits) + ")";
    bool be = (fi.endian == model::Endian::Big);
    if (fi.is_float) {
        if (fi.bits == 16) return std::string("r.readF16(") + (be ? "true" : "false") + ")";
        if (fi.bits <= 32) return std::string("r.readF32(") + (be ? "true" : "false") + ")";
        if (fi.bits <= 48) return std::string("r.readF48(") + (be ? "true" : "false") + ")";
        if (fi.bits <= 64) return std::string("r.readF64(") + (be ? "true" : "false") + ")";
        return "r.readBits(" + std::to_string(fi.bits) + ")";
    }
    if (byte_aligned) {
        if (fi.bits == 8 && !fi.is_signed) return "r.readU8()";
        if (fi.bits == 16 && !fi.is_signed) return std::string("r.readU16(") + (be ? "true" : "false") + ")";
        if (fi.bits == 32 && !fi.is_signed) return std::string("r.readU32(") + (be ? "true" : "false") + ")";
        if (fi.bits == 64 && !fi.is_signed) return std::string("r.readU64(") + (be ? "true" : "false") + ")";
        // Signed byte-aligned reads: read unsigned then cast (matches C++ emit_read_expr)
        if (fi.bits == 16 && fi.is_signed) return std::string("(short) r.readU16(") + (be ? "true" : "false") + ")";
        if (fi.bits == 32 && fi.is_signed) return std::string("r.readU32(") + (be ? "true" : "false") + ")";
        if (fi.bits == 64 && fi.is_signed) return std::string("r.readU64(") + (be ? "true" : "false") + ")";
    }
    if (fi.is_signed) return "r.readSignedBits(" + std::to_string(fi.bits) + ")";
    return "r.readBits(" + std::to_string(fi.bits) + ")";
}

std::string j_write_stmt(const std::string& val, const JFieldInfo& fi, bool byte_aligned = true) {
    if (fi.wire_enc == model::WireEncoding::BCD) return "w.writeBcd(" + val + ", " + std::to_string(fi.bits) + ")";
    if (fi.wire_enc == model::WireEncoding::BCD_S) return "w.writeBcdSigned(" + val + ", " + std::to_string(fi.bits) + ")";
    if (fi.wire_enc == model::WireEncoding::BNR_S) return "w.writeSignMagnitude(" + val + ", " + std::to_string(fi.bits) + ")";
    bool be = (fi.endian == model::Endian::Big);
    if (fi.is_float) {
        if (fi.bits == 16) return std::string("w.writeF16(") + val + ", " + (be ? "true" : "false") + ")";
        if (fi.bits <= 32) return std::string("w.writeF32(") + val + ", " + (be ? "true" : "false") + ")";
        if (fi.bits <= 48) return std::string("w.writeF48(") + val + ", " + (be ? "true" : "false") + ")";
        if (fi.bits <= 64) return std::string("w.writeF64(") + val + ", " + (be ? "true" : "false") + ")";
        return "w.writeBits(" + val + ", " + std::to_string(fi.bits) + ")";
    }
    // bool must be checked before bit-width checks since (int)boolean is illegal in Java
    if (fi.is_bool) return "w.writeBits(" + val + " ? 1 : 0, " + std::to_string(fi.bits) + ")";
    if (byte_aligned) {
        if (fi.bits == 8 && !fi.is_signed) return "w.writeU8(" + val + ")";
        if (fi.bits == 16 && !fi.is_signed) return std::string("w.writeU16(") + val + ", " + (be ? "true" : "false") + ")";
        if (fi.bits == 32 && !fi.is_signed) return std::string("w.writeU32(") + val + ", " + (be ? "true" : "false") + ")";
        if (fi.bits == 64 && !fi.is_signed) return std::string("w.writeU64(") + val + ", " + (be ? "true" : "false") + ")";
    }
    if (fi.is_signed) return "w.writeSignedBits(" + val + ", " + std::to_string(fi.bits) + ")";
    return "w.writeBits(" + val + ", " + std::to_string(fi.bits) + ")";
}

// ============================================================================
// BitReader.java / BitWriter.java
// ============================================================================

std::string generate_j_bit_reader(const std::string& pkg) {
    EmitContext ctx;
    ctx.line("// Generated by bgen - DO NOT EDIT");
    ctx.line("package " + pkg + ".codec;");
    ctx.line();
    ctx.line("import java.nio.ByteBuffer;");
    ctx.line("import java.nio.ByteOrder;");
    ctx.line("import java.nio.charset.StandardCharsets;");
    ctx.line();
    ctx.line("public final class BitReader {");
    ctx.indent();
    ctx.line("private final byte[] data;");
    ctx.line("private int bitPos;");
    ctx.line("private final int bitLen;");
    ctx.line();
    // EBCDIC-to-ASCII conversion table (standard EBCDIC Code Page 037 mapping)
    ctx.line("private static final byte[] EBCDIC_TO_ASCII = new byte[256];");
    ctx.line("static {");
    ctx.indent();
    ctx.line("java.util.Arrays.fill(EBCDIC_TO_ASCII, (byte)0x3F);"); // default '?'
    ctx.line("int[] map = {");
    ctx.indent();
    ctx.line("0x40,' ', 0x4B,'.', 0x4C,'<', 0x4D,'(', 0x4E,'+', 0x4F,'|',");
    ctx.line("0x50,'&', 0x5A,'!', 0x5B,'$', 0x5C,'*', 0x5D,')', 0x5E,';',");
    ctx.line("0x60,'-', 0x61,'/', 0x6B,',', 0x6C,'%', 0x6D,'_', 0x6E,'>',");
    ctx.line("0x6F,'?', 0x7A,':', 0x7B,'#', 0x7C,'@', 0x7D,'\\'', 0x7E,'=', 0x7F,'\"',");
    ctx.line("0xC1,'A', 0xC2,'B', 0xC3,'C', 0xC4,'D', 0xC5,'E', 0xC6,'F',");
    ctx.line("0xC7,'G', 0xC8,'H', 0xC9,'I', 0xD1,'J', 0xD2,'K', 0xD3,'L',");
    ctx.line("0xD4,'M', 0xD5,'N', 0xD6,'O', 0xD7,'P', 0xD8,'Q', 0xD9,'R',");
    ctx.line("0xE2,'S', 0xE3,'T', 0xE4,'U', 0xE5,'V', 0xE6,'W', 0xE7,'X',");
    ctx.line("0xE8,'Y', 0xE9,'Z',");
    ctx.line("0x81,'a', 0x82,'b', 0x83,'c', 0x84,'d', 0x85,'e', 0x86,'f',");
    ctx.line("0x87,'g', 0x88,'h', 0x89,'i', 0x91,'j', 0x92,'k', 0x93,'l',");
    ctx.line("0x94,'m', 0x95,'n', 0x96,'o', 0x97,'p', 0x98,'q', 0x99,'r',");
    ctx.line("0xA2,'s', 0xA3,'t', 0xA4,'u', 0xA5,'v', 0xA6,'w', 0xA7,'x',");
    ctx.line("0xA8,'y', 0xA9,'z',");
    ctx.line("0xF0,'0', 0xF1,'1', 0xF2,'2', 0xF3,'3', 0xF4,'4', 0xF5,'5',");
    ctx.line("0xF6,'6', 0xF7,'7', 0xF8,'8', 0xF9,'9',");
    ctx.line("};");
    ctx.dedent();
    ctx.line("for (int i=0;i<map.length;i+=2) EBCDIC_TO_ASCII[map[i]]=(byte)map[i+1];");
    ctx.dedent();
    ctx.line("}");
    ctx.line();
    ctx.line("public BitReader(byte[] data) { this.data = data; this.bitPos = 0; this.bitLen = data.length * 8; }");
    ctx.line();
    ctx.line("public int remainingBits() { return Math.max(0, bitLen - bitPos); }");
    ctx.line("public int remainingBytes() { return remainingBits() / 8; }");
    ctx.line();
    ctx.line("private void check(int n) { if (bitPos + n > bitLen) throw new ConduitCodecException(\"underflow\"); }");
    ctx.line();
    ctx.line("public long readBits(int n) {");
    ctx.indent();
    ctx.line("check(n);");
    ctx.line("if ((bitPos & 7) == 0 && (n & 7) == 0) {");
    ctx.indent();
    ctx.line("long val = 0; int idx = bitPos / 8; int bytes = n / 8;");
    ctx.line("for (int i = 0; i < bytes; i++) val = (val << 8) | (data[idx + i] & 0xFF);");
    ctx.line("bitPos += n; return val;");
    ctx.dedent();
    ctx.line("}");
    ctx.line("long val = 0;");
    ctx.line("int rem = n;");
    ctx.line("while (rem > 0) {");
    ctx.indent();
    ctx.line("int byteIdx = bitPos / 8;");
    ctx.line("int bitOff = bitPos % 8;");
    ctx.line("int avail = 8 - bitOff;");
    ctx.line("int take = Math.min(avail, rem);");
    ctx.line("int mask = ((1 << take) - 1) << (avail - take);");
    ctx.line("long bits = (data[byteIdx] & mask) >>> (avail - take);");
    ctx.line("val = (val << take) | bits;");
    ctx.line("bitPos += take;");
    ctx.line("rem -= take;");
    ctx.dedent();
    ctx.line("}");
    ctx.line("return val;");
    ctx.dedent();
    ctx.line("}");
    ctx.line();
    ctx.line("public long readSignedBits(int n) {");
    ctx.indent();
    ctx.line("long val = readBits(n);");
    ctx.line("if (n < 64 && n > 0 && ((val >> (n - 1)) & 1) != 0) val -= (1L << n);");
    ctx.line("return val;");
    ctx.dedent();
    ctx.line("}");
    ctx.line();
    ctx.line("public int readU8() { return (int) readBits(8); }");
    ctx.line("public int readU16(boolean bigEndian) {");
    ctx.indent();
    ctx.line("byte[] b = {(byte)readBits(8), (byte)readBits(8)};");
    ctx.line("return Short.toUnsignedInt(ByteBuffer.wrap(b).order(bigEndian ? ByteOrder.BIG_ENDIAN : ByteOrder.LITTLE_ENDIAN).getShort());");
    ctx.dedent();
    ctx.line("}");
    ctx.line("public int readU32(boolean bigEndian) {");
    ctx.indent();
    ctx.line("byte[] b = new byte[4]; for (int i=0;i<4;i++) b[i]=(byte)readBits(8);");
    ctx.line("return ByteBuffer.wrap(b).order(bigEndian ? ByteOrder.BIG_ENDIAN : ByteOrder.LITTLE_ENDIAN).getInt();");
    ctx.dedent();
    ctx.line("}");
    ctx.line("public long readU64(boolean bigEndian) {");
    ctx.indent();
    ctx.line("byte[] b = new byte[8]; for (int i=0;i<8;i++) b[i]=(byte)readBits(8);");
    ctx.line("return ByteBuffer.wrap(b).order(bigEndian ? ByteOrder.BIG_ENDIAN : ByteOrder.LITTLE_ENDIAN).getLong();");
    ctx.dedent();
    ctx.line("}");
    ctx.line("public float readF16(boolean bigEndian) {");
    ctx.indent();
    ctx.line("int raw = (int) readBits(16);");
    ctx.line("if (!bigEndian) raw = ((raw & 0xFF) << 8) | ((raw >> 8) & 0xFF);");
    ctx.line("int sign = (raw & 0x8000) << 16;");
    ctx.line("int exp = (raw >> 10) & 0x1F;");
    ctx.line("int mant = raw & 0x03FF;");
    ctx.line("if (exp == 0) { if (mant == 0) return Float.intBitsToFloat(sign); exp = 1; while ((mant & 0x0400) == 0) { mant <<= 1; exp--; } mant &= ~0x0400; exp += (127 - 15); return Float.intBitsToFloat(sign | (exp << 23) | (mant << 13)); }");
    ctx.line("if (exp == 31) return Float.intBitsToFloat(sign | 0x7F800000 | (mant << 13));");
    ctx.line("return Float.intBitsToFloat(sign | ((exp + 112) << 23) | (mant << 13));");
    ctx.dedent();
    ctx.line("}");
    ctx.line("public float readF32(boolean bigEndian) {");
    ctx.indent();
    ctx.line("byte[] b = new byte[4]; for (int i=0;i<4;i++) b[i]=(byte)readBits(8);");
    ctx.line("return ByteBuffer.wrap(b).order(bigEndian ? ByteOrder.BIG_ENDIAN : ByteOrder.LITTLE_ENDIAN).getFloat();");
    ctx.dedent();
    ctx.line("}");
    ctx.line("public double readF48(boolean bigEndian) {");
    ctx.indent();
    ctx.line("byte[] b = new byte[6]; for (int i=0;i<6;i++) b[i]=(byte)readBits(8);");
    ctx.line("if (!bigEndian) { for (int i=0;i<3;i++) { byte t=b[i]; b[i]=b[5-i]; b[5-i]=t; } }");
    ctx.line("long raw = 0; for (int i=0;i<6;i++) raw = (raw<<8)|(b[i]&0xFF);");
    ctx.line("return Double.longBitsToDouble(raw << 16);");
    ctx.dedent();
    ctx.line("}");
    ctx.line("public double readF64(boolean bigEndian) {");
    ctx.indent();
    ctx.line("byte[] b = new byte[8]; for (int i=0;i<8;i++) b[i]=(byte)readBits(8);");
    ctx.line("return ByteBuffer.wrap(b).order(bigEndian ? ByteOrder.BIG_ENDIAN : ByteOrder.LITTLE_ENDIAN).getDouble();");
    ctx.dedent();
    ctx.line("}");
    ctx.line("public String readString(int len) {");
    ctx.indent();
    ctx.line("byte[] b = new byte[len]; for (int i=0;i<len;i++) b[i]=(byte)readBits(8);");
    ctx.line("return new String(b, StandardCharsets.ISO_8859_1);");
    ctx.dedent();
    ctx.line("}");
    // Encoding-aware string read: 0=ASCII, 1=IA5, 2=EBCDIC
    ctx.line("public String readStringEncoded(int len, int enc) {");
    ctx.indent();
    ctx.line("byte[] b = new byte[len]; for (int i=0;i<len;i++) b[i]=(byte)readBits(8);");
    ctx.line("if (enc == 2) { for (int i=0;i<b.length;i++) b[i]=EBCDIC_TO_ASCII[b[i]&0xFF]; }");
    ctx.line("else if (enc == 1) { for (int i=0;i<b.length;i++) b[i]=(byte)(b[i]&0x7F); }");
    ctx.line("return new String(b, StandardCharsets.ISO_8859_1);");
    ctx.dedent();
    ctx.line("}");
    // Packed character read: reads char_bits per character with IA5 6-bit mapping
    ctx.line("public String readPackedChars(int count, int charBits) {");
    ctx.indent();
    ctx.line("StringBuilder sb = new StringBuilder(count);");
    ctx.line("for (int i=0;i<count;i++) { int c=(int)readBits(charBits); if (charBits<7) sb.append((char)(c==0?0:(c<32?c+0x40:c))); else sb.append((char)c); }");
    ctx.line("return sb.toString();");
    ctx.dedent();
    ctx.line("}");
    // Terminated string read
    ctx.line("public String readTerminatedString(int terminator, int maxLen) {");
    ctx.indent();
    ctx.line("StringBuilder sb = new StringBuilder();");
    ctx.line("for (int i=0;i<maxLen;i++) { int c=(int)readBits(8); if (c==terminator) break; sb.append((char)c); }");
    ctx.line("return sb.toString();");
    ctx.dedent();
    ctx.line("}");
    ctx.line("public String readCrlfTerminatedString(int maxLen) {");
    ctx.indent();
    ctx.line("StringBuilder sb = new StringBuilder(); int prev=0;");
    ctx.line("for (int i=0;i<maxLen;i++) { int c=(int)readBits(8); if (prev==0x0D && c==0x0A) { sb.deleteCharAt(sb.length()-1); break; } sb.append((char)c); prev=c; }");
    ctx.line("return sb.toString();");
    ctx.dedent();
    ctx.line("}");
    ctx.line("public byte[] readBytes(int len) {");
    ctx.indent();
    ctx.line("byte[] b = new byte[len]; for (int i=0;i<len;i++) b[i]=(byte)readBits(8); return b;");
    ctx.dedent();
    ctx.line("}");
    ctx.line("public long readBcd(int bits) {");
    ctx.indent();
    ctx.line("long raw = readBits(bits); long result = 0; long mult = 1;");
    ctx.line("for (int i = 0; i < bits/4; i++) { result += ((raw >> (i*4)) & 0xF) * mult; mult *= 10; }");
    ctx.line("return result;");
    ctx.dedent();
    ctx.line("}");
    ctx.line("public long readBcdSigned(int bits) {");
    ctx.indent();
    ctx.line("long sign = readBits(1); long val = readBcd(bits-1); return sign != 0 ? -val : val;");
    ctx.dedent();
    ctx.line("}");
    ctx.line("public long readSignMagnitude(int bits) {");
    ctx.indent();
    ctx.line("long sign = readBits(1); long mag = readBits(bits-1); return sign != 0 ? -mag : mag;");
    ctx.dedent();
    ctx.line("}");
    ctx.line("public void skipBits(int n) { check(n); bitPos += n; }");
    ctx.line("public BitReader subReader(int byteCount) { return new BitReader(readBytes(byteCount)); }");
    ctx.line("public void alignTo(int boundary) {");
    ctx.indent();
    ctx.line("if (boundary <= 0) return;");
    ctx.line("int rem = bitPos % 8;");
    ctx.line("if (rem != 0) bitPos += (8 - rem);");
    ctx.line("int bytePos = bitPos / 8;");
    ctx.line("int bRem = bytePos % boundary;");
    ctx.line("if (bRem != 0) bitPos += (boundary - bRem) * 8;");
    ctx.line("if (bitPos > bitLen) bitPos = bitLen;");
    ctx.dedent();
    ctx.line("}");
    ctx.dedent();
    ctx.line("}");
    return ctx.str();
}

std::string generate_j_bit_writer(const std::string& pkg) {
    EmitContext ctx;
    ctx.line("// Generated by bgen - DO NOT EDIT");
    ctx.line("package " + pkg + ".codec;");
    ctx.line();
    ctx.line("import java.nio.ByteBuffer;");
    ctx.line("import java.nio.ByteOrder;");
    ctx.line("import java.nio.charset.StandardCharsets;");
    ctx.line("import java.util.Arrays;");
    ctx.line();
    ctx.line("public final class BitWriter {");
    ctx.indent();
    // ASCII-to-EBCDIC conversion table (reverse of EBCDIC_TO_ASCII in BitReader)
    ctx.line("private static final byte[] ASCII_TO_EBCDIC = new byte[256];");
    ctx.line("static {");
    ctx.indent();
    ctx.line("java.util.Arrays.fill(ASCII_TO_EBCDIC, (byte)0x40);"); // default space
    ctx.line("int[] map = {");
    ctx.indent();
    ctx.line("' ',0x40, '.',0x4B, '<',0x4C, '(',0x4D, '+',0x4E, '|',0x4F,");
    ctx.line("'&',0x50, '!',0x5A, '$',0x5B, '*',0x5C, ')',0x5D, ';',0x5E,");
    ctx.line("'-',0x60, '/',0x61, ',',0x6B, '%',0x6C, '_',0x6D, '>',0x6E,");
    ctx.line("'?',0x6F, ':',0x7A, '#',0x7B, '@',0x7C, '\\'',0x7D, '=',0x7E, '\"',0x7F,");
    ctx.line("'A',0xC1, 'B',0xC2, 'C',0xC3, 'D',0xC4, 'E',0xC5, 'F',0xC6,");
    ctx.line("'G',0xC7, 'H',0xC8, 'I',0xC9, 'J',0xD1, 'K',0xD2, 'L',0xD3,");
    ctx.line("'M',0xD4, 'N',0xD5, 'O',0xD6, 'P',0xD7, 'Q',0xD8, 'R',0xD9,");
    ctx.line("'S',0xE2, 'T',0xE3, 'U',0xE4, 'V',0xE5, 'W',0xE6, 'X',0xE7,");
    ctx.line("'Y',0xE8, 'Z',0xE9,");
    ctx.line("'a',0x81, 'b',0x82, 'c',0x83, 'd',0x84, 'e',0x85, 'f',0x86,");
    ctx.line("'g',0x87, 'h',0x88, 'i',0x89, 'j',0x91, 'k',0x92, 'l',0x93,");
    ctx.line("'m',0x94, 'n',0x95, 'o',0x96, 'p',0x97, 'q',0x98, 'r',0x99,");
    ctx.line("'s',0xA2, 't',0xA3, 'u',0xA4, 'v',0xA5, 'w',0xA6, 'x',0xA7,");
    ctx.line("'y',0xA8, 'z',0xA9,");
    ctx.line("'0',0xF0, '1',0xF1, '2',0xF2, '3',0xF3, '4',0xF4, '5',0xF5,");
    ctx.line("'6',0xF6, '7',0xF7, '8',0xF8, '9',0xF9,");
    ctx.line("};");
    ctx.dedent();
    ctx.line("for (int i=0;i<map.length;i+=2) ASCII_TO_EBCDIC[map[i]]=(byte)map[i+1];");
    ctx.dedent();
    ctx.line("}");
    ctx.line();
    ctx.line("private byte[] buf = new byte[64];");
    ctx.line("private int bitPos;");
    ctx.line();
    ctx.line("private void ensure(int n) {");
    ctx.indent();
    ctx.line("int needed = (bitPos + n + 7) / 8;");
    ctx.line("if (needed > buf.length) buf = Arrays.copyOf(buf, Math.max(needed, buf.length * 2));");
    ctx.dedent();
    ctx.line("}");
    ctx.line();
    ctx.line("public void writeBits(long value, int n) {");
    ctx.indent();
    ctx.line("ensure(n);");
    ctx.line("if ((bitPos & 7) == 0 && (n & 7) == 0) {");
    ctx.indent();
    ctx.line("int idx = bitPos / 8; int bytes = n / 8;");
    ctx.line("for (int i = bytes - 1; i >= 0; i--) { buf[idx + bytes - 1 - i] = (byte)(value >> (i * 8)); }");
    ctx.line("bitPos += n; return;");
    ctx.dedent();
    ctx.line("}");
    ctx.line("int rem = n;");
    ctx.line("while (rem > 0) {");
    ctx.indent();
    ctx.line("int byteIdx = bitPos / 8;");
    ctx.line("int bitOff = bitPos % 8;");
    ctx.line("int avail = 8 - bitOff;");
    ctx.line("int take = Math.min(avail, rem);");
    ctx.line("int shift = rem - take;");
    ctx.line("int bits = (int)((value >> shift) & ((1 << take) - 1));");
    ctx.line("buf[byteIdx] |= (byte)(bits << (avail - take));");
    ctx.line("bitPos += take;");
    ctx.line("rem -= take;");
    ctx.dedent();
    ctx.line("}");
    ctx.dedent();
    ctx.line("}");
    ctx.line();
    ctx.line("public void writeSignedBits(long value, int n) {");
    ctx.indent();
    ctx.line("if (n >= 64) { writeBits(value, n); return; }");
    ctx.line("if (value < 0) value += (1L << n);");
    ctx.line("writeBits(value & ((1L << n) - 1), n);");
    ctx.dedent();
    ctx.line("}");
    ctx.line();
    ctx.line("public void writeU8(int v) { writeBits(v & 0xFF, 8); }");
    ctx.line("public void writeU16(int v, boolean be) { byte[] b = new byte[2]; ByteBuffer.wrap(b).order(be?ByteOrder.BIG_ENDIAN:ByteOrder.LITTLE_ENDIAN).putShort((short)v); for (byte x:b) writeBits(x&0xFF,8); }");
    ctx.line("public void writeU32(int v, boolean be) { byte[] b = new byte[4]; ByteBuffer.wrap(b).order(be?ByteOrder.BIG_ENDIAN:ByteOrder.LITTLE_ENDIAN).putInt(v); for (byte x:b) writeBits(x&0xFF,8); }");
    ctx.line("public void writeU64(long v, boolean be) { byte[] b = new byte[8]; ByteBuffer.wrap(b).order(be?ByteOrder.BIG_ENDIAN:ByteOrder.LITTLE_ENDIAN).putLong(v); for (byte x:b) writeBits(x&0xFF,8); }");
    ctx.line("public void writeF16(float v, boolean be) { int fb = Float.floatToRawIntBits(v); int sign = (fb >> 16) & 0x8000; int exp = ((fb >> 23) & 0xFF) - 127 + 15; int mant = fb & 0x007FFFFF; int h; if (((fb >> 23) & 0xFF) == 255) h = sign | 0x7C00 | (mant != 0 ? 0x0200 : 0); else if (exp >= 31) h = sign | 0x7C00; else if (exp <= 0) { if (exp < -10) h = sign; else { mant |= 0x00800000; int shift = 1 - exp; h = sign | (mant >> (13 + shift)); } } else h = sign | (exp << 10) | (mant >> 13); if (be) { writeBits((h >> 8) & 0xFF, 8); writeBits(h & 0xFF, 8); } else { writeBits(h & 0xFF, 8); writeBits((h >> 8) & 0xFF, 8); } }");
    ctx.line("public void writeF32(float v, boolean be) { byte[] b = new byte[4]; ByteBuffer.wrap(b).order(be?ByteOrder.BIG_ENDIAN:ByteOrder.LITTLE_ENDIAN).putFloat(v); for (byte x:b) writeBits(x&0xFF,8); }");
    ctx.line("public void writeF48(double v, boolean be) { long raw = Double.doubleToRawLongBits(v) >>> 16; byte[] b = new byte[6]; for (int i=5;i>=0;i--) { b[i]=(byte)(raw&0xFF); raw>>>=8; } if (!be) { for (int i=0;i<3;i++) { byte t=b[i]; b[i]=b[5-i]; b[5-i]=t; } } for (byte x:b) writeBits(x&0xFF,8); }");
    ctx.line("public void writeF64(double v, boolean be) { byte[] b = new byte[8]; ByteBuffer.wrap(b).order(be?ByteOrder.BIG_ENDIAN:ByteOrder.LITTLE_ENDIAN).putDouble(v); for (byte x:b) writeBits(x&0xFF,8); }");
    ctx.line("public void writeString(String s, int len, int pad) {");
    ctx.indent();
    ctx.line("byte[] enc = s.getBytes(StandardCharsets.ISO_8859_1);");
    ctx.line("for (int i=0;i<len;i++) writeU8(i<enc.length ? enc[i]&0xFF : pad);");
    ctx.dedent();
    ctx.line("}");
    // Encoding-aware string write: 0=ASCII, 1=IA5, 2=EBCDIC
    ctx.line("public void writeStringEncoded(String s, int len, int pad, int enc) {");
    ctx.indent();
    ctx.line("byte[] b = s.getBytes(StandardCharsets.ISO_8859_1);");
    ctx.line("if (enc == 2) { for (int i=0;i<b.length;i++) b[i]=ASCII_TO_EBCDIC[b[i]&0xFF]; }");
    ctx.line("else if (enc == 1) { for (int i=0;i<b.length;i++) b[i]=(byte)(b[i]&0x7F); }");
    ctx.line("for (int i=0;i<len;i++) writeU8(i<b.length ? b[i]&0xFF : pad);");
    ctx.dedent();
    ctx.line("}");
    // Packed character write: writes char_bits per character with IA5 6-bit mapping
    ctx.line("public void writePackedChars(String s, int count, int charBits) { writePackedChars(s, count, charBits, 0x20); }");
    ctx.line("public void writePackedChars(String s, int count, int charBits, int pad) {");
    ctx.indent();
    ctx.line("for (int i=0;i<count;i++) { int c=i<s.length()?(s.charAt(i)&0xFF):pad; if (charBits<7 && c>='a' && c<='z') c-=32; writeBits(c>=0x40?c-0x40:c, charBits); }");
    ctx.dedent();
    ctx.line("}");
    // Terminated string write
    ctx.line("public void writeTerminatedString(String s, int terminator) {");
    ctx.indent();
    ctx.line("for (int i=0;i<s.length();i++) writeU8(s.charAt(i)&0xFF);");
    ctx.line("writeU8(terminator);");
    ctx.dedent();
    ctx.line("}");
    ctx.line("public void writeCrlfTerminatedString(String s) {");
    ctx.indent();
    ctx.line("for (int i=0;i<s.length();i++) writeU8(s.charAt(i)&0xFF);");
    ctx.line("writeU8(0x0D); writeU8(0x0A);");
    ctx.dedent();
    ctx.line("}");
    ctx.line("public void writeBytes(byte[] data) { for (byte b:data) writeU8(b&0xFF); }");
    ctx.line("public void writeBcd(long val, int bits) {");
    ctx.indent();
    ctx.line("int digits=bits/4; long limit=1; for(int i=0;i<digits;i++) limit*=10;");
    ctx.line("if (Math.abs(val)>=limit) throw new ConduitCodecException(\"BCD value \"+val+\" exceeds capacity of \"+digits+\" digits\");");
    ctx.line("long raw=0; long v=Math.abs(val);");
    ctx.line("for (int i=0;i<bits/4;i++) { raw|=(v%10)<<(i*4); v/=10; }");
    ctx.line("writeBits(raw, bits);");
    ctx.dedent();
    ctx.line("}");
    ctx.line("public void writeBcdSigned(long val, int bits) {");
    ctx.indent();
    ctx.line("int digits=(bits-1)/4; long limit=1; for(int i=0;i<digits;i++) limit*=10;");
    ctx.line("if (Math.abs(val)>=limit) throw new ConduitCodecException(\"BCD signed value \"+val+\" exceeds capacity of \"+digits+\" digits\");");
    ctx.line("writeBits(val<0?1:0,1); writeBcd(Math.abs(val),bits-1);");
    ctx.dedent();
    ctx.line("}");
    ctx.line("public void writeSignMagnitude(long val, int bits) { writeBits(val<0?1:0,1); writeBits(Math.abs(val),bits-1); }");
    ctx.line("public int sizeBytes() { return (bitPos+7)/8; }");
    ctx.line("public byte[] toBytes() { return Arrays.copyOf(buf, sizeBytes()); }");
    ctx.line("public void patchU8(int off, int v) { if (off<buf.length) buf[off]=(byte)(v&0xFF); }");
    ctx.line("public void patchU16(int off, int v, boolean be) { byte[] b=new byte[2]; ByteBuffer.wrap(b).order(be?ByteOrder.BIG_ENDIAN:ByteOrder.LITTLE_ENDIAN).putShort((short)v); for(int i=0;i<2&&off+i<buf.length;i++) buf[off+i]=b[i]; }");
    ctx.line("public void patchU32(int off, int v, boolean be) { byte[] b=new byte[4]; ByteBuffer.wrap(b).order(be?ByteOrder.BIG_ENDIAN:ByteOrder.LITTLE_ENDIAN).putInt(v); for(int i=0;i<4&&off+i<buf.length;i++) buf[off+i]=b[i]; }");
    ctx.line("public void alignTo(int boundary) {");
    ctx.indent();
    ctx.line("if (boundary <= 0) return;");
    ctx.line("int rem = bitPos % 8;");
    ctx.line("if (rem != 0) bitPos += (8 - rem);");
    ctx.line("int bytePos = sizeBytes();");
    ctx.line("int bRem = bytePos % boundary;");
    ctx.line("if (bRem != 0) { for (int i = 0; i < boundary - bRem; i++) writeU8(0); }");
    ctx.dedent();
    ctx.line("}");
    ctx.dedent();
    ctx.line("}");
    return ctx.str();
}

std::string generate_j_exception(const std::string& pkg) {
    EmitContext ctx;
    ctx.line("// Generated by bgen - DO NOT EDIT");
    ctx.line("package " + pkg + ".codec;");
    ctx.line();
    ctx.line("public class ConduitCodecException extends RuntimeException {");
    ctx.indent();
    ctx.line("private static final long serialVersionUID = 1L;");
    ctx.line("public ConduitCodecException(String msg) { super(msg); }");
    ctx.dedent();
    ctx.line("}");
    return ctx.str();
}

// ============================================================================
// Constants.java
// ============================================================================

std::string generate_j_constants(const model::Protocol& protocol, const std::string& pkg) {
    EmitContext ctx;
    ctx.line("// Generated by bgen - DO NOT EDIT");
    ctx.line("package " + pkg + ";");
    ctx.line();
    ctx.line("public final class Constants {");
    ctx.indent();
    ctx.line("private Constants() {}");
    for (const auto& c : protocol.constants) {
        ctx.line("public static final long " + j_const(c.name) + " = " + c.value + ";");
    }
    ctx.dedent();
    ctx.line("}");
    return ctx.str();
}

// ============================================================================
// Struct/Message Java class generation
// ============================================================================

struct JFieldDef {
    std::string name;
    std::string j_type;
    std::string init;
    model::DisplayFormat format = model::DisplayFormat::Decimal;
    bool is_numeric = false;  // true for simple int/long fields (not struct/enum/string/bytes)
    // Constraint & accessor info (matching C++ emit_plain_accessors / emit_setter_constraint_checks)
    const model::Constraint* constraint = nullptr;
    bool is_signed = false;
    bool is_optional = false;        // FX / bitmap / present-when (nullable)
    bool is_enum = false;
    bool is_struct = false;
    bool is_string = false;
    bool is_bytes = false;
    bool is_bool = false;
    bool is_type_wrapper = false; // TypeDef wrapper (string/scaled/flags/constrained) vs real struct
    bool is_string_wrapper = false; // String type wrapper (AsciiStr etc)
    bool is_scaled_wrapper = false; // Scaled type wrapper (ScaledTemp etc)
    bool is_list_of_type_wrappers = false; // List<TypeWrapper> elements
    bool is_list_of_string_wrappers = false;
    bool is_list_of_scaled_wrappers = false;
    std::optional<int> max_length;
    // Scaled field raw accessors
    bool has_scale = false;
    double scale = 1.0;
    double offset = 0.0;
    int raw_bits = 0;
    bool raw_signed = false;
    // Source BMDL name for accessor naming (PascalCase)
    std::string bmdl_name;
    // Documentation string from <doc> tag
    std::string doc;
};

// Helper: return Java encoding constant string for a field (0=ASCII, 1=IA5, 2=EBCDIC)
std::string j_encoding_const(const model::Field& f) {
    if (f.encoding) {
        switch (*f.encoding) {
            case model::StringEncoding::Ia5: return "1";
            case model::StringEncoding::Ebcdic: return "2";
            default: break;
        }
    }
    return "";
}

bool j_field_needs_encoding(const model::Field& f) {
    return f.encoding && (*f.encoding == model::StringEncoding::Ia5 || *f.encoding == model::StringEncoding::Ebcdic);
}

// Helper: emit Java string trim code based on field trim mode
static model::StringPadding j_resolve_effective_padding(const model::Field& f,
                                                        const analyzer::TypeIndex& index) {
    if (f.padding) return *f.padding;
    if (!f.type_ref.empty()) {
        auto it = index.types.find(f.type_ref);
        if (it != index.types.end()) return it->second->padding;
    }
    return model::StringPadding::Null;
}

void emit_j_field_trim(EmitContext& ctx, const std::string& m, const model::Field& f,
                       const analyzer::TypeIndex& index) {
    // Only trim when explicitly set (matching C++ behavior)
    if (!f.trim) return;
    auto eff_trim = *f.trim;
    auto padding = j_resolve_effective_padding(f, index);
    std::string ch = (padding == model::StringPadding::Space) ? "\" \"" : "\"\\0\"";
    switch (eff_trim) {
        case model::StringTrim::Right:
            ctx.line("while (" + m + ".endsWith(" + ch + ")) " + m + " = " + m + ".substring(0, " + m + ".length()-1);");
            break;
        case model::StringTrim::Left:
            ctx.line("while (" + m + ".startsWith(" + ch + ")) " + m + " = " + m + ".substring(1);");
            break;
        case model::StringTrim::Both:
            ctx.line("while (" + m + ".endsWith(" + ch + ")) " + m + " = " + m + ".substring(0, " + m + ".length()-1);");
            ctx.line("while (" + m + ".startsWith(" + ch + ")) " + m + " = " + m + ".substring(1);");
            break;
        case model::StringTrim::None:
            break;
    }
}

void collect_j_fields(const std::vector<model::StructChild>& children,
                      const analyzer::TypeIndex& index,
                      std::vector<JFieldDef>& fields,
                      const JInlineNameMap& name_map = {},
                      const std::string& parent_class_name = {},
                      bool in_fx = false) {
    for (const auto& child : children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            // Inline enum field: enum_values populated, type_ref empty
            if (!f->enum_values.empty() && f->type_ref.empty() && !parent_class_name.empty()) {
                std::string enum_name = parent_class_name + j_class(f->name);
                JFieldDef ef;
                ef.name = j_field(f->name);
                ef.j_type = enum_name;
                ef.init = "null";
                ef.bmdl_name = f->name;
                ef.is_enum = true;
                ef.doc = f->doc;
                fields.push_back(ef);
                continue;
            }
            auto fi = j_resolve_field(*f, index);
            JFieldDef jf;
            jf.name = j_field(f->name);
            jf.j_type = fi.j_type;
            jf.format = f->format;
            jf.is_numeric = !fi.is_struct && !fi.is_enum && !fi.is_string && !fi.is_bytes && !fi.is_bool &&
                            !fi.is_float && !fi.has_scale && (fi.bits > 0);
            // FX or bitmap-controlled fields are optional (nullable)
            bool optional = in_fx || f->present_when != nullptr || f->bit.has_value();
            if (optional) {
                // Use boxed types for primitives inside FX/bitmap blocks
                if (fi.j_type == "int") jf.j_type = "Integer";
                else if (fi.j_type == "long") jf.j_type = "Long";
                else if (fi.j_type == "float") jf.j_type = "Float";
                else if (fi.j_type == "double") jf.j_type = "Double";
                else if (fi.j_type == "boolean") jf.j_type = "Boolean";
                jf.init = "null";
            } else if (fi.is_string) jf.init = "\"\"";
            else if (fi.is_bytes) jf.init = "new byte[0]";
            else if (fi.is_bool) jf.init = "false";
            else if (fi.j_type == "float") jf.init = "0.0f";
            else if (fi.j_type == "double") jf.init = "0.0";
            else if (fi.is_enum) {
                // Non-optional enum: use first enum value as default
                auto it = index.types.find(f->type_ref);
                if (it != index.types.end() && !it->second->enum_values.empty()) {
                    jf.init = fi.j_type + "." + j_const(it->second->enum_values[0].name);
                } else {
                    jf.init = "null";
                }
            }
            else if (fi.is_struct) jf.init = "new " + jf.j_type + "()";
            else if (fi.j_type == "long") jf.init = "0L";
            else jf.init = "0";
            // Apply explicit default value from BMDL spec (matching C++/Python)
            if (f->default_value) {
                if (fi.is_enum) {
                    // Enum defaults: qualify with enum type name and UPPER_CASE value
                    jf.init = fi.j_type + "." + j_const(*f->default_value);
                } else {
                    jf.init = j_qualify_const(*f->default_value, fi.j_type == "long" || jf.j_type == "Long");
                }
            }
            // constraint equals="X" implies default="X" (matching C++ behavior)
            if (!f->default_value && f->constraint && f->constraint->equals) {
                if (fi.is_enum)
                    jf.init = fi.j_type + "." + j_const(*f->constraint->equals);
                else
                    jf.init = j_qualify_const(*f->constraint->equals, fi.j_type == "long" || jf.j_type == "Long");
            }
            // Populate constraint & accessor metadata (matching C++ collect_fields)
            jf.bmdl_name = f->name;
            jf.is_optional = optional;
            jf.is_signed = fi.is_signed;
            jf.is_enum = fi.is_enum;
            jf.is_struct = fi.is_struct;
            jf.is_type_wrapper = fi.is_type_wrapper;
            jf.is_string_wrapper = fi.is_string_wrapper;
            jf.is_scaled_wrapper = fi.is_scaled_wrapper;
            jf.is_string = fi.is_string;
            jf.is_bytes = fi.is_bytes;
            jf.is_bool = fi.is_bool;
            jf.max_length = f->max_length;
            if (f->constraint) jf.constraint = &*f->constraint;
            // Scaled field raw accessor info
            jf.has_scale = fi.has_scale;
            jf.scale = fi.scale;
            jf.offset = fi.offset;
            jf.raw_bits = fi.raw_bits;
            jf.raw_signed = fi.raw_signed;
            jf.doc = f->doc;
            fields.push_back(jf);
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            JFieldDef sdf;
            sdf.name = j_field(sd->name);
            sdf.j_type = j_inline_class(sd->name, name_map);
            sdf.bmdl_name = sd->name;
            sdf.is_struct = true;
            sdf.is_optional = in_fx || sd->present_when != nullptr || sd->bit.has_value();
            sdf.init = sdf.is_optional ? "null" : "new " + sdf.j_type + "()";
            sdf.doc = sd->doc;
            fields.push_back(sdf);
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            std::string elem = ad->type_ref.empty() ? j_inline_class(ad->name, name_map) : j_class(ad->type_ref);
            JFieldDef adf;
            adf.name = j_field(ad->name);
            adf.bmdl_name = ad->name;
            adf.is_optional = in_fx || ad->present_when != nullptr || ad->bit.has_value();
            // Check if array element type is a type wrapper
            if (!ad->type_ref.empty()) {
                auto resolved = index.find(ad->type_ref);
                if (resolved) {
                    std::visit([&adf](const auto* def) {
                        using T = std::decay_t<decltype(*def)>;
                        if constexpr (std::is_same_v<T, model::TypeDef>) {
                            adf.is_list_of_type_wrappers = true;
                            adf.is_list_of_string_wrappers = (def->base == model::PrimitiveBase::String);
                            adf.is_list_of_scaled_wrappers = def->scale.has_value() || def->offset.has_value();
                        }
                    }, *resolved);
                }
            }
            if (in_fx) {
                adf.j_type = "java.util.List<" + elem + ">";
                adf.init = "null";
            } else {
                adf.j_type = "java.util.List<" + elem + ">";
                adf.init = "new java.util.ArrayList<>()";
            }
            fields.push_back(adf);
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            JFieldDef cdf;
            cdf.name = j_field(cd->name);
            cdf.j_type = "Object";
            cdf.init = "null";
            cdf.bmdl_name = cd->name;
            cdf.is_optional = true;
            fields.push_back(cdf);
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            collect_j_fields(fx->children, index, fields, name_map, parent_class_name, true);
        }
    }
}

// Collect the set of enum field names from struct children (for expression codegen)
std::set<std::string> j_collect_enum_fields(const std::vector<model::StructChild>& children,
                                             const analyzer::TypeIndex& index) {
    std::set<std::string> result;
    for (const auto& child : children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto cfi = resolve_field_type(*f, index);
            if (cfi.is_enum) result.insert(f->name);
        }
    }
    return result;
}

// Forward declarations for mutual recursion with inline struct handling
void emit_j_decode_children(EmitContext& ctx, const std::vector<model::StructChild>& children,
                             const analyzer::TypeIndex& index, const std::string& pfx,
                             JBitTracker& tracker,
                             const JOuterScopeMap& scope_map = {},
                             const JOuterContext& outer_ctx = {},
                             const JInlineNameMap& name_map = {},
                             const std::string& parent_class_name = {});
void emit_j_encode_children(EmitContext& ctx, const std::vector<model::StructChild>& children,
                             const analyzer::TypeIndex& index, const std::string& pfx,
                             JBitTracker& tracker,
                             const std::string& len_ref_target = "",
                             const model::Field* auto_len_ref_field = nullptr,
                             const JInlineNameMap& name_map = {},
                             const std::string& parent_class_name = {},
                             const JOuterContext& outer_ctx = {});

// Emit Java decode for field
void emit_j_field_decode(EmitContext& ctx, const model::Field& f,
                          const analyzer::TypeIndex& index, const std::string& pfx,
                          JBitTracker& tracker,
                          const JOuterContext& outer_ctx = {},
                          const std::string& parent_class_name = {}) {
    // Inline enum field: enum_values populated, type_ref empty
    if (!f.enum_values.empty() && f.type_ref.empty() && !parent_class_name.empty()) {
        std::string enum_name = parent_class_name + j_class(f.name);
        std::string m = pfx + "." + j_field(f.name);
        ctx.line(m + " = " + enum_name + ".decode(r);");
        if (f.bits) tracker.advance_bits(*f.bits);
        else tracker.advance_bits_variable();
        return;
    }
    auto fi = j_resolve_field(f, index);
    std::string m = pfx + "." + j_field(f.name);
    // Inline struct: decode children directly into parent (flattened)
    if (f.is_inline && !f.type_ref.empty()) {
        auto sit = index.structs.find(f.type_ref);
        if (sit != index.structs.end()) {
            emit_j_decode_children(ctx, sit->second->children, index, pfx, tracker);
            return;
        }
        auto mit = index.messages.find(f.type_ref);
        if (mit != index.messages.end()) {
            emit_j_decode_children(ctx, mit->second->children, index, pfx, tracker);
            return;
        }
    }
    if (fi.is_struct || fi.is_enum) { ctx.line(m + " = " + fi.j_type + ".decode(r);"); tracker.advance_field(fi); return; }
    if (fi.is_string) {
        bool has_enc = j_field_needs_encoding(f);
        std::string enc_arg = has_enc ? (", " + j_encoding_const(f)) : "";
        std::string read_fn = has_enc ? "readStringEncoded" : "readString";
        if (f.char_bits && f.length) {
            // Packed character decode (e.g., ICAO 6-bit chars)
            ctx.line(m + " = r.readPackedChars(" + std::to_string(*f.length) + ", " + std::to_string(*f.char_bits) + ");");
            emit_j_field_trim(ctx, m, f, index);
        } else if (f.terminated) {
            // Terminated string decode
            int max_len = f.max_length ? *f.max_length : 65535;
            if (*f.terminated == "crlf") {
                ctx.line(m + " = r.readCrlfTerminatedString(" + std::to_string(max_len) + ");");
            } else {
                // Parse hex terminator like "0x00", or named terminators
                std::string term = "0";
                if (*f.terminated == "newline") {
                    term = "0x0A";
                } else if (f.terminated->size() > 2 && f.terminated->substr(0, 2) == "0x") {
                    term = *f.terminated;
                }
                ctx.line(m + " = r.readTerminatedString(" + term + ", " + std::to_string(max_len) + ");");
            }
            emit_j_field_trim(ctx, m, f, index);
        } else if (f.length) {
            ctx.line(m + " = r." + read_fn + "(" + std::to_string(*f.length) + enc_arg + ");");
            emit_j_field_trim(ctx, m, f, index);
        } else if (f.length_from) {
            ctx.line(m + " = r." + read_fn + "(" + j_expr_ctx(*f.length_from, pfx, outer_ctx) + enc_arg + ");");
            emit_j_field_trim(ctx, m, f, index);
        } else if (f.length_prefix) {
            auto pti = resolve_prefix_type(*f.length_prefix, index);
            bool pbe = (pti.endian == model::Endian::Big);
            std::string rd;
            if (pti.bits <= 8) rd = "r.readU8()";
            else if (pti.bits <= 16) rd = std::string("r.readU16(") + (pbe ? "true" : "false") + ")";
            else rd = std::string("r.readU32(") + (pbe ? "true" : "false") + ")";
            ctx.line("int _pl = " + rd + ";");
            if (f.length_includes_prefix)
                ctx.line("_pl -= " + std::to_string(get_prefix_bytes(pti)) + ";");
            ctx.line(m + " = r." + read_fn + "(_pl" + enc_arg + ");");
            emit_j_field_trim(ctx, m, f, index);
        } else if (f.length_star) {
            ctx.line(m + " = r." + read_fn + "(r.remainingBytes()" + enc_arg + ");");
            emit_j_field_trim(ctx, m, f, index);
        } else {
            ctx.line(m + " = r." + read_fn + "(r.remainingBytes()" + enc_arg + ");");
            emit_j_field_trim(ctx, m, f, index);
        }
        // max_length validation (matching C++ MaxLengthExceeded check)
        if (f.max_length) {
            ctx.line("if (" + m + ".length() > " + std::to_string(*f.max_length) + ") throw new ConduitCodecException(\"" + f.name + " exceeds max length " + std::to_string(*f.max_length) + "\");");
        }
        tracker.advance_field(fi);
        return;
    }
    if (fi.is_bytes) {
        int len = f.length ? *f.length : (f.bytes_attr ? *f.bytes_attr : 0);
        if (len > 0) ctx.line(m + " = r.readBytes(" + std::to_string(len) + ");");
        else if (f.length_from) ctx.line(m + " = r.readBytes(" + j_expr_ctx(*f.length_from, pfx, outer_ctx) + ");");
        else ctx.line(m + " = r.readBytes(r.remainingBytes());");
        if (f.max_length) {
            ctx.line("if (" + m + ".length > " + std::to_string(*f.max_length) + ") throw new ConduitCodecException(\"" + f.name + " exceeds max length " + std::to_string(*f.max_length) + "\");");
        }
        tracker.advance_field(fi);
        return;
    }
    if (fi.has_scale) {
        JFieldInfo raw_fi = fi; raw_fi.bits = fi.raw_bits; raw_fi.is_signed = fi.raw_signed;
        raw_fi.is_float = false; raw_fi.has_scale = false;
        std::string ve = j_read_expr(raw_fi, tracker.is_byte_aligned());
        std::string expr = ve;
        if (fi.scale != 1.0) expr += " * " + j_double(fi.scale);
        if (fi.offset != 0.0) expr += " + " + j_double(fi.offset);
        ctx.line(m + " = " + expr + ";");
        tracker.advance_field(fi);
        return;
    }
    if (fi.is_bool) { ctx.line(m + " = (" + j_read_expr(fi, tracker.is_byte_aligned()) + " != 0);"); tracker.advance_field(fi); return; }
    ctx.line(m + " = " + (j_needs_int_cast(fi, tracker.is_byte_aligned()) ? "(int) " : "") + j_read_expr(fi, tracker.is_byte_aligned()) + ";");
    // Field-level constraint checks (matching C++ emit_constraint_check)
    // Skip deferred constraints (validated externally, not at decode time)
    if (f.constraint && f.constraint->validate != model::ValidateTiming::Deferred) {
        if (f.constraint->equals) {
            ctx.line("if (" + m + " != " + j_qualify_const(*f.constraint->equals) + ") throw new ConduitCodecException(\"" + f.name + " constraint violation: expected " + *f.constraint->equals + "\");");
        }
        if (f.constraint->max) {
            ctx.line("if (" + m + " > " + j_qualify_const(*f.constraint->max) + ") throw new ConduitCodecException(\"" + f.name + " exceeds max " + *f.constraint->max + "\");");
        }
        bool is_signed = fi.is_signed;
        if (f.constraint->min && (*f.constraint->min != "0" || is_signed)) {
            ctx.line("if (" + m + " < " + j_qualify_const(*f.constraint->min) + ") throw new ConduitCodecException(\"" + f.name + " below min " + *f.constraint->min + "\");");
        }
    }
    tracker.advance_field(fi);
}

// Emit Java encode for field
void emit_j_field_encode(EmitContext& ctx, const model::Field& f,
                          const analyzer::TypeIndex& index, const std::string& pfx,
                          JBitTracker& tracker,
                          const std::string& parent_class_name = {},
                          const JOuterContext& outer_ctx = {}) {
    (void)outer_ctx;
    // Inline enum field: enum_values populated, type_ref empty
    if (!f.enum_values.empty() && f.type_ref.empty() && !parent_class_name.empty()) {
        std::string m = pfx + "." + j_field(f.name);
        ctx.line(m + ".encode(w);");
        if (f.bits) tracker.advance_bits(*f.bits);
        else tracker.advance_bits_variable();
        return;
    }
    auto fi = j_resolve_field(f, index);
    std::string m = pfx + "." + j_field(f.name);
    // Inline struct: encode children directly from parent (flattened)
    if (f.is_inline && !f.type_ref.empty()) {
        auto sit = index.structs.find(f.type_ref);
        if (sit != index.structs.end()) {
            std::string len_ref; // no auto-length ref target for inline children
            emit_j_encode_children(ctx, sit->second->children, index, pfx, tracker, len_ref);
            return;
        }
        auto mit = index.messages.find(f.type_ref);
        if (mit != index.messages.end()) {
            std::string len_ref;
            emit_j_encode_children(ctx, mit->second->children, index, pfx, tracker, len_ref);
            return;
        }
    }
    if (f.auto_expr && f.auto_expr->kind == model::AutoKind::Length) {
        if (f.auto_expr->field_ref.empty()) {
            // auto="length" (whole struct): record start pos, write placeholder
            ctx.line("_lenPos = w.sizeBytes();");
        } else {
            // auto="length(field)": record position for field-specific backpatch
            ctx.line("_lenRefPos = w.sizeBytes();");
        }
        ctx.line(j_write_stmt("0", fi, tracker.is_byte_aligned()) + ";");
        tracker.advance_field(fi);
        return;
    }
    if (f.auto_expr && f.auto_expr->kind == model::AutoKind::Count) {
        // auto="count(field)": write the size of the referenced array
        std::string array_member = pfx + "." + j_field(f.auto_expr->field_ref);
        ctx.line(j_write_stmt(array_member + ".size()", fi, tracker.is_byte_aligned()) + ";");
        tracker.advance_field(fi);
        return;
    }
    if (f.auto_expr && f.auto_expr->kind == model::AutoKind::Id) {
        // auto="id": write the message's ID_VALUE constant
        ctx.line(j_write_stmt("ID_VALUE", fi, tracker.is_byte_aligned()) + ";");
        tracker.advance_field(fi);
        return;
    }
    // Encode-time constraint checks (matching C++ emit_encode_constraint_check)
    // Skip for struct, enum, bytes fields — constraints don't apply to those at encode time
    // Skip deferred constraints (validated externally, not at encode time)
    if (f.constraint && f.constraint->validate != model::ValidateTiming::Deferred
        && !fi.is_struct && !fi.is_enum && !fi.is_bytes) {
        if (f.constraint->equals) {
            ctx.line("if (" + m + " != " + j_qualify_const(*f.constraint->equals) + ") throw new ConduitCodecException(\"" + f.name + " constraint: expected " + *f.constraint->equals + "\");");
        }
        if (f.constraint->max) {
            ctx.line("if (" + m + " > " + j_qualify_const(*f.constraint->max) + ") throw new ConduitCodecException(\"" + f.name + " exceeds max " + *f.constraint->max + "\");");
        }
        if (f.constraint->min && (*f.constraint->min != "0" || fi.is_signed)) {
            ctx.line("if (" + m + " < " + j_qualify_const(*f.constraint->min) + ") throw new ConduitCodecException(\"" + f.name + " below min " + *f.constraint->min + "\");");
        }
    }
    if (fi.is_struct || fi.is_enum) { ctx.line(m + ".encode(w);"); tracker.advance_field(fi); return; }
    if (fi.is_string) {
        bool has_enc = j_field_needs_encoding(f);
        std::string enc_arg = has_enc ? (", " + j_encoding_const(f)) : "";
        std::string write_fn = has_enc ? "writeStringEncoded" : "writeString";
        if (f.char_bits && f.length) {
            // Packed character encode
            ctx.line("w.writePackedChars(" + m + ", " + std::to_string(*f.length) + ", " + std::to_string(*f.char_bits) + ");");
        } else if (f.terminated) {
            // Terminated string encode
            if (*f.terminated == "crlf") {
                ctx.line("w.writeCrlfTerminatedString(" + m + ");");
            } else {
                std::string term = "0";
                if (*f.terminated == "newline") {
                    term = "0x0A";
                } else if (f.terminated->size() > 2 && f.terminated->substr(0, 2) == "0x") {
                    term = *f.terminated;
                }
                ctx.line("w.writeTerminatedString(" + m + ", " + term + ");");
            }
        } else if (f.length) {
            // Determine padding: field-level first, then fallback to type-level
            int pad = 0;
            if (f.padding && *f.padding == model::StringPadding::Space) {
                // Check for EBCDIC encoding (EBCDIC space is 0x40)
                bool is_ebcdic = (f.encoding && *f.encoding == model::StringEncoding::Ebcdic);
                if (!is_ebcdic && !f.type_ref.empty()) {
                    auto it = index.types.find(f.type_ref);
                    if (it != index.types.end() && it->second->encoding == model::StringEncoding::Ebcdic)
                        is_ebcdic = true;
                }
                pad = is_ebcdic ? 0x40 : 0x20;
            } else if (!f.padding && !f.type_ref.empty()) {
                auto it = index.types.find(f.type_ref);
                if (it != index.types.end() && it->second->padding == model::StringPadding::Space) {
                    bool is_ebcdic = (it->second->encoding == model::StringEncoding::Ebcdic);
                    pad = is_ebcdic ? 0x40 : 0x20;
                }
            }
            ctx.line("w." + write_fn + "(" + m + ", " + std::to_string(*f.length) + ", " + std::to_string(pad) + enc_arg + ");");
        } else if (f.length_prefix) {
            auto pti = resolve_prefix_type(*f.length_prefix, index);
            bool pbe = (pti.endian == model::Endian::Big);
            std::string le = m + ".length()";
            if (f.length_includes_prefix) le += " + " + std::to_string(get_prefix_bytes(pti));
            if (pti.bits <= 8) ctx.line("w.writeU8(" + le + ");");
            else if (pti.bits <= 16) ctx.line(std::string("w.writeU16(") + le + ", " + (pbe ? "true" : "false") + ");");
            else ctx.line(std::string("w.writeU32(") + le + ", " + (pbe ? "true" : "false") + ");");
            ctx.line("w." + write_fn + "(" + m + ", " + m + ".length(), 0" + enc_arg + ");");
        } else {
            ctx.line("w." + write_fn + "(" + m + ", " + m + ".length(), 0" + enc_arg + ");");
        }
        tracker.advance_field(fi);
        return;
    }
    if (fi.is_bytes) { ctx.line("w.writeBytes(" + m + ");"); tracker.advance_field(fi); return; }
    if (fi.has_scale) {
        std::string inv = m;
        if (fi.offset != 0.0) inv = "(" + inv + " - " + j_double(fi.offset) + ")";
        if (fi.scale != 1.0) inv = "(" + inv + " / " + j_double(fi.scale) + ")";
        JFieldInfo raw_fi = fi; raw_fi.bits = fi.raw_bits; raw_fi.is_signed = fi.raw_signed;
        raw_fi.is_float = false; raw_fi.has_scale = false;
        // writeU8/writeU16/writeU32 take int, writeBits/writeSignedBits take long
        bool use_int = !raw_fi.is_signed &&
            (raw_fi.bits == 8 || raw_fi.bits == 16 || raw_fi.bits == 32);
        std::string cast = use_int ? "(int)" : "(long)";
        ctx.line(j_write_stmt(cast + "(" + inv + ")", raw_fi, tracker.is_byte_aligned()) + ";");
        tracker.advance_field(fi);
        return;
    }
    ctx.line(j_write_stmt(m, fi, tracker.is_byte_aligned()) + ";");
    tracker.advance_field(fi);
}

void emit_j_decode_children(EmitContext& ctx, const std::vector<model::StructChild>& children,
                             const analyzer::TypeIndex& index, const std::string& pfx,
                             JBitTracker& tracker,
                             const JOuterScopeMap& scope_map,
                             const JOuterContext& outer_ctx,
                             const JInlineNameMap& name_map,
                             const std::string& parent_class_name) {
    // Collect enum field names for expression codegen
    auto enum_fields = j_collect_enum_fields(children, index);
    const std::set<std::string>* ef_ptr = enum_fields.empty() ? nullptr : &enum_fields;

    // Pre-scan: find struct-level auto-length field (auto="length" with no field_ref).
    // When present, we create a bounded subReader after reading the length field
    // so that subsequent children cannot over-read past the struct boundary.
    int auto_length_idx = -1;
    model::ArithModifier auto_length_mod;
    std::string auto_length_field_name;
    for (size_t i = 0; i < children.size(); ++i) {
        if (auto* f = std::get_if<model::Field>(&children[i])) {
            if (f->auto_expr && f->auto_expr->kind == model::AutoKind::Length
                && f->auto_expr->field_ref.empty()) {
                auto_length_idx = static_cast<int>(i);
                auto_length_mod = f->auto_expr->modifier;
                auto_length_field_name = f->name;
                break;
            }
        }
    }

    // Emit start position marker if auto-length is present
    if (auto_length_idx >= 0 && auto_length_idx + 1 < static_cast<int>(children.size())) {
        ctx.line("int _autoLenStart = r.remainingBytes();");
    }

    for (size_t idx = 0; idx < children.size(); ++idx) {
        const auto& child = children[idx];
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto emit_field_decode_wrapped = [&]() {
                ctx.line("try {");
                ctx.indent();
                emit_j_field_decode(ctx, *f, index, pfx, tracker, outer_ctx, parent_class_name);
                ctx.dedent();
                ctx.line("} catch (ConduitCodecException _e) { throw new ConduitCodecException(\"field '" + f->name + "': \" + _e.getMessage()); }");
            };
            if (f->present_when) {
                ctx.line("if (" + j_expr_ctx(*f->present_when, pfx, outer_ctx, ef_ptr) + ") {");
                ctx.indent(); emit_field_decode_wrapped(); ctx.dedent(); ctx.line("}");
            } else emit_field_decode_wrapped();
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            std::string resolved = j_inline_class(sd->name, name_map);
            std::string args = j_build_outer_args(sd->name, scope_map, pfx, outer_ctx);
            std::string decode_line = pfx + "." + j_field(sd->name) + " = " + resolved + ".decode(r" + args + ");";
            if (sd->present_when) {
                ctx.line("if (" + j_expr_ctx(*sd->present_when, pfx, outer_ctx, ef_ptr) + ") {");
                ctx.indent(); ctx.line(decode_line); ctx.dedent(); ctx.line("}");
            } else {
                ctx.line(decode_line);
            }
            tracker.advance_bits_variable();
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            std::string m = pfx + "." + j_field(ad->name);
            std::string elem = ad->type_ref.empty() ? j_inline_class(ad->name, name_map) : j_class(ad->type_ref);
            auto emit_array_decode = [&]() {
                // Initialize null list (e.g. nullable array in FX block) before adding
                ctx.line("if (" + m + " == null) " + m + " = new java.util.ArrayList<>();");
                if (ad->fixed_count) {
                    ctx.line("for (int _i=0; _i<" + std::to_string(*ad->fixed_count) + "; _i++) " + m + ".add(" + elem + ".decode(r));");
                } else if (ad->count_from) {
                    ctx.line("for (int _i=0; _i<" + j_expr_ctx(*ad->count_from, pfx, outer_ctx, ef_ptr) + "; _i++) " + m + ".add(" + elem + ".decode(r));");
                } else if (ad->length_from) {
                    // Bounded array: create sub-reader limited to length_from bytes
                    ctx.line("{ BitReader _ar = r.subReader((int)(" + j_expr_ctx(*ad->length_from, pfx, outer_ctx, ef_ptr) + "));");
                    ctx.line("  while (_ar.remainingBytes() > 0) " + m + ".add(" + elem + ".decode(_ar));");
                    ctx.line("}");
                } else {
                    ctx.line("while (r.remainingBytes() > 0) " + m + ".add(" + elem + ".decode(r));");
                }
            };
            if (ad->present_when) {
                ctx.line("if (" + j_expr_ctx(*ad->present_when, pfx, outer_ctx, ef_ptr) + ") {");
                ctx.indent(); emit_array_decode(); ctx.dedent(); ctx.line("}");
            } else {
                emit_array_decode();
            }
            tracker.advance_bits_variable();
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            if (!cd->switch_expr) continue;
            std::string sv = j_expr_ctx(*cd->switch_expr, pfx, outer_ctx, ef_ptr);
            std::string m = pfx + "." + j_field(cd->name);

            // Check if the switch expression field is an enum type.
            // If so, we need to use .value for numeric comparisons with constants.
            bool switch_is_enum = false;
            if (cd->switch_expr->op == model::ExprOp::FieldRef) {
                for (const auto& sib : children) {
                    if (auto* sf = std::get_if<model::Field>(&sib)) {
                        if (sf->name == cd->switch_expr->name) {
                            auto sfi = j_resolve_field(*sf, index);
                            switch_is_enum = sfi.is_enum;
                            break;
                        }
                    }
                }
            }
            // For enum switch fields, use .value to get the numeric value
            std::string sv_cmp = switch_is_enum ? sv + ".value" : sv;

            // Create bounded sub-reader if choice has length/length_from
            bool bounded = cd->length_from != nullptr || cd->length.has_value();
            std::string reader_var = "r";
            if (bounded) {
                ctx.line("{");
                ctx.indent();
                if (cd->length_from) {
                    ctx.line("BitReader cr = r.subReader(" + j_expr_ctx(*cd->length_from, pfx, outer_ctx, ef_ptr) + ");");
                } else if (cd->length) {
                    ctx.line("BitReader cr = r.subReader(" + std::to_string(*cd->length) + ");");
                }
                reader_var = "cr";
            }

            auto emit_choice_decode = [&]() {
                // Collect receive/both values and ranges for send-only dedup
                std::set<std::string> recv_values;
                std::set<std::string> recv_ranges;
                for (const auto& cs : cd->cases) {
                    if (cs.direction == model::Direction::Send) continue;
                    if (cs.value) recv_values.insert(*cs.value);
                    if (cs.range) recv_ranges.insert(*cs.range);
                }

                bool first = true;
                for (const auto& cs : cd->cases) {
                    // Skip send-only cases when a receive/both case exists for same value/range
                    if (cs.direction == model::Direction::Send && cs.value) {
                        if (recv_values.count(*cs.value)) continue;
                    }
                    if (cs.direction == model::Direction::Send && cs.range) {
                        if (recv_ranges.count(*cs.range)) continue;
                    }

                    std::string cond;
                    if (cs.value) {
                        std::string val = *cs.value;
                        if (index.constants.count(*cs.value)) {
                            val = "Constants." + j_const(*cs.value);
                        }
                        cond = sv_cmp + " == " + val;
                    } else if (cs.range) {
                        auto dot_pos = cs.range->find("..");
                        if (dot_pos != std::string::npos) {
                            std::string min_s = cs.range->substr(0, dot_pos);
                            std::string max_s = cs.range->substr(dot_pos + 2);
                            if (index.constants.count(min_s)) min_s = "Constants." + j_const(min_s);
                            if (index.constants.count(max_s)) max_s = "Constants." + j_const(max_s);
                            if (min_s == "0") {
                                cond = sv_cmp + " <= " + max_s;
                            } else {
                                cond = sv_cmp + " >= " + min_s + " && " + sv_cmp + " <= " + max_s;
                            }
                        } else {
                            std::string range_val = *cs.range;
                            if (index.constants.count(range_val)) range_val = "Constants." + j_const(range_val);
                            cond = sv_cmp + " == " + range_val;
                        }
                    } else {
                        continue;
                    }
                    ctx.line(std::string(first ? "if (" : "} else if (") + cond + ") {");
                    ctx.indent();
                    std::string et = cs.type_ref.empty() ? j_inline_class(cs.name, name_map) : j_class(cs.type_ref);
                    std::string case_name = cs.type_ref.empty() ? cs.name : cs.type_ref;
                    std::string args = j_build_outer_args(case_name, scope_map, pfx, outer_ctx);
                    ctx.line(m + " = " + et + ".decode(" + reader_var + args + ");");
                    ctx.dedent();
                    first = false;
                }
                if (cd->otherwise) {
                    ctx.line("} else {");
                    ctx.indent();
                    std::string et = cd->otherwise->type_ref.empty() ? j_inline_class(cd->otherwise->name, name_map) : j_class(cd->otherwise->type_ref);
                    std::string ow_name = cd->otherwise->type_ref.empty() ? cd->otherwise->name : cd->otherwise->type_ref;
                    std::string args = j_build_outer_args(ow_name, scope_map, pfx, outer_ctx);
                    ctx.line(m + " = " + et + ".decode(" + reader_var + args + ");");
                    ctx.dedent();
                } else if (!first) {
                    ctx.line("} else {");
                    ctx.indent();
                    ctx.line("throw new ConduitCodecException(\"choice '" + cd->name + "': no case matched switch value\");");
                    ctx.dedent();
                }
                if (!first) ctx.line("}");
            };
            if (cd->present_when) {
                ctx.line("if (" + j_expr_ctx(*cd->present_when, pfx, outer_ctx, ef_ptr) + ") {");
                ctx.indent(); emit_choice_decode(); ctx.dedent(); ctx.line("}");
            } else {
                emit_choice_decode();
            }

            if (bounded) {
                ctx.dedent();
                ctx.line("}");
            }
            tracker.advance_bits_variable();
        } else if (auto* res = std::get_if<model::Reserved>(&child)) {
            ctx.line("r.skipBits(" + std::to_string(res->bits) + ");");
            tracker.advance_bits(res->bits);
        } else if (auto* al = std::get_if<model::Align>(&child)) {
            ctx.line("r.alignTo(" + std::to_string(al->to) + ");");
            tracker.bit_mod8 = 0; // alignment resets to byte-aligned
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            // FX extension: read continuation bit, conditionally decode children
            ctx.line("if (r.readBits(1) != 0) {");
            ctx.indent();
            emit_j_decode_children(ctx, fx->children, index, pfx, tracker, scope_map, outer_ctx, name_map, parent_class_name);
            // Write terminal FX=0 bit if this FX extent has no nested FxBlock
            bool has_nested_fx = false;
            for (const auto& fc : fx->children) {
                if (std::holds_alternative<model::FxBlock>(fc)) { has_nested_fx = true; break; }
            }
            if (!has_nested_fx) {
                ctx.line("r.skipBits(1); // Terminal FX=0");
            }
            ctx.dedent();
            ctx.line("}");
        }

        // After auto-length field, create bounded subReader for remaining children
        if (auto_length_idx >= 0 && static_cast<int>(idx) == auto_length_idx
            && idx + 1 < children.size()) {
            std::string len_member = pfx + "." + j_field(auto_length_field_name);
            // Compute remaining: total struct length minus bytes already consumed
            // auto="length" measures from struct start, so track consumed bytes at runtime
            std::string raw_len = len_member;
            if (auto_length_mod.has_modifier()) {
                std::string inv_op;
                switch (auto_length_mod.op) {
                    case model::ArithOp::Mul: inv_op = " / "; break;
                    case model::ArithOp::Div: inv_op = " * "; break;
                    case model::ArithOp::Add: inv_op = " - "; break;
                    case model::ArithOp::Sub: inv_op = " + "; break;
                    default: break;
                }
                if (!inv_op.empty()) {
                    raw_len = "(" + len_member + inv_op
                              + std::to_string(auto_length_mod.literal) + ")";
                }
            }
            std::string remaining = raw_len + " - (_autoLenStart - r.remainingBytes())";
            ctx.line("r = r.subReader(" + remaining + ");");
        }
    }
}

// Helper: emit code to check if any FX child fields have non-null values
void j_fx_has_fields_check(EmitContext& ctx, const std::vector<model::StructChild>& children,
                            const std::string& pfx, const std::string& flag_var) {
    for (const auto& child : children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            ctx.line("if (" + pfx + "." + j_field(f->name) + " != null) " + flag_var + " = true;");
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            if (!sd->name.empty()) {
                ctx.line("if (" + pfx + "." + j_field(sd->name) + " != null) " + flag_var + " = true;");
            }
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            ctx.line("if (" + pfx + "." + j_field(ad->name) + " != null) " + flag_var + " = true;");
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            ctx.line("if (" + pfx + "." + j_field(cd->name) + " != null) " + flag_var + " = true;");
        } else if (auto* nested_fx = std::get_if<model::FxBlock>(&child)) {
            j_fx_has_fields_check(ctx, nested_fx->children, pfx, flag_var);
        }
    }
}

// Encode children within an FX block: optional fields must write zero-fill when absent (null-safe)
void emit_j_encode_fx_children(EmitContext& ctx, const std::vector<model::StructChild>& children,
                               const analyzer::TypeIndex& index, const std::string& pfx,
                               const JInlineNameMap& name_map,
                               const std::string& parent_class_name,
                               int fx_depth = 0) {
    for (const auto& child : children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fi = j_resolve_field(*f, index);
            std::string m = pfx + "." + j_field(f->name);
            if (fi.is_enum || (!f->enum_values.empty() && f->type_ref.empty())) {
                // Enum: encode if present, else write zero bits
                ctx.line("if (" + m + " != null) { " + m + ".encode(w); }");
                ctx.line("else { " + j_write_stmt("0", fi) + "; }");
            } else if (fi.is_struct && !fi.is_string && !fi.is_bytes) {
                // Struct: encode if present, else default-construct and encode
                std::string stype = f->type_ref.empty() ? j_inline_class(f->name, name_map) : j_class(f->type_ref);
                ctx.line("if (" + m + " != null) { " + m + ".encode(w); }");
                ctx.line("else { new " + stype + "().encode(w); }");
            } else if (fi.is_string) {
                if (f->char_bits && f->length) {
                    // Packed character encode (e.g., ICAO 6-bit chars)
                    ctx.line("w.writePackedChars(" + m + " != null ? " + m + " : \"\", " +
                             std::to_string(*f->length) + ", " + std::to_string(*f->char_bits) + ");");
                } else if (f->length) {
                    int pad = 0;
                    if (f->padding && *f->padding == model::StringPadding::Space) {
                        bool is_ebcdic = (f->encoding && *f->encoding == model::StringEncoding::Ebcdic);
                        pad = is_ebcdic ? 0x40 : 0x20;
                    }
                    bool has_enc = j_field_needs_encoding(*f);
                    std::string enc_arg = has_enc ? ", " + j_encoding_const(*f) : "";
                    std::string write_fn = has_enc ? "writeStringEncoded" : "writeString";
                    ctx.line("w." + write_fn + "(" + m + " != null ? " + m + " : \"\", " +
                             std::to_string(*f->length) + ", " + std::to_string(pad) + enc_arg + ");");
                } else {
                    ctx.line("if (" + m + " != null) { w.writeString(" + m + ", " + m + ".length(), 0); }");
                }
            } else if (fi.is_bytes) {
                if (f->length) {
                    ctx.line("if (" + m + " != null) { w.writeBytes(" + m + "); }");
                    ctx.line("else { w.writeBits(0, " + std::to_string(*f->length * 8) + "); }");
                } else {
                    ctx.line("if (" + m + " != null) { w.writeBytes(" + m + "); }");
                }
            } else if (fi.has_scale) {
                // Scaled: use 0.0 when absent
                std::string val = "(" + m + " != null ? " + m + " : 0.0)";
                std::string inv = val;
                if (fi.offset != 0.0) inv = "(" + inv + " - " + j_double(fi.offset) + ")";
                if (fi.scale != 1.0) inv = "(" + inv + " / " + j_double(fi.scale) + ")";
                JFieldInfo raw_fi = fi; raw_fi.bits = fi.raw_bits; raw_fi.is_signed = fi.raw_signed;
                raw_fi.is_float = false; raw_fi.has_scale = false;
                ctx.line(j_write_stmt("(long)(" + inv + ")", raw_fi) + ";");
            } else {
                // Primitive: use (field != null ? field : 0)
                ctx.line(j_write_stmt("(" + m + " != null ? " + m + " : 0)", fi) + ";");
            }
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            std::string m = pfx + "." + j_field(sd->name);
            std::string stype = j_inline_class(sd->name, name_map);
            ctx.line("if (" + m + " != null) { " + m + ".encode(w); }");
            ctx.line("else { new " + stype + "().encode(w); }");
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            std::string m = pfx + "." + j_field(ad->name);
            if (ad->fixed_count) {
                // Fixed-count array: write elements if present, else write zero-filled defaults
                std::string elem = ad->type_ref.empty() ? j_inline_class(ad->name, name_map) : j_class(ad->type_ref);
                ctx.line("if (" + m + " != null) { for (var _item : " + m + ") _item.encode(w); }");
                ctx.line("else { for (int _i=0; _i<" + std::to_string(*ad->fixed_count) + "; _i++) new " + elem + "().encode(w); }");
            } else {
                ctx.line("if (" + m + " != null) { for (var _item : " + m + ") _item.encode(w); }");
            }
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            std::string m = pfx + "." + j_field(cd->name);
            // Use instanceof checks to cast Object to the correct type for encode()
            ctx.line("if (" + m + " != null) {");
            ctx.indent();
            bool first_case = true;
            for (const auto& cs : cd->cases) {
                std::string et = cs.type_ref.empty() ? j_inline_class(cs.name, name_map) : j_class(cs.type_ref);
                ctx.line(std::string(first_case ? "if" : "} else if") + " (" + m + " instanceof " + et + ") {");
                ctx.indent();
                ctx.line("((" + et + ") " + m + ").encode(w);");
                ctx.dedent();
                first_case = false;
            }
            if (cd->otherwise) {
                std::string ow_type = cd->otherwise->type_ref.empty()
                    ? j_inline_class(cd->otherwise->name, name_map)
                    : j_class(cd->otherwise->type_ref);
                ctx.line(std::string(first_case ? "if" : "} else if") + " (" + m + " instanceof " + ow_type + ") {");
                ctx.indent();
                ctx.line("((" + ow_type + ") " + m + ").encode(w);");
                ctx.dedent();
                first_case = false;
            }
            if (!first_case) ctx.line("}");
            ctx.dedent();
            ctx.line("}");
        } else if (auto* res = std::get_if<model::Reserved>(&child)) {
            ctx.line("w.writeBits(0, " + std::to_string(res->bits) + ");");
        } else if (auto* al = std::get_if<model::Align>(&child)) {
            ctx.line("w.alignTo(" + std::to_string(al->to) + ");");
        } else if (auto* nested_fx = std::get_if<model::FxBlock>(&child)) {
            int nd = fx_depth + 1;
            std::string fxvar = "_fxContinue" + std::to_string(nd);
            ctx.line("{");
            ctx.indent();
            ctx.line("boolean " + fxvar + " = false;");
            j_fx_has_fields_check(ctx, nested_fx->children, pfx, fxvar);
            ctx.line("w.writeBits(" + fxvar + " ? 1 : 0, 1);");
            ctx.line("if (" + fxvar + ") {");
            ctx.indent();
            emit_j_encode_fx_children(ctx, nested_fx->children, index, pfx, name_map, parent_class_name, nd);
            bool has_nested_fx = false;
            for (const auto& fc : nested_fx->children) {
                if (std::holds_alternative<model::FxBlock>(fc)) { has_nested_fx = true; break; }
            }
            if (!has_nested_fx) {
                ctx.line("w.writeBits(0, 1); // Terminal FX=0");
            }
            ctx.dedent();
            ctx.line("}");
            ctx.dedent();
            ctx.line("}");
        }
    }
}

void emit_j_encode_children(EmitContext& ctx, const std::vector<model::StructChild>& children,
                             const analyzer::TypeIndex& index, const std::string& pfx,
                             JBitTracker& tracker,
                             const std::string& len_ref_target,
                             const model::Field* auto_len_ref_field,
                             const JInlineNameMap& name_map,
                             const std::string& parent_class_name,
                             const JOuterContext& outer_ctx) {
    // Collect enum field names for expression codegen
    auto enum_fields = j_collect_enum_fields(children, index);
    const std::set<std::string>* ef_ptr = enum_fields.empty() ? nullptr : &enum_fields;

    // Get the BMDL name from a StructChild
    auto get_child_name = [](const model::StructChild& child) -> std::string {
        return std::visit([](const auto& c) -> std::string {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, model::Field> || std::is_same_v<T, model::StructDef> ||
                          std::is_same_v<T, model::ArrayDef> || std::is_same_v<T, model::ChoiceDef>)
                return c.name;
            else return {};
        }, child);
    };

    // Helper lambda: emit the auto-length(field) backpatch
    auto emit_length_ref_patch = [&]() {
        if (!auto_len_ref_field || !auto_len_ref_field->auto_expr) return;
        auto al_fi = j_resolve_field(*auto_len_ref_field, index);
        bool be = (al_fi.endian == model::Endian::Big);
        std::string target_field = j_field(auto_len_ref_field->auto_expr->field_ref);
        std::string size_expr = "w.sizeBytes() - _" + target_field + "Start";
        if (auto_len_ref_field->auto_expr->modifier.has_modifier()) {
            auto& mod = auto_len_ref_field->auto_expr->modifier;
            std::string op_str;
            switch (mod.op) {
                case model::ArithOp::Add: op_str = " + "; break;
                case model::ArithOp::Sub: op_str = " - "; break;
                case model::ArithOp::Mul: op_str = " * "; break;
                case model::ArithOp::Div: op_str = " / "; break;
                case model::ArithOp::Mod: op_str = " % "; break;
                default: break;
            }
            if (!op_str.empty()) {
                size_expr = "((" + size_expr + ")" + op_str +
                            std::to_string(mod.literal) + ")";
            }
        }
        if (al_fi.bits <= 8) {
            ctx.line("w.patchU8(_lenRefPos, " + size_expr + ");");
        } else if (al_fi.bits <= 16) {
            ctx.line("w.patchU16(_lenRefPos, " + size_expr + ", " + (be ? "true" : "false") + ");");
        } else {
            ctx.line("w.patchU32(_lenRefPos, " + size_expr + ", " + (be ? "true" : "false") + ");");
        }
    };

    bool is_target_active = false;
    for (const auto& child : children) {
        std::string child_name = get_child_name(child);

        // auto-length(field) start marker: record position before the target child
        if (!len_ref_target.empty() && child_name == len_ref_target) {
            ctx.line("int _" + j_field(len_ref_target) + "Start = w.sizeBytes();");
            is_target_active = true;
        } else if (is_target_active) {
            // The previous child was the target; emit the backpatch now
            emit_length_ref_patch();
            is_target_active = false;
        }

        if (auto* f = std::get_if<model::Field>(&child)) {
            auto emit_field_encode_wrapped = [&]() {
                ctx.line("try {");
                ctx.indent();
                emit_j_field_encode(ctx, *f, index, pfx, tracker, parent_class_name, outer_ctx);
                ctx.dedent();
                ctx.line("} catch (ConduitCodecException _e) { throw new ConduitCodecException(\"field '" + f->name + "': \" + _e.getMessage()); }");
            };
            if (f->present_when) {
                ctx.line("if (" + j_expr_ctx(*f->present_when, pfx, outer_ctx, ef_ptr) + ") {");
                ctx.indent(); emit_field_encode_wrapped(); ctx.dedent(); ctx.line("}");
            } else emit_field_encode_wrapped();
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            std::string m = pfx + "." + j_field(sd->name);
            if (sd->present_when) {
                ctx.line("if (" + m + " != null) " + m + ".encode(w);");
            } else {
                ctx.line(m + ".encode(w);");
            }
            tracker.advance_bits_variable();
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            std::string m = pfx + "." + j_field(ad->name);
            if (ad->present_when) {
                ctx.line("if (" + m + " != null) { for (var _item : " + m + ") _item.encode(w); }");
            } else {
                ctx.line("for (var _item : " + m + ") _item.encode(w);");
            }
            tracker.advance_bits_variable();
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            std::string m = pfx + "." + j_field(cd->name);
            auto emit_choice_encode = [&]() {
                bool first = true;
                for (const auto& cs : cd->cases) {
                    std::string et = cs.type_ref.empty() ? j_inline_class(cs.name, name_map) : j_class(cs.type_ref);
                    ctx.line(std::string(first ? "if" : "} else if") + " (" + m + " instanceof " + et + ") {");
                    ctx.indent();
                    ctx.line(et + " _cv = (" + et + ") " + m + ";");
                    ctx.line("_cv.encode(w);");
                    ctx.dedent();
                    first = false;
                }
                if (cd->otherwise) {
                    std::string ow_type = cd->otherwise->type_ref.empty()
                        ? j_inline_class(cd->otherwise->name, name_map)
                        : j_class(cd->otherwise->type_ref);
                    ctx.line(std::string(first ? "if" : "} else if") + " (" + m + " instanceof " + ow_type + ") {");
                    ctx.indent();
                    ctx.line(ow_type + " _cv = (" + ow_type + ") " + m + ";");
                    ctx.line("_cv.encode(w);");
                    ctx.dedent();
                    first = false;
                }
                if (!first) ctx.line("}");
            };
            if (cd->present_when) {
                ctx.line("if (" + m + " != null) {");
                ctx.indent(); emit_choice_encode(); ctx.dedent(); ctx.line("}");
            } else {
                emit_choice_encode();
            }
            tracker.advance_bits_variable();
        } else if (auto* res = std::get_if<model::Reserved>(&child)) {
            ctx.line("w.writeBits(0, " + std::to_string(res->bits) + ");");
            tracker.advance_bits(res->bits);
        } else if (auto* al = std::get_if<model::Align>(&child)) {
            ctx.line("w.alignTo(" + std::to_string(al->to) + ");");
            tracker.bit_mod8 = 0;
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            // FX extension: check if any children are set, write FX bit, conditionally encode
            ctx.line("{");
            ctx.indent();
            ctx.line("boolean _fxContinue = false;");
            j_fx_has_fields_check(ctx, fx->children, pfx, "_fxContinue");
            ctx.line("w.writeBits(_fxContinue ? 1 : 0, 1);");
            ctx.line("if (_fxContinue) {");
            ctx.indent();
            // Use FX-aware encoder that writes zero-fill for absent optional fields
            emit_j_encode_fx_children(ctx, fx->children, index, pfx, name_map, parent_class_name);
            // Write terminal FX=0 bit if this FX extent has no nested FxBlock
            {
                bool has_nested_fx = false;
                for (const auto& fc : fx->children) {
                    if (std::holds_alternative<model::FxBlock>(fc)) { has_nested_fx = true; break; }
                }
                if (!has_nested_fx) {
                    ctx.line("w.writeBits(0, 1); // Terminal FX=0");
                }
            }
            ctx.dedent();
            ctx.line("}");
            ctx.dedent();
            ctx.line("}");
        }
    }
    // If the target field was the last child, emit the backpatch now
    if (is_target_active) {
        emit_length_ref_patch();
    }
}

// Generate one Java class file for a struct or message
// ============================================================================
// Java Bitmap/FSPEC class generation
// ============================================================================

struct JBitmapField {
    std::string name;
    std::string j_type;
    int bit = 0;
    bool is_struct = false;
    bool is_enum = false;
    bool is_string = false;
    bool is_bytes = false;
    bool is_float = false;
    bool is_bool = false;
    bool is_signed = false;
    bool is_type_wrapper = false;
    bool is_string_wrapper = false;
    bool is_scaled_wrapper = false;
    bool has_scale = false;
    double scale = 1.0;
    double offset = 0.0;
    int bits = 0;
    int raw_bits = 0;
    bool raw_signed = false;
    model::Endian endian = model::Endian::Big;
    model::Endian raw_endian = model::Endian::Big;
    model::WireEncoding wire_enc = model::WireEncoding::Default;
    std::optional<int> length;
    std::optional<int> bytes_attr;
    bool is_choice = false;
    const model::ChoiceDef* choice_def = nullptr;
    const model::Field* source_field = nullptr;
    std::string doc;
};

// Forward declarations
static bool j_is_protocol_type(const std::string& t);
static void j_emit_imports(EmitContext& ctx,
                            const std::string& codec_pkg,
                            const std::string& pkg,
                            const std::set<std::string>& type_names);

std::string generate_j_bitmap_class(const model::StructDef& sd,
                                     const analyzer::TypeIndex& index,
                                     const std::string& pkg,
                                     const std::unordered_map<std::string, uint64_t>& /*tid_map*/,
                                     const JOuterScopeMap& scope_map = {},
                                     const JInlineNameMap& name_map = {},
                                     const std::string& class_name_override = {},
                                     const analyzer::WireSizeInfo* sizes = nullptr) {
    std::string cn = class_name_override.empty() ? j_class(sd.name) : class_name_override;

    // Collect bitmap-controlled fields
    std::vector<JBitmapField> bfields;
    for (const auto& child : sd.children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            if (f->bit) {
                JBitmapField bf;
                bf.name = f->name;
                auto fi = j_resolve_field(*f, index);
                bf.j_type = fi.j_type;
                bf.bit = *f->bit;
                bf.is_struct = fi.is_struct;
                bf.is_enum = fi.is_enum;
                bf.is_string = fi.is_string;
                bf.is_bytes = fi.is_bytes;
                bf.is_float = fi.is_float;
                bf.is_bool = fi.is_bool;
                bf.is_signed = fi.is_signed;
                bf.is_type_wrapper = fi.is_type_wrapper;
                bf.is_string_wrapper = fi.is_string_wrapper;
                bf.is_scaled_wrapper = fi.is_scaled_wrapper;
                bf.has_scale = fi.has_scale;
                bf.scale = fi.scale;
                bf.offset = fi.offset;
                bf.bits = fi.bits;
                bf.raw_bits = fi.raw_bits;
                bf.raw_signed = fi.raw_signed;
                bf.endian = fi.endian;
                bf.raw_endian = fi.endian;
                bf.wire_enc = fi.wire_enc;
                bf.length = f->length;
                bf.bytes_attr = f->bytes_attr;
                bf.source_field = f;
                bf.doc = f->doc;
                // Use boxed types for primitives
                if (bf.j_type == "int") bf.j_type = "Integer";
                else if (bf.j_type == "long") bf.j_type = "Long";
                else if (bf.j_type == "float") bf.j_type = "Float";
                else if (bf.j_type == "double") bf.j_type = "Double";
                else if (bf.j_type == "boolean") bf.j_type = "Boolean";
                bfields.push_back(bf);
            }
        } else if (auto* child_sd = std::get_if<model::StructDef>(&child)) {
            if (child_sd->bit) {
                JBitmapField bf;
                bf.name = child_sd->name;
                bf.j_type = j_inline_class(child_sd->name, name_map);
                bf.bit = *child_sd->bit;
                bf.is_struct = true;
                bf.doc = child_sd->doc;
                bfields.push_back(bf);
            }
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            if (cd->bit) {
                JBitmapField bf;
                bf.name = cd->name;
                // Use Object for field declaration since choice cases have no
                // common base class.  The switch_expr decode path (below)
                // resolves individual case types directly.
                bf.j_type = "Object";
                bf.bit = *cd->bit;
                bf.is_struct = true;
                bf.is_choice = true;
                bf.choice_def = cd;
                bfields.push_back(bf);
            }
        }
    }

    int max_bit = 0;
    for (const auto& bf : bfields) max_bit = std::max(max_bit, bf.bit);
    bool has_ext = sd.bitmap_ext.has_value();
    int max_octet = max_bit / J_BITS_PER_BYTE;
    int num_octets = max_octet + 1;
    bool fspec_le = !bfields.empty() && bfields[0].endian == model::Endian::Little;

    // Sort by bit position (octet first, then descending bit within octet)
    auto sorted_fields = bfields;
    std::sort(sorted_fields.begin(), sorted_fields.end(), [](const auto& a, const auto& b) {
        int a_oct = a.bit / 8;
        int b_oct = b.bit / 8;
        if (a_oct != b_oct) return a_oct < b_oct;
        return a.bit > b.bit;
    });

    std::set<std::string> type_imports;
    for (const auto& bf : bfields) {
        if (j_is_protocol_type(bf.j_type)) type_imports.insert(bf.j_type);
    }
    type_imports.erase(cn);

    EmitContext ctx;
    ctx.line("// Generated by bgen - DO NOT EDIT");
    ctx.line("package " + pkg + ";");
    ctx.line();
    j_emit_imports(ctx, pkg + ".codec", pkg, type_imports);
    j_emit_doc(ctx, sd.doc);
    ctx.line("public final class " + cn + " {");
    ctx.indent();
    if (sizes) {
        auto ws = sizes->get(sd.name);
        if (ws) {
            ctx.line("public static final int WIRE_SIZE = " + std::to_string(*ws) + ";");
        }
    }

    // Fields — all nullable
    for (const auto& bf : bfields) {
        j_emit_doc(ctx, bf.doc);
        ctx.line("public " + bf.j_type + " " + j_field(bf.name) + " = null;");
    }
    ctx.line();

    // Accessors for bitmap fields (all nullable/optional, matching C++ emit_optional_accessors)
    for (const auto& bf : bfields) {
        std::string acc = j_class(bf.name);
        std::string m = j_field(bf.name);

        // Getter
        ctx.line("public " + bf.j_type + " get" + acc + "() { return " + m + "; }");
        // has / clear
        ctx.line("public boolean has" + acc + "() { return " + m + " != null; }");
        ctx.line("public void clear" + acc + "() { " + m + " = null; }");

        // Setter — check constraints if the source field has them
        bool has_constraint = bf.source_field && bf.source_field->constraint
            && bf.source_field->constraint->validate != model::ValidateTiming::Deferred
            && (bf.source_field->constraint->equals || bf.source_field->constraint->min ||
                bf.source_field->constraint->max);
        bool has_ml = bf.source_field && bf.source_field->max_length.has_value();

        if (has_constraint || has_ml) {
            ctx.line("public void set" + acc + "(" + bf.j_type + " v) {");
            ctx.indent();
            if (has_constraint) {
                const auto& con = *bf.source_field->constraint;
                if (con.equals)
                    ctx.line("if (v != " + j_qualify_const(*con.equals) +
                             ") throw new ConduitCodecException(\"" + bf.name +
                             " constraint: expected " + *con.equals + "\");");
                if (con.max)
                    ctx.line("if (v > " + j_qualify_const(*con.max) +
                             ") throw new ConduitCodecException(\"" + bf.name +
                             " exceeds max " + *con.max + "\");");
                if (con.min && (*con.min != "0" || bf.is_signed))
                    ctx.line("if (v < " + j_qualify_const(*con.min) +
                             ") throw new ConduitCodecException(\"" + bf.name +
                             " below min " + *con.min + "\");");
            }
            if (has_ml) {
                int ml = *bf.source_field->max_length;
                if (bf.is_string)
                    ctx.line("if (v.length() > " + std::to_string(ml) +
                             ") throw new ConduitCodecException(\"" + bf.name +
                             " exceeds max length " + std::to_string(ml) + "\");");
                else if (bf.is_bytes)
                    ctx.line("if (v.length > " + std::to_string(ml) +
                             ") throw new ConduitCodecException(\"" + bf.name +
                             " exceeds max length " + std::to_string(ml) + "\");");
            }
            ctx.line(m + " = v;");
            ctx.dedent();
            ctx.line("}");
        } else {
            ctx.line("public void set" + acc + "(" + bf.j_type + " v) { " + m + " = v; }");
        }

        // Raw accessors for scaled bitmap fields
        if (bf.has_scale) {
            std::string raw_type = (bf.raw_bits <= 32) ? "int" : "long";
            std::string scale_s = j_double(bf.scale);
            std::string offset_s = j_double(bf.offset);
            if (bf.offset != 0.0) {
                ctx.line("public " + raw_type + " get" + acc + "Raw() { return " + m +
                         " != null ? (" + raw_type + ")((" + m + " - " + offset_s + ") / " +
                         scale_s + ") : 0; }");
            } else {
                ctx.line("public " + raw_type + " get" + acc + "Raw() { return " + m +
                         " != null ? (" + raw_type + ")(" + m + " / " + scale_s + ") : 0; }");
            }
            ctx.line("public void set" + acc + "Raw(" + raw_type + " v) { " + m +
                     " = (double)(v) * " + scale_s + " + " + offset_s + "; }");
        }
    }
    ctx.line();

    // Build outer-scope decode parameters
    auto osp_it = scope_map.find(sd.name);
    std::string decode_params;
    JOuterContext outer_ctx;
    if (osp_it != scope_map.end()) {
        for (const auto& p : osp_it->second) {
            decode_params += ", " + p.java_type + " " + j_field(p.bmdl_name);
            outer_ctx[p.bmdl_name] = j_field(p.bmdl_name);
        }
    }

    // decode()
    ctx.line("public static " + cn + " decode(BitReader r" + decode_params + ") {");
    ctx.indent();
    ctx.line(cn + " result = new " + cn + "();");
    ctx.line();
    ctx.line("// Read FSPEC bitmap");
    ctx.line("byte[] fspec = new byte[" + std::to_string(num_octets) + "];");
    ctx.line("int fspecLen = 0;");
    if (has_ext) {
        ctx.line("while (true) {");
        ctx.indent();
        ctx.line("int b = r.readU8();");
        ctx.line("if (fspecLen < " + std::to_string(num_octets) + ") fspec[fspecLen] = (byte) b;");
        ctx.line("fspecLen++;");
        ctx.line("if ((b & (1 << " + std::to_string(*sd.bitmap_ext) + ")) == 0) break;");
        ctx.dedent();
        ctx.line("}");
    } else {
        ctx.line("for (int i = 0; i < " + std::to_string(num_octets) + "; i++) {");
        ctx.indent();
        ctx.line("fspec[i] = (byte) r.readU8();");
        ctx.dedent();
        ctx.line("}");
        ctx.line("fspecLen = " + std::to_string(num_octets) + ";");
    }
    if (fspec_le) {
        ctx.line("for (int lo = 0, hi = fspecLen - 1; lo < hi; lo++, hi--) { byte tmp = fspec[lo]; fspec[lo] = fspec[hi]; fspec[hi] = tmp; }");
    }
    ctx.line();

    // Decode fields based on FSPEC bits
    for (const auto& bf : sorted_fields) {
        int byte_idx = bf.bit / J_BITS_PER_BYTE;
        int bit_in_byte = bf.bit % J_BITS_PER_BYTE;
        std::string m = "result." + j_field(bf.name);
        ctx.line("if (fspecLen > " + std::to_string(byte_idx) +
                 " && (fspec[" + std::to_string(byte_idx) +
                 "] & (1 << " + std::to_string(bit_in_byte) + ")) != 0) {");
        ctx.indent();
        if (bf.is_choice && bf.choice_def && bf.choice_def->switch_expr) {
            // Choice decode based on switch expression
            std::string sv = "result." + j_field(bf.choice_def->switch_expr->name);
            // If the switch field is an enum, compare using .value (raw int)
            if (bf.choice_def->switch_expr->op == model::ExprOp::FieldRef) {
                for (const auto& sbf : bfields) {
                    if (sbf.name == bf.choice_def->switch_expr->name && sbf.is_enum) {
                        sv += ".value";
                        break;
                    }
                }
            }
            bool first_case = true;
            for (const auto& cs : bf.choice_def->cases) {
                std::string cond;
                if (cs.value) {
                    std::string val = *cs.value;
                    if (index.constants.count(*cs.value)) {
                        val = "Constants." + j_const(*cs.value);
                    }
                    cond = sv + " == " + val;
                } else if (cs.range) {
                    auto dot_pos = cs.range->find("..");
                    if (dot_pos != std::string::npos) {
                        std::string min_s = cs.range->substr(0, dot_pos);
                        std::string max_s = cs.range->substr(dot_pos + 2);
                        if (index.constants.count(min_s)) min_s = "Constants." + j_const(min_s);
                        if (index.constants.count(max_s)) max_s = "Constants." + j_const(max_s);
                        if (min_s == "0") {
                            cond = sv + " <= " + max_s;
                        } else {
                            cond = sv + " >= " + min_s + " && " + sv + " <= " + max_s;
                        }
                    } else {
                        std::string range_val = *cs.range;
                        if (index.constants.count(range_val)) range_val = "Constants." + j_const(range_val);
                        cond = sv + " == " + range_val;
                    }
                } else {
                    continue;
                }
                ctx.line(std::string(first_case ? "if (" : "} else if (") + cond + ") {");
                ctx.indent();
                std::string et = cs.type_ref.empty() ? j_inline_class(cs.name, name_map) : j_class(cs.type_ref);
                std::string case_args;
                auto child_osp = scope_map.find(cs.type_ref.empty() ? cs.name : cs.type_ref);
                if (child_osp != scope_map.end()) {
                    for (const auto& p : child_osp->second)
                        case_args += ", result." + j_field(p.bmdl_name);
                }
                ctx.line(m + " = " + et + ".decode(r" + case_args + ");");
                ctx.dedent();
                first_case = false;
            }
            if (bf.choice_def->otherwise) {
                ctx.line("} else {");
                ctx.indent();
                std::string et = bf.choice_def->otherwise->type_ref.empty()
                    ? j_inline_class(bf.choice_def->otherwise->name, name_map)
                    : j_class(bf.choice_def->otherwise->type_ref);
                ctx.line(m + " = " + et + ".decode(r);");
                ctx.dedent();
            }
            if (!first_case) ctx.line("}");
        } else if (bf.is_struct && !bf.is_string && !bf.is_bytes) {
            std::string args;
            auto child_osp = scope_map.find(bf.name);
            if (child_osp != scope_map.end()) {
                for (const auto& p : child_osp->second) {
                    args += ", result." + j_field(p.bmdl_name);
                }
            }
            ctx.line(m + " = " + bf.j_type + ".decode(r" + args + ");");
        } else if (bf.is_enum) {
            ctx.line(m + " = " + bf.j_type + ".decode(r);");
        } else if (bf.has_scale) {
            // Scaled field
            std::string be = (bf.raw_endian == model::Endian::Big) ? "true" : "false";
            std::string read;
            if (bf.raw_signed) {
                if (bf.raw_bits <= 8) read = "r.readSignedBits(" + std::to_string(bf.raw_bits) + ")";
                else read = "r.readSignedBits(" + std::to_string(bf.raw_bits) + ")";
            } else {
                if (bf.raw_bits == 16) read = "r.readU16(" + be + ")";
                else if (bf.raw_bits == 32) read = "r.readU32(" + be + ")";
                else if (bf.raw_bits == 64) read = "r.readU64(" + be + ")";
                else read = "r.readBits(" + std::to_string(bf.raw_bits) + ")";
            }
            ctx.line(m + " = (double) " + read + " * " + j_double(bf.scale) +
                     (bf.offset != 0.0 ? " + " + j_double(bf.offset) : "") + ";");
        } else if (bf.is_string) {
            bool has_enc = bf.source_field && j_field_needs_encoding(*bf.source_field);
            std::string enc_arg = has_enc ? ", " + j_encoding_const(*bf.source_field) : "";
            std::string read_fn = has_enc ? "readStringEncoded" : "readString";
            if (bf.length) {
                ctx.line(m + " = r." + read_fn + "(" + std::to_string(*bf.length) + enc_arg + ");");
            } else {
                ctx.line(m + " = r." + read_fn + "(r.remainingBytes()" + enc_arg + ");");
            }
        } else if (bf.is_bytes) {
            if (bf.length) {
                ctx.line(m + " = r.readBytes(" + std::to_string(*bf.length) + ");");
            } else if (bf.bytes_attr) {
                ctx.line(m + " = r.readBytes(" + std::to_string(*bf.bytes_attr) + ");");
            } else {
                ctx.line(m + " = r.readBytes(r.remainingBytes());");
            }
        } else if (bf.is_bool) {
            ctx.line(m + " = r.readBits(" + std::to_string(bf.bits) + ") != 0;");
        } else {
            // Primitive integer
            std::string be = (bf.endian == model::Endian::Big) ? "true" : "false";
            std::string read;
            if (bf.wire_enc == model::WireEncoding::BCD) {
                read = std::string(bf.bits > 32 ? "(long)" : "(int)") + " r.readBcd(" + std::to_string(bf.bits) + ")";
            } else if (bf.wire_enc == model::WireEncoding::BCD_S) {
                read = std::string(bf.bits > 32 ? "(long)" : "(int)") + " r.readBcdSigned(" + std::to_string(bf.bits) + ")";
            } else if (bf.wire_enc == model::WireEncoding::BNR_S) {
                read = "r.readSignMagnitude(" + std::to_string(bf.bits) + ")";
            } else if (bf.is_signed || bf.wire_enc == model::WireEncoding::CB2) {
                if (bf.bits <= 8) read = "(int) r.readSignedBits(" + std::to_string(bf.bits) + ")";
                else read = "(int) r.readSignedBits(" + std::to_string(bf.bits) + ")";
            } else {
                if (bf.bits == 8) read = "r.readU8()";
                else if (bf.bits == 16) read = "r.readU16(" + be + ")";
                else if (bf.bits == 32) read = "r.readU32(" + be + ")";
                else if (bf.bits == 64) read = "r.readU64(" + be + ")";
                else read = "(int) r.readBits(" + std::to_string(bf.bits) + ")";
            }
            if (bf.is_float) {
                if (bf.bits == 16) read = "r.readF16(" + be + ")";
                else if (bf.bits <= 32) read = "r.readF32(" + be + ")";
                else if (bf.bits <= 48) read = "r.readF48(" + be + ")";
                else read = "r.readF64(" + be + ")";
            }
            ctx.line(m + " = " + read + ";");
        }
        ctx.dedent();
        ctx.line("}");
    }

    ctx.line();
    ctx.line("return result;");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // decodeBytes
    ctx.line("public static " + cn + " decodeBytes(byte[] data) {");
    ctx.indent();
    ctx.line("return " + cn + ".decode(new BitReader(data));");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // encode()
    ctx.line("public void encode(BitWriter w) {");
    ctx.indent();

    ctx.line("byte[] fspec = new byte[" + std::to_string(num_octets) + "];");
    if (has_ext) {
        ctx.line("int lastOctet = 0;");
        for (const auto& bf : bfields) {
            int byte_idx = bf.bit / J_BITS_PER_BYTE;
            int bit_in_byte = bf.bit % J_BITS_PER_BYTE;
            ctx.line("if (" + j_field(bf.name) + " != null) { fspec[" +
                     std::to_string(byte_idx) + "] = (byte)(fspec[" +
                     std::to_string(byte_idx) + "] | (1 << " +
                     std::to_string(bit_in_byte) + ")); lastOctet = Math.max(lastOctet, " +
                     std::to_string(byte_idx) + "); }");
        }
        ctx.line("for (int i = 0; i < lastOctet; i++) fspec[i] = (byte)(fspec[i] | (1 << " +
                 std::to_string(*sd.bitmap_ext) + "));" );
        if (fspec_le) {
            ctx.line("for (int lo = 0, hi = lastOctet; lo < hi; lo++, hi--) { byte tmp = fspec[lo]; fspec[lo] = fspec[hi]; fspec[hi] = tmp; }");
        }
        ctx.line("for (int i = 0; i <= lastOctet; i++) w.writeU8(fspec[i] & 0xFF);");
    } else {
        for (const auto& bf : bfields) {
            int byte_idx = bf.bit / J_BITS_PER_BYTE;
            int bit_in_byte = bf.bit % J_BITS_PER_BYTE;
            ctx.line("if (" + j_field(bf.name) + " != null) fspec[" +
                     std::to_string(byte_idx) + "] = (byte)(fspec[" +
                     std::to_string(byte_idx) + "] | (1 << " +
                     std::to_string(bit_in_byte) + "));");
        }
        if (fspec_le) {
            ctx.line("for (int lo = 0, hi = " + std::to_string(num_octets - 1) + "; lo < hi; lo++, hi--) { byte tmp = fspec[lo]; fspec[lo] = fspec[hi]; fspec[hi] = tmp; }");
        }
        ctx.line("for (int i = 0; i < " + std::to_string(num_octets) + "; i++) w.writeU8(fspec[i] & 0xFF);");
    }

    // Encode present fields
    for (const auto& bf : sorted_fields) {
        std::string m = j_field(bf.name);
        ctx.line("if (" + m + " != null) {");
        ctx.indent();
        if ((bf.is_struct && !bf.is_string && !bf.is_bytes) || bf.is_enum) {
            ctx.line(m + ".encode(w);");
        } else if (bf.has_scale) {
            std::string be = (bf.raw_endian == model::Endian::Big) ? "true" : "false";
            std::string reverse_scale = "(long) ((" + m + " - " + j_double(bf.offset) + ") / " + j_double(bf.scale) + ")";
            if (bf.raw_signed) {
                if (bf.raw_bits <= 8) ctx.line("w.writeSignedBits(" + reverse_scale + ", " + std::to_string(bf.raw_bits) + ");");
                else ctx.line("w.writeSignedBits(" + reverse_scale + ", " + std::to_string(bf.raw_bits) + ");");
            } else {
                if (bf.raw_bits == 16) ctx.line("w.writeU16((int) " + reverse_scale + ", " + be + ");");
                else if (bf.raw_bits == 32) ctx.line("w.writeU32((int) " + reverse_scale + ", " + be + ");");
                else if (bf.raw_bits == 64) ctx.line("w.writeU64(" + reverse_scale + ", " + be + ");");
                else ctx.line("w.writeBits(" + reverse_scale + ", " + std::to_string(bf.raw_bits) + ");");
            }
        } else if (bf.is_string) {
            int pad = 0;
            if (bf.source_field && bf.source_field->padding && *bf.source_field->padding == model::StringPadding::Space) {
                bool is_ebcdic = (bf.source_field->encoding && *bf.source_field->encoding == model::StringEncoding::Ebcdic);
                if (!is_ebcdic && !bf.source_field->type_ref.empty()) {
                    auto it = index.types.find(bf.source_field->type_ref);
                    if (it != index.types.end() && it->second->encoding == model::StringEncoding::Ebcdic)
                        is_ebcdic = true;
                }
                pad = is_ebcdic ? 0x40 : 0x20;
            } else if (bf.source_field && !bf.source_field->padding && !bf.source_field->type_ref.empty()) {
                auto it = index.types.find(bf.source_field->type_ref);
                if (it != index.types.end() && it->second->padding == model::StringPadding::Space) {
                    bool is_ebcdic = (it->second->encoding == model::StringEncoding::Ebcdic);
                    pad = is_ebcdic ? 0x40 : 0x20;
                }
            }
            bool has_enc = bf.source_field && j_field_needs_encoding(*bf.source_field);
            std::string enc_arg = has_enc ? ", " + j_encoding_const(*bf.source_field) : "";
            std::string write_fn = has_enc ? "writeStringEncoded" : "writeString";
            if (bf.length)
                ctx.line("w." + write_fn + "(" + m + ", " + std::to_string(*bf.length) + ", " + std::to_string(pad) + enc_arg + ");");
            else
                ctx.line("w." + write_fn + "(" + m + ", " + m + ".length(), " + std::to_string(pad) + enc_arg + ");");
        } else if (bf.is_bytes) {
            ctx.line("w.writeBytes(" + m + ");");
        } else if (bf.is_bool) {
            ctx.line("w.writeBits(" + m + " ? 1 : 0, " + std::to_string(bf.bits) + ");");
        } else {
            std::string be = (bf.endian == model::Endian::Big) ? "true" : "false";
            if (bf.is_float) {
                if (bf.bits == 16) ctx.line("w.writeF16(" + m + ", " + be + ");");
                else if (bf.bits <= 32) ctx.line("w.writeF32(" + m + ", " + be + ");");
                else if (bf.bits <= 48) ctx.line("w.writeF48(" + m + ", " + be + ");");
                else ctx.line("w.writeF64(" + m + ", " + be + ");");
            } else if (bf.wire_enc == model::WireEncoding::BCD) {
                ctx.line("w.writeBcd(" + m + ", " + std::to_string(bf.bits) + ");");
            } else if (bf.wire_enc == model::WireEncoding::BCD_S) {
                ctx.line("w.writeBcdSigned(" + m + ", " + std::to_string(bf.bits) + ");");
            } else if (bf.wire_enc == model::WireEncoding::BNR_S) {
                ctx.line("w.writeSignMagnitude(" + m + ", " + std::to_string(bf.bits) + ");");
            } else if (bf.is_signed || bf.wire_enc == model::WireEncoding::CB2) {
                if (bf.bits <= 8) ctx.line("w.writeSignedBits(" + m + ", " + std::to_string(bf.bits) + ");");
                else ctx.line("w.writeSignedBits(" + m + ", " + std::to_string(bf.bits) + ");");
            } else {
                if (bf.bits == 8) ctx.line("w.writeU8(" + m + ");");
                else if (bf.bits == 16) ctx.line("w.writeU16(" + m + ", " + be + ");");
                else if (bf.bits == 32) ctx.line("w.writeU32(" + m + ", " + be + ");");
                else if (bf.bits == 64) ctx.line("w.writeU64(" + m + ", " + be + ");");
                else ctx.line("w.writeBits(" + m + ", " + std::to_string(bf.bits) + ");");
            }
        }
        ctx.dedent();
        ctx.line("}");
    }

    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // encodeBytes
    ctx.line("public byte[] encodeBytes() {");
    ctx.indent();
    ctx.line("BitWriter w = new BitWriter();");
    ctx.line("this.encode(w);");
    ctx.line("return w.toBytes();");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // toString
    ctx.line("@Override public String toString() {");
    ctx.indent();
    if (bfields.empty()) {
        ctx.line("return \"" + cn + "()\";");
    } else {
        std::string fmt = "return \"" + cn + "(\" + ";
        bool first = true;
        for (const auto& bf : bfields) {
            if (!first) fmt += " + \", \" + ";
            fmt += "\"" + j_field(bf.name) + "=\" + " + j_field(bf.name);
            first = false;
        }
        ctx.line(fmt + " + \")\";");
    }
    ctx.dedent();
    ctx.line("}");

    // equals()
    ctx.line();
    ctx.line("@Override public boolean equals(Object o) {");
    ctx.indent();
    ctx.line("if (this == o) return true;");
    ctx.line("if (!(o instanceof " + cn + ")) return false;");
    ctx.line(cn + " that = (" + cn + ") o;");
    if (bfields.empty()) {
        ctx.line("return true;");
    } else {
        std::string eq_expr;
        for (size_t i = 0; i < bfields.size(); i++) {
            if (i > 0) eq_expr += " && ";
            eq_expr += "java.util.Objects.equals(" + j_field(bfields[i].name) + ", that." + j_field(bfields[i].name) + ")";
        }
        ctx.line("return " + eq_expr + ";");
    }
    ctx.dedent();
    ctx.line("}");

    // hashCode()
    ctx.line();
    ctx.line("@Override public int hashCode() {");
    ctx.indent();
    if (bfields.empty()) {
        ctx.line("return 0;");
    } else {
        std::string args;
        for (size_t i = 0; i < bfields.size(); i++) {
            if (i > 0) args += ", ";
            args += j_field(bfields[i].name);
        }
        ctx.line("return java.util.Objects.hash(" + args + ");");
    }
    ctx.dedent();
    ctx.line("}");

    // toMap()
    ctx.line();
    ctx.line("public java.util.Map<String, Object> toMap() {");
    ctx.indent();
    ctx.line("java.util.Map<String, Object> m = new java.util.LinkedHashMap<>();");
    for (const auto& bf : bfields) {
        std::string fn = j_field(bf.name);
        std::string key = bf.name;
        ctx.line("if (" + fn + " != null) {");
        ctx.indent();
        if (bf.is_enum) {
            ctx.line("m.put(\"" + key + "\", " + fn + ".value);");
        } else if (bf.is_type_wrapper) {
            ctx.line("m.put(\"" + key + "\", " + fn + ".value());");
        } else if (bf.is_struct) {
            ctx.line("m.put(\"" + key + "\", " + fn + ".toMap());");
        } else if (bf.is_bytes) {
            ctx.line("{ java.util.List<Integer> _bl = new java.util.ArrayList<>(); for (byte _b : " + fn + ") _bl.add((int)_b & 0xFF); m.put(\"" + key + "\", _bl); }");
        } else {
            ctx.line("m.put(\"" + key + "\", " + fn + ");");
        }
        ctx.dedent();
        ctx.line("}");
    }
    ctx.line("return m;");
    ctx.dedent();
    ctx.line("}");

    // fromMap()
    ctx.line();
    ctx.line("@SuppressWarnings(\"unchecked\")");
    ctx.line("public static " + cn + " fromMap(java.util.Map<String, Object> d) {");
    ctx.indent();
    ctx.line(cn + " obj = new " + cn + "();");
    for (const auto& bf : bfields) {
        std::string fn = j_field(bf.name);
        std::string key = bf.name;
        ctx.line("if (d.containsKey(\"" + key + "\")) {");
        ctx.indent();
        if (bf.is_enum) {
            // Enum from int value
            ctx.line("{ int _rv = ((Number) d.get(\"" + key + "\")).intValue(); for (" + bf.j_type + " v : " + bf.j_type + ".values()) { if (v.value == _rv) { obj." + fn + " = v; break; } } }");
        } else if (bf.is_string_wrapper) {
            ctx.line("{ Object _sv = d.get(\"" + key + "\"); if (_sv instanceof String) obj." + fn + " = new " + bf.j_type + "((String)_sv); }");
        } else if (bf.is_scaled_wrapper) {
            ctx.line("{ Object _sv = d.get(\"" + key + "\"); if (_sv instanceof Number) { obj." + fn + " = new " + bf.j_type + "(); obj." + fn + ".setValue(((Number)_sv).doubleValue()); } }");
        } else if (bf.is_type_wrapper) {
            ctx.line("{ Object _sv = d.get(\"" + key + "\"); if (_sv instanceof Number) obj." + fn + " = new " + bf.j_type + "(((Number)_sv).longValue()); }");
        } else if (bf.has_scale || bf.is_float) {
            ctx.line("obj." + fn + " = ((Number) d.get(\"" + key + "\")).doubleValue();");
        } else if (bf.is_struct) {
            ctx.line("obj." + fn + " = " + bf.j_type + ".fromMap((java.util.Map<String, Object>) d.get(\"" + key + "\"));");
        } else if (bf.is_bytes) {
            ctx.line("{ java.util.List<Number> _bl = (java.util.List<Number>) d.get(\"" + key + "\"); byte[] _ba = new byte[_bl.size()]; for (int _i = 0; _i < _bl.size(); _i++) _ba[_i] = _bl.get(_i).byteValue(); obj." + fn + " = _ba; }");
        } else if (bf.is_bool) {
            ctx.line("obj." + fn + " = (Boolean) d.get(\"" + key + "\");");
        } else if (bf.is_string) {
            ctx.line("obj." + fn + " = (String) d.get(\"" + key + "\");");
        } else if (bf.j_type == "long" || bf.j_type == "Long") {
            ctx.line("obj." + fn + " = ((Number) d.get(\"" + key + "\")).longValue();");
        } else {
            ctx.line("obj." + fn + " = ((Number) d.get(\"" + key + "\")).intValue();");
        }
        ctx.dedent();
        ctx.line("}");
    }
    ctx.line("return obj;");
    ctx.dedent();
    ctx.line("}");

    // validate() for bitmap class
    {
        bool has_any = false;
        for (const auto& child : sd.children) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                if (f->constraint &&
                    f->constraint->validate != model::ValidateTiming::Deferred &&
                    (f->constraint->equals || f->constraint->min || f->constraint->max)) {
                    has_any = true; break;
                }
            }
        }
        if (has_any) {
            ctx.line();
            ctx.line("public void validate() {");
            ctx.indent();
            for (const auto& child : sd.children) {
                if (auto* f = std::get_if<model::Field>(&child)) {
                    if (!f->constraint) continue;
                    const auto& con = *f->constraint;
                    if (con.validate == model::ValidateTiming::Deferred) continue;
                    if (!con.equals && !con.min && !con.max) continue;
                    auto fi = j_resolve_field(*f, index);
                    if (fi.is_struct || fi.is_enum || fi.is_string || fi.is_bytes) continue;
                    std::string m = j_field(f->name);
                    // All bitmap fields are nullable
                    ctx.line("if (" + m + " != null) {");
                    ctx.indent();
                    if (con.equals)
                        ctx.line("if (" + m + " != " + j_qualify_const(*con.equals) +
                                 ") throw new ConduitCodecException(\"" + f->name +
                                 ": expected " + *con.equals + "\");");
                    if (con.max)
                        ctx.line("if (" + m + " > " + j_qualify_const(*con.max) +
                                 ") throw new ConduitCodecException(\"" + f->name +
                                 " exceeds max " + *con.max + "\");");
                    if (con.min && (*con.min != "0" || fi.is_signed))
                        ctx.line("if (" + m + " < " + j_qualify_const(*con.min) +
                                 ") throw new ConduitCodecException(\"" + f->name +
                                 " below min " + *con.min + "\");");
                    ctx.dedent();
                    ctx.line("}");
                }
            }
            ctx.dedent();
            ctx.line("}");
        }
    }

    ctx.dedent();
    ctx.line("}");
    return ctx.str();
}

// ============================================================================
// Import collection helpers
// ============================================================================

// Returns true if a Java type name refers to a protocol-generated type (not a Java built-in).
static bool j_is_protocol_type(const std::string& t) {
    if (t.empty() || t.find("java.") == 0) return false;
    static const std::set<std::string> builtins = {
        "int", "long", "double", "float", "boolean", "byte[]",
        "String", "Object", "Integer", "Long", "Float", "Double", "Boolean"
    };
    return builtins.find(t) == builtins.end();
}

// Collect protocol-generated type names referenced by the given fields.
// These need explicit import statements when the codec infrastructure is in a sub-package.
static void j_collect_type_imports(const std::vector<JFieldDef>& fields,
                                    std::set<std::string>& out) {
    for (const auto& f : fields) {
        std::string t = f.j_type;
        // Extract T from java.util.List<T>
        if (t.find("java.util.List<") == 0 && t.back() == '>') {
            t = t.substr(15, t.size() - 16);
        } else if (t.find("java.") == 0) {
            continue;
        }
        if (j_is_protocol_type(t)) out.insert(t);
    }
}

// Emit import block: codec wildcard + per-type imports for protocol types.
// codec_pkg  = the .codec sub-package (e.g. "asterix.codec")
// pkg        = the protocol package (e.g. "asterix")
// type_names = sorted set of protocol type class names to import explicitly
static void j_emit_imports(EmitContext& ctx,
                            const std::string& codec_pkg,
                            const std::string& pkg,
                            const std::set<std::string>& type_names) {
    ctx.line("import " + codec_pkg + ".*;");
    for (const auto& t : type_names) {
        ctx.line("import " + pkg + "." + t + ";");
    }
    ctx.line("import java.util.*;");
    ctx.line();
}

// ============================================================================
// Normal (non-bitmap) Java class generation
// ============================================================================

std::string generate_j_class(const std::string& name,
                              const std::vector<model::StructChild>& children,
                              const analyzer::TypeIndex& index,
                              const std::string& pkg,
                              const std::unordered_map<std::string, uint64_t>& tid_map,
                              const std::string& msg_id = "",
                              const JOuterScopeMap& scope_map = {},
                              const JInlineNameMap& name_map = {},
                              const std::string& class_name_override = {},
                              const std::vector<JFieldDef>& extra_fields = {},
                              const std::string& doc = {},
                              const analyzer::WireSizeInfo* sizes = nullptr) {
    std::string cn = class_name_override.empty() ? j_class(name) : class_name_override;
    std::vector<JFieldDef> fields;
    // Add frame header/footer fields if this is a message used in a frame
    for (const auto& ef : extra_fields) fields.push_back(ef);
    collect_j_fields(children, index, fields, name_map, cn);

    std::set<std::string> type_imports;
    j_collect_type_imports(fields, type_imports);
    type_imports.erase(cn); // don't import self

    EmitContext ctx;
    ctx.line("// Generated by bgen - DO NOT EDIT");
    ctx.line("package " + pkg + ";");
    ctx.line();
    j_emit_imports(ctx, pkg + ".codec", pkg, type_imports);
    j_emit_doc(ctx, doc);
    ctx.line("public final class " + cn + " {");
    ctx.indent();

    auto tid_it = tid_map.find(name);
    if (tid_it != tid_map.end()) {
        ctx.line("public static final long TYPE_ID = " + j_hex64(tid_it->second) + ";");
        ctx.line("public static final String TYPE_NAME = \"" + name + "\";");
    }
    if (!msg_id.empty()) ctx.line("public static final int ID_VALUE = " + msg_id + ";");
    if (sizes) {
        auto ws = sizes->get(name);
        if (ws) {
            ctx.line("public static final int WIRE_SIZE = " + std::to_string(*ws) + ";");
        }
    }
    ctx.line();

    // Fields
    for (const auto& f : fields) {
        j_emit_doc(ctx, f.doc);
        ctx.line("public " + f.j_type + " " + f.name + " = " + f.init + ";");
    }
    ctx.line();

    // Accessors (matching C++ emit_plain_accessors / emit_optional_accessors)
    for (const auto& f : fields) {
        std::string acc = j_class(f.bmdl_name); // PascalCase accessor suffix
        if (acc.empty()) continue; // extra fields without bmdl_name (frame fields)

        // Getter
        ctx.line("public " + f.j_type + " get" + acc + "() { return " + f.name + "; }");

        // Determine if setter needs constraint validation
        bool has_immediate_constraint = f.constraint
            && f.constraint->validate != model::ValidateTiming::Deferred
            && (f.constraint->equals || f.constraint->min || f.constraint->max);
        bool needs_validation = has_immediate_constraint || f.max_length.has_value();

        if (needs_validation) {
            ctx.line("public void set" + acc + "(" + f.j_type + " v) {");
            ctx.indent();
            if (has_immediate_constraint && f.is_bytes) {
                // Byte-array fields: convert to numeric value before checking constraints
                bool need_numeric = f.constraint->equals || f.constraint->max ||
                    (f.constraint->min && (*f.constraint->min != "0" || f.is_signed));
                if (need_numeric) {
                    ctx.line("long _raw = 0;");
                    ctx.line("for (int i = 0; i < v.length; i++) _raw = (_raw << 8) | (v[i] & 0xFF);");
                    if (f.constraint->equals)
                        ctx.line("if (_raw != " + j_qualify_const(*f.constraint->equals) +
                                 ") throw new ConduitCodecException(\"" + f.bmdl_name +
                                 " constraint: expected " + *f.constraint->equals + "\");");
                    if (f.constraint->max)
                        ctx.line("if (_raw > " + j_qualify_const(*f.constraint->max) +
                                 ") throw new ConduitCodecException(\"" + f.bmdl_name +
                                 " exceeds max " + *f.constraint->max + "\");");
                    if (f.constraint->min && (*f.constraint->min != "0" || f.is_signed))
                        ctx.line("if (_raw < " + j_qualify_const(*f.constraint->min) +
                                 ") throw new ConduitCodecException(\"" + f.bmdl_name +
                                 " below min " + *f.constraint->min + "\");");
                }
            } else if (has_immediate_constraint) {
                if (f.constraint->equals)
                    ctx.line("if (v != " + j_qualify_const(*f.constraint->equals) +
                             ") throw new ConduitCodecException(\"" + f.bmdl_name +
                             " constraint: expected " + *f.constraint->equals + "\");");
                if (f.constraint->max)
                    ctx.line("if (v > " + j_qualify_const(*f.constraint->max) +
                             ") throw new ConduitCodecException(\"" + f.bmdl_name +
                             " exceeds max " + *f.constraint->max + "\");");
                if (f.constraint->min && (*f.constraint->min != "0" || f.is_signed))
                    ctx.line("if (v < " + j_qualify_const(*f.constraint->min) +
                             ") throw new ConduitCodecException(\"" + f.bmdl_name +
                             " below min " + *f.constraint->min + "\");");
            }
            if (f.max_length) {
                if (f.is_string)
                    ctx.line("if (v.length() > " + std::to_string(*f.max_length) +
                             ") throw new ConduitCodecException(\"" + f.bmdl_name +
                             " exceeds max length " + std::to_string(*f.max_length) + "\");");
                else if (f.is_bytes)
                    ctx.line("if (v.length > " + std::to_string(*f.max_length) +
                             ") throw new ConduitCodecException(\"" + f.bmdl_name +
                             " exceeds max length " + std::to_string(*f.max_length) + "\");");
            }
            ctx.line(f.name + " = v;");
            ctx.dedent();
            ctx.line("}");
        } else {
            ctx.line("public void set" + acc + "(" + f.j_type + " v) { " + f.name + " = v; }");
        }

        // Raw accessors for scaled fields (matching C++ emit_plain_accessors raw section)
        if (f.has_scale) {
            std::string raw_type = (f.raw_bits <= 32 && !f.raw_signed) ? "int" :
                                   (f.raw_bits <= 32 && f.raw_signed)  ? "int" : "long";
            std::string scale_s = j_double(f.scale);
            std::string offset_s = j_double(f.offset);
            if (f.offset != 0.0) {
                ctx.line("public " + raw_type + " get" + acc + "Raw() { return (" + raw_type +
                         ")((" + f.name + " - " + offset_s + ") / " + scale_s + "); }");
            } else {
                ctx.line("public " + raw_type + " get" + acc + "Raw() { return (" + raw_type +
                         ")(" + f.name + " / " + scale_s + "); }");
            }
            ctx.line("public void set" + acc + "Raw(" + raw_type + " v) { " + f.name +
                     " = (double)(v) * " + scale_s + " + " + offset_s + "; }");
        }

        // Optional field helpers (matching C++ has_foo / clear_foo)
        if (f.is_optional) {
            ctx.line("public boolean has" + acc + "() { return " + f.name + " != null; }");
            ctx.line("public void clear" + acc + "() { " + f.name + " = null; }");
        }
    }
    ctx.line();

    // Build outer-scope decode parameters and context for this class
    auto osp_it = scope_map.find(name);
    std::string decode_params;
    JOuterContext outer_ctx;
    if (osp_it != scope_map.end()) {
        for (const auto& p : osp_it->second) {
            decode_params += ", " + p.java_type + " " + j_field(p.bmdl_name);
            outer_ctx[p.bmdl_name] = j_field(p.bmdl_name);
        }
    }

    // decode
    if (children.empty() && extra_fields.empty()) {
        ctx.line("public static " + cn + " decode(BitReader r" + decode_params + ") { return new " + cn + "(); }");
    } else {
        ctx.line("public static " + cn + " decode(BitReader r" + decode_params + ") {");
        ctx.indent();
        ctx.line(cn + " result = new " + cn + "();");
        { JBitTracker decode_tracker; emit_j_decode_children(ctx, children, index, "result", decode_tracker, scope_map, outer_ctx, name_map, cn); }
        ctx.line("return result;");
        ctx.dedent();
        ctx.line("}");
    }
    ctx.line();

    // decode from bytes (only when no outer-scope params — otherwise it's an inline type)
    if (osp_it == scope_map.end()) {
        ctx.line("public static " + cn + " decodeBytes(byte[] data) { return decode(new BitReader(data)); }");
        ctx.line();
    }

    // encode - with auto-length backpatch support
    if (children.empty() && extra_fields.empty()) {
        ctx.line("public void encode(BitWriter w) {}");
    } else {
    {
        // Check for auto-length fields
        const model::Field* auto_len_field = nullptr;
        const model::Field* auto_len_ref_field = nullptr;
        for (const auto& child : children) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                if (f->auto_expr && f->auto_expr->kind == model::AutoKind::Length) {
                    if (f->auto_expr->field_ref.empty()) {
                        auto_len_field = f;
                    } else {
                        auto_len_ref_field = f;
                    }
                }
            }
        }

        ctx.line("public void encode(BitWriter w) {");
        ctx.indent();
        if (auto_len_field) {
            ctx.line("int _structStart = w.sizeBytes();");
            ctx.line("int _lenPos = 0;");
        }
        std::string len_ref_target = (auto_len_ref_field && auto_len_ref_field->auto_expr)
            ? auto_len_ref_field->auto_expr->field_ref : "";
        if (auto_len_ref_field) {
            ctx.line("int _lenRefPos = 0;");
        }
        { JBitTracker encode_tracker; emit_j_encode_children(ctx, children, index, "this", encode_tracker, len_ref_target, auto_len_ref_field, name_map, cn); }

        // Backpatch auto-length (whole struct)
        if (auto_len_field && auto_len_field->auto_expr) {
            auto al_fi = j_resolve_field(*auto_len_field, index);
            bool be = (al_fi.endian == model::Endian::Big);
            std::string size_expr = "w.sizeBytes() - _structStart";
            // Apply arithmetic modifier if present
            if (auto_len_field->auto_expr->modifier.has_modifier()) {
                std::string op_str;
                switch (auto_len_field->auto_expr->modifier.op) {
                    case model::ArithOp::Add: op_str = " + "; break;
                    case model::ArithOp::Sub: op_str = " - "; break;
                    case model::ArithOp::Mul: op_str = " * "; break;
                    case model::ArithOp::Div: op_str = " / "; break;
                    case model::ArithOp::Mod: op_str = " % "; break;
                    default: break;
                }
                if (!op_str.empty()) {
                    size_expr = "((" + size_expr + ")" + op_str +
                                std::to_string(auto_len_field->auto_expr->modifier.literal) + ")";
                }
            }
            if (al_fi.bits <= 8) {
                ctx.line("w.patchU8(_lenPos, " + size_expr + ");");
            } else if (al_fi.bits <= 16) {
                ctx.line("w.patchU16(_lenPos, " + size_expr + ", " + (be ? "true" : "false") + ");");
            } else {
                ctx.line("w.patchU32(_lenPos, " + size_expr + ", " + (be ? "true" : "false") + ");");
            }
        }

        // Note: auto-length(field) backpatching is now emitted inline within
        // emit_j_encode_children, right after the target field is written.

        ctx.dedent();
        ctx.line("}");
        ctx.line();
    }

    // encodeBytes
    ctx.line("public byte[] encodeBytes() { BitWriter w = new BitWriter(); encode(w); return w.toBytes(); }");
    } // end empty-struct else
    ctx.line();

    // toMap
    ctx.line("public java.util.Map<String, Object> toMap() {");
    ctx.indent();
    ctx.line("java.util.Map<String, Object> m = new java.util.LinkedHashMap<>();");
    for (const auto& f : fields) {
        std::string key = f.bmdl_name.empty() ? f.name : f.bmdl_name;
        std::string val = f.name;
        if (f.j_type == "byte[]") {
            ctx.line("{ java.util.List<Integer> _bl = new java.util.ArrayList<>(); if (" + val + " != null) for (byte b : " + val + ") _bl.add(b & 0xFF); m.put(\"" + key + "\", _bl); }");
        } else if (f.is_enum) {
            ctx.line("m.put(\"" + key + "\", " + val + " != null ? " + val + ".value : null);");
        } else if (f.is_type_wrapper || (f.is_struct && f.is_string)) {
            // Type wrappers (string/scaled/flags/constrained) and string structs use .value()
            ctx.line("m.put(\"" + key + "\", " + val + " != null ? " + val + ".value() : null);");
        } else if (f.is_struct) {
            ctx.line("m.put(\"" + key + "\", " + val + " != null ? " + val + ".toMap() : null);");
        } else if (f.j_type.find("java.util.List") == 0) {
            // Extract element type from java.util.List<ElemType>
            std::string elem_type;
            auto lt_pos = f.j_type.find('<');
            if (lt_pos != std::string::npos && f.j_type.back() == '>') {
                elem_type = f.j_type.substr(lt_pos + 1, f.j_type.size() - lt_pos - 2);
            }
            if (!elem_type.empty() && f.is_list_of_type_wrappers) {
                ctx.line("{ java.util.List<Object> _al = new java.util.ArrayList<>(); if (" + val + " != null) for (" + elem_type + " _e : " + val + ") _al.add(_e != null ? _e.value() : null); m.put(\"" + key + "\", _al); }");
            } else if (!elem_type.empty()) {
                ctx.line("{ java.util.List<Object> _al = new java.util.ArrayList<>(); if (" + val + " != null) for (" + elem_type + " _e : " + val + ") _al.add(_e != null ? _e.toMap() : null); m.put(\"" + key + "\", _al); }");
            } else {
                ctx.line("m.put(\"" + key + "\", " + val + ");");
            }
        } else {
            ctx.line("m.put(\"" + key + "\", " + val + ");");
        }
    }
    ctx.line("return m;");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // fromMap
    ctx.line("@SuppressWarnings(\"unchecked\")");
    ctx.line("public static " + cn + " fromMap(java.util.Map<String, Object> m) {");
    ctx.indent();
    ctx.line(cn + " obj = new " + cn + "();");
    for (const auto& f : fields) {
        std::string key = f.bmdl_name.empty() ? f.name : f.bmdl_name;
        std::string target = "obj." + f.name;
        ctx.line("if (m.containsKey(\"" + key + "\")) {");
        ctx.indent();
        if (f.j_type == "byte[]") {
            ctx.line("Object _bv = m.get(\"" + key + "\"); if (_bv instanceof java.util.List) { java.util.List<?> _bl = (java.util.List<?>)_bv; byte[] _ba = new byte[_bl.size()]; for (int _i=0;_i<_bl.size();_i++) _ba[_i] = ((Number)_bl.get(_i)).byteValue(); " + target + " = _ba; }");
        } else if (f.is_enum) {
            ctx.line("Object _ev = m.get(\"" + key + "\"); if (_ev instanceof Number) { int _rv = ((Number)_ev).intValue(); for (" + f.j_type + " v : " + f.j_type + ".values()) { if (v.value == _rv) { " + target + " = v; break; } } }");
        } else if (f.is_string_wrapper) {
            // String type wrapper: construct from String value
            ctx.line("Object _sv = m.get(\"" + key + "\"); if (_sv instanceof String) " + target + " = new " + f.j_type + "((String)_sv);");
        } else if (f.is_scaled_wrapper) {
            // Scaled type wrapper: construct + setValue from double
            ctx.line("Object _sv = m.get(\"" + key + "\"); if (_sv instanceof Number) { " + target + " = new " + f.j_type + "(); " + target + ".setValue(((Number)_sv).doubleValue()); }");
        } else if (f.is_type_wrapper) {
            // Flags/constrained type wrapper: construct from raw long
            ctx.line("Object _sv = m.get(\"" + key + "\"); if (_sv instanceof Number) " + target + " = new " + f.j_type + "(((Number)_sv).longValue());");
        } else if (f.is_struct && f.is_string) {
            ctx.line("Object _sv = m.get(\"" + key + "\"); if (_sv instanceof String) " + target + " = new " + f.j_type + "((String)_sv);");
        } else if (f.is_struct) {
            ctx.line("Object _sv = m.get(\"" + key + "\"); if (_sv instanceof java.util.Map) " + target + " = " + f.j_type + ".fromMap((java.util.Map<String, Object>)_sv);");
        } else if (f.j_type.find("java.util.List") == 0) {
            // List fields: deserialize from List<Object>
            std::string elem_type;
            auto lt_pos = f.j_type.find('<');
            if (lt_pos != std::string::npos && f.j_type.back() == '>') {
                elem_type = f.j_type.substr(lt_pos + 1, f.j_type.size() - lt_pos - 2);
            }
            if (!elem_type.empty() && f.is_list_of_string_wrappers) {
                ctx.line("Object _lv = m.get(\"" + key + "\"); if (_lv instanceof java.util.List) { java.util.List<?> _sl = (java.util.List<?>)_lv; " + target + " = new java.util.ArrayList<>(); for (Object _e : _sl) if (_e instanceof String) " + target + ".add(new " + elem_type + "((String)_e)); }");
            } else if (!elem_type.empty() && f.is_list_of_scaled_wrappers) {
                ctx.line("Object _lv = m.get(\"" + key + "\"); if (_lv instanceof java.util.List) { java.util.List<?> _sl = (java.util.List<?>)_lv; " + target + " = new java.util.ArrayList<>(); for (Object _e : _sl) if (_e instanceof Number) { " + elem_type + " _tw = new " + elem_type + "(); _tw.setValue(((Number)_e).doubleValue()); " + target + ".add(_tw); } }");
            } else if (!elem_type.empty() && f.is_list_of_type_wrappers) {
                ctx.line("Object _lv = m.get(\"" + key + "\"); if (_lv instanceof java.util.List) { java.util.List<?> _sl = (java.util.List<?>)_lv; " + target + " = new java.util.ArrayList<>(); for (Object _e : _sl) if (_e instanceof Number) " + target + ".add(new " + elem_type + "(((Number)_e).longValue())); }");
            } else if (!elem_type.empty()) {
                ctx.line("Object _lv = m.get(\"" + key + "\"); if (_lv instanceof java.util.List) { java.util.List<?> _sl = (java.util.List<?>)_lv; " + target + " = new java.util.ArrayList<>(); for (Object _e : _sl) if (_e instanceof java.util.Map) " + target + ".add(" + elem_type + ".fromMap((java.util.Map<String, Object>)_e)); }");
            } else {
                ctx.line(target + " = m.get(\"" + key + "\");");
            }
        } else if (f.has_scale || f.j_type == "double" || f.j_type == "Double") {
            ctx.line("Object _nv = m.get(\"" + key + "\"); if (_nv instanceof Number) " + target + " = ((Number)_nv).doubleValue();");
        } else if (f.j_type == "float" || f.j_type == "Float") {
            ctx.line("Object _nv = m.get(\"" + key + "\"); if (_nv instanceof Number) " + target + " = ((Number)_nv).floatValue();");
        } else if (f.is_numeric) {
            if (f.j_type == "long" || f.j_type == "Long")
                ctx.line("Object _nv = m.get(\"" + key + "\"); if (_nv instanceof Number) " + target + " = ((Number)_nv).longValue();");
            else
                ctx.line("Object _nv = m.get(\"" + key + "\"); if (_nv instanceof Number) " + target + " = ((Number)_nv).intValue();");
        } else if (f.is_bool) {
            ctx.line("Object _bv = m.get(\"" + key + "\"); if (_bv instanceof Boolean) " + target + " = (Boolean)_bv;");
        } else if (f.is_string) {
            ctx.line("Object _sv = m.get(\"" + key + "\"); if (_sv instanceof String) " + target + " = (String)_sv;");
        } else {
            ctx.line(target + " = m.get(\"" + key + "\");");
        }
        ctx.dedent();
        ctx.line("}");
    }
    ctx.line("return obj;");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // toString
    ctx.line("@Override public String toString() { return toString(null); }");
    ctx.line();

    // toString with overrides (for formatOutbound — auto-managed fields like cat/len)
    ctx.line("public String toString(java.util.List<String[]> overrides) {");
    ctx.indent();
    if (fields.empty()) {
        ctx.line("return \"" + cn + "()\";");
    } else {
        ctx.line("StringBuilder sb = new StringBuilder(\"" + cn + "(\");");
        for (size_t i = 0; i < fields.size(); i++) {
            if (i > 0) ctx.line("sb.append(\", \");");
            std::string fname = fields[i].name;
            // Check overrides for this field
            ctx.line("sb.append(\"" + fname + "=\");");
            ctx.line("{");
            ctx.indent();
            ctx.line("String _ov = null;");
            ctx.line("if (overrides != null) { for (String[] kv : overrides) { if (kv[0].equals(\"" + fname + "\")) { _ov = kv[1]; break; } } }");
            ctx.line("if (_ov != null) { sb.append(_ov); }");
            if (fields[i].j_type == "byte[]")
                ctx.line("else { sb.append(java.util.Arrays.toString(" + fname + ")); }");
            else if (fields[i].is_numeric && fields[i].format == model::DisplayFormat::Hex)
                ctx.line("else { sb.append(\"0x\").append(Long.toHexString(" + fname + ")); }");
            else if (fields[i].is_numeric && fields[i].format == model::DisplayFormat::Octal)
                ctx.line("else { sb.append(\"0\").append(Long.toOctalString(" + fname + ")); }");
            else if (fields[i].is_numeric && fields[i].format == model::DisplayFormat::Binary)
                ctx.line("else { sb.append(\"0b\").append(Long.toBinaryString(" + fname + ")); }");
            else
                ctx.line("else { sb.append(" + fname + "); }");
            ctx.dedent();
            ctx.line("}");
        }
        ctx.line("sb.append(\")\");");
        ctx.line("return sb.toString();");
    }
    ctx.dedent();
    ctx.line("}");

    // equals()
    ctx.line();
    ctx.line("@Override public boolean equals(Object o) {");
    ctx.indent();
    ctx.line("if (this == o) return true;");
    ctx.line("if (!(o instanceof " + cn + ")) return false;");
    ctx.line(cn + " that = (" + cn + ") o;");
    if (fields.empty()) {
        ctx.line("return true;");
    } else {
        std::string eq_expr;
        for (size_t i = 0; i < fields.size(); i++) {
            if (i > 0) eq_expr += " && ";
            if (fields[i].j_type == "byte[]") {
                eq_expr += "java.util.Arrays.equals(" + fields[i].name + ", that." + fields[i].name + ")";
            } else if (fields[i].is_numeric && !fields[i].is_optional) {
                eq_expr += fields[i].name + " == that." + fields[i].name;
            } else {
                eq_expr += "java.util.Objects.equals(" + fields[i].name + ", that." + fields[i].name + ")";
            }
        }
        ctx.line("return " + eq_expr + ";");
    }
    ctx.dedent();
    ctx.line("}");

    // hashCode()
    ctx.line();
    ctx.line("@Override public int hashCode() {");
    ctx.indent();
    if (fields.empty()) {
        ctx.line("return 0;");
    } else {
        // Collect hash components -- use Arrays.hashCode for byte[], otherwise Objects.hash
        bool has_byte_array = false;
        for (const auto& f : fields) {
            if (f.j_type == "byte[]") { has_byte_array = true; break; }
        }
        if (has_byte_array) {
            ctx.line("int result = 17;");
            for (const auto& f : fields) {
                if (f.j_type == "byte[]") {
                    ctx.line("result = 31 * result + java.util.Arrays.hashCode(" + f.name + ");");
                } else if (f.is_numeric && !f.is_optional) {
                    if (f.j_type == "long" || f.j_type == "Long") {
                        ctx.line("result = 31 * result + Long.hashCode(" + f.name + ");");
                    } else {
                        ctx.line("result = 31 * result + Integer.hashCode(" + f.name + ");");
                    }
                } else {
                    ctx.line("result = 31 * result + java.util.Objects.hashCode(" + f.name + ");");
                }
            }
            ctx.line("return result;");
        } else {
            std::string args;
            for (size_t i = 0; i < fields.size(); i++) {
                if (i > 0) args += ", ";
                args += fields[i].name;
            }
            ctx.line("return java.util.Objects.hash(" + args + ");");
        }
    }
    ctx.dedent();
    ctx.line("}");

    // validate()
    {
        bool has_any = false;
        // Check if any field has a deferred constraint
        std::function<bool(const std::vector<model::StructChild>&)> has_constraints =
            [&](const std::vector<model::StructChild>& cs) -> bool {
            for (const auto& c : cs) {
                if (auto* f = std::get_if<model::Field>(&c)) {
                    if (f->constraint &&
                        f->constraint->validate == model::ValidateTiming::Deferred &&
                        (f->constraint->equals || f->constraint->min || f->constraint->max))
                        return true;
                } else if (auto* fx = std::get_if<model::FxBlock>(&c)) {
                    if (has_constraints(fx->children)) return true;
                }
            }
            return false;
        };
        has_any = has_constraints(children);
        if (has_any) {
            ctx.line();
            ctx.line("public void validate() {");
            ctx.indent();
            std::function<void(const std::vector<model::StructChild>&, bool)> emit_checks =
                [&](const std::vector<model::StructChild>& cs, bool nullable) {
                for (const auto& child : cs) {
                    if (auto* f = std::get_if<model::Field>(&child)) {
                        if (!f->constraint) continue;
                        const auto& con = *f->constraint;
                        if (con.validate != model::ValidateTiming::Deferred) continue;
                        if (!con.equals && !con.min && !con.max) continue;
                        // Skip non-numeric fields (struct, enum, string, bytes)
                        auto fi = j_resolve_field(*f, index);
                        if (fi.is_struct || fi.is_enum || fi.is_string || fi.is_bytes) continue;
                        std::string m = j_field(f->name);
                        if (nullable) {
                            ctx.line("if (" + m + " != null) {");
                            ctx.indent();
                        }
                        if (con.equals) {
                            ctx.line("if (" + m + " != " + j_qualify_const(*con.equals) +
                                     ") throw new ConduitCodecException(\"" + f->name +
                                     ": expected " + *con.equals + "\");");
                        }
                        if (con.max) {
                            ctx.line("if (" + m + " > " + j_qualify_const(*con.max) +
                                     ") throw new ConduitCodecException(\"" + f->name +
                                     " exceeds max " + *con.max + "\");");
                        }
                        if (con.min && (*con.min != "0" || fi.is_signed)) {
                            ctx.line("if (" + m + " < " + j_qualify_const(*con.min) +
                                     ") throw new ConduitCodecException(\"" + f->name +
                                     " below min " + *con.min + "\");");
                        }
                        if (nullable) {
                            ctx.dedent();
                            ctx.line("}");
                        }
                    } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
                        emit_checks(fx->children, true);
                    }
                }
            };
            emit_checks(children, false);
            ctx.dedent();
            ctx.line("}");
        }
    }

    ctx.dedent();
    ctx.line("}");
    return ctx.str();
}

// ============================================================================
// Protocol.java
// ============================================================================

std::string generate_j_protocol(const model::Protocol& protocol,
                                 const std::vector<analyzer::SessionInfo>& sessions,
                                 const std::string& pkg) {
    EmitContext ctx;
    ctx.line("// Generated by bgen - DO NOT EDIT");
    ctx.line("package " + pkg + ";");
    ctx.line();
    ctx.line("public final class Protocol {");
    ctx.indent();
    ctx.line("public static final String NAME = \"" + protocol.name + "\";");
    ctx.line("public static final String VERSION = \"" + protocol.version + "\";");
    ctx.line();

    // Type registry
    ctx.line("public static final class TypeInfo {");
    ctx.indent();
    ctx.line("private final long typeId;");
    ctx.line("private final String typeName;");
    ctx.line("public TypeInfo(long typeId, String typeName) { this.typeId = typeId; this.typeName = typeName; }");
    ctx.line("public long typeId() { return typeId; }");
    ctx.line("public String typeName() { return typeName; }");
    ctx.dedent();
    ctx.line("}");
    ctx.line();
    ctx.line("public static final TypeInfo[] TYPES = {");
    ctx.indent();
    std::set<uint64_t> seen;
    for (const auto& si : sessions)
        for (const auto& lt : si.leaf_types)
            if (!seen.count(lt.type_id)) {
                seen.insert(lt.type_id);
                ctx.line("new TypeInfo(" + j_hex64(lt.type_id) + ", \"" + lt.name + "\"),");
            }
    ctx.dedent();
    ctx.line("};");
    ctx.line();

    ctx.line("public static TypeInfo findById(long typeId) {");
    ctx.indent();
    ctx.line("for (TypeInfo t : TYPES) if (t.typeId() == typeId) return t;");
    ctx.line("return null;");
    ctx.dedent();
    ctx.line("}");
    ctx.line();
    ctx.line("public static TypeInfo findByName(String name) {");
    ctx.indent();
    ctx.line("for (TypeInfo t : TYPES) if (t.typeName().equals(name)) return t;");
    ctx.line("return null;");
    ctx.dedent();
    ctx.line("}");

    ctx.dedent();
    ctx.line("}");
    return ctx.str();
}

// ============================================================================
// Collect inline struct/array types that need their own .java files
// ============================================================================

// Recursively collect inline struct/array/choice types that need their own .java files.
// current_bmdl_name: the BMDL name of the current parent (for outer-scope analysis keyed by BMDL names)
// current_resolved_name: the resolved class name of the current parent (for child prefixing)
void collect_inline_types(const std::vector<model::StructChild>& children,
                          const analyzer::TypeIndex& index,
                          const std::string& pkg,
                          const std::unordered_map<std::string, uint64_t>& tid_map,
                          JOuterScopeMap& scope_map,
                          const std::string& current_bmdl_name,
                          std::vector<std::pair<std::string, std::string>>& out_files,
                          JInlineNameMap& name_map,
                          const std::string& current_resolved_name = {}) {
    // Use resolved name for child prefixing; fall back to PascalCase of BMDL name
    const std::string& prefix = current_resolved_name.empty()
        ? current_bmdl_name : current_resolved_name;
    for (const auto& child : children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            // Generate Java enum file for inline enum fields
            if (!f->enum_values.empty() && f->type_ref.empty()) {
                std::string enum_name = j_class(prefix) + j_class(f->name);
                int bits = f->bits.value_or(8);
                bool use_long = (bits > 32);
                std::string val_type = use_long ? "long" : "int";
                std::string lit_suffix = use_long ? "L" : "";
                EmitContext tctx;
                tctx.line("// Generated by bgen - DO NOT EDIT");
                tctx.line("package " + pkg + ";");
                tctx.line();
                tctx.line("import " + pkg + ".codec.*;");
                tctx.line();
                tctx.line("public enum " + enum_name + " {");
                tctx.indent();
                for (size_t i = 0; i < f->enum_values.size(); i++) {
                    std::string comma = (i + 1 < f->enum_values.size()) ? "," : ";";
                    tctx.line(j_const(f->enum_values[i].name) + "(" + std::to_string(f->enum_values[i].id) + lit_suffix + ")" + comma);
                }
                tctx.line();
                tctx.line("public final " + val_type + " value;");
                tctx.line(enum_name + "(" + val_type + " v) { this.value = v; }");
                tctx.line();
                tctx.line("public static " + enum_name + " decode(BitReader r) {");
                tctx.indent();
                tctx.line(val_type + " raw = " + (use_long ? "" : "(int) ") + "r.readBits(" + std::to_string(bits) + ");");
                tctx.line("for (" + enum_name + " v : values()) if (v.value == raw) return v;");
                tctx.line("throw new ConduitCodecException(\"unknown " + enum_name + " value: \" + raw);");
                tctx.dedent();
                tctx.line("}");
                tctx.line();
                tctx.line("public void encode(BitWriter w) { w.writeBits(value, " + std::to_string(bits) + "); }");
                tctx.dedent();
                tctx.line("}");
                out_files.push_back({enum_name + ".java", tctx.str()});
            }
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            std::string resolved = j_resolve_inline_name(sd->name, prefix, sd->type_name);
            name_map[sd->name] = resolved;
            j_analyze_outer_scope(sd->name, sd->children, children, index, current_bmdl_name, scope_map);
            collect_inline_types(sd->children, index, pkg, tid_map, scope_map, sd->name, out_files, name_map, resolved);
            // Restore our mapping in case a recursive descendant with the same BMDL name
            // (e.g., nested "items" inside "re") overwrote it in the shared name_map.
            name_map[sd->name] = resolved;
            std::string code;
            if (sd->is_bitmap) {
                code = generate_j_bitmap_class(*sd, index, pkg, tid_map, scope_map, name_map, resolved);
            } else {
                code = generate_j_class(sd->name, sd->children, index, pkg, tid_map, {}, scope_map, name_map, resolved, {}, sd->doc);
            }
            out_files.push_back({resolved + ".java", code});
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            if (ad->type_ref.empty() && !ad->children.empty()) {
                std::string resolved = j_resolve_inline_name(ad->name, prefix, ad->type_name);
                name_map[ad->name] = resolved;
                collect_inline_types(ad->children, index, pkg, tid_map, scope_map, ad->name, out_files, name_map, resolved);
                name_map[ad->name] = resolved;
                std::string code = generate_j_class(ad->name, ad->children, index, pkg, tid_map, {}, scope_map, name_map, resolved);
                out_files.push_back({resolved + ".java", code});
            }
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            for (const auto& cs : cd->cases) {
                if (cs.type_ref.empty() && !cs.children.empty()) {
                    std::string resolved = j_resolve_inline_name(cs.name, prefix, cs.type_name);
                    name_map[cs.name] = resolved;
                    j_analyze_outer_scope(cs.name, cs.children, children, index, current_bmdl_name, scope_map);
                    collect_inline_types(cs.children, index, pkg, tid_map, scope_map, cs.name, out_files, name_map, resolved);
                    name_map[cs.name] = resolved;
                    std::string code = generate_j_class(cs.name, cs.children, index, pkg, tid_map, {}, scope_map, name_map, resolved);
                    out_files.push_back({resolved + ".java", code});
                }
            }
            if (cd->otherwise && cd->otherwise->type_ref.empty() && !cd->otherwise->children.empty()) {
                std::string resolved = j_resolve_inline_name(cd->otherwise->name, prefix, cd->otherwise->type_name);
                name_map[cd->otherwise->name] = resolved;
                j_analyze_outer_scope(cd->otherwise->name, cd->otherwise->children, children, index, current_bmdl_name, scope_map);
                collect_inline_types(cd->otherwise->children, index, pkg, tid_map, scope_map, cd->otherwise->name, out_files, name_map, resolved);
                name_map[cd->otherwise->name] = resolved;
                std::string code = generate_j_class(cd->otherwise->name, cd->otherwise->children, index, pkg, tid_map, {}, scope_map, name_map, resolved);
                out_files.push_back({resolved + ".java", code});
            }
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            collect_inline_types(fx->children, index, pkg, tid_map, scope_map, current_bmdl_name, out_files, name_map, prefix);
        }
    }
}

// ============================================================================
// Java Frame class generation
// ============================================================================

std::string generate_j_frame_class(const analyzer::SessionInfo& si,
                                    const analyzer::TypeIndex& index,
                                    const std::string& pkg) {
    if (!si.frame) return {};
    const model::FrameDef& frame = *si.frame;
    std::string cn = j_class(frame.name);

    std::set<std::string> type_imports;
    for (const auto& lt : si.leaf_types) type_imports.insert(j_class(lt.name));
    type_imports.erase(cn);

    EmitContext ctx;
    ctx.line("// Generated by bgen - DO NOT EDIT");
    ctx.line("package " + pkg + ";");
    ctx.line();
    j_emit_imports(ctx, pkg + ".codec", pkg, type_imports);
    ctx.line("public final class " + cn + " {");
    ctx.indent();

    // Header field declarations
    for (const auto& child : frame.header_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fi = j_resolve_field(*f, index);
            std::string init;
            if (fi.is_string) init = "\"\"";
            else if (fi.is_bytes) init = "new byte[0]";
            else if (fi.is_bool) init = "false";
            else if (fi.j_type == "float") init = "0.0f";
            else if (fi.j_type == "double") init = "0.0";
            else if (fi.is_struct || fi.is_enum) init = "null";
            else if (fi.j_type == "long") init = "0L";
            else init = "0";
            ctx.line("public " + fi.j_type + " " + j_field(f->name) + " = " + init + ";");
        }
    }

    // Payload field
    if (si.payload_is_array) {
        ctx.line("public java.util.List<Object> payload = new java.util.ArrayList<>();");
    } else {
        ctx.line("public Object payload = null;");
    }

    // Footer field declarations
    for (const auto& child : frame.footer_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fi = j_resolve_field(*f, index);
            std::string init;
            if (fi.is_string) init = "\"\"";
            else if (fi.is_bytes) init = "new byte[0]";
            else if (fi.is_bool) init = "false";
            else if (fi.j_type == "float") init = "0.0f";
            else if (fi.j_type == "double") init = "0.0";
            else if (fi.is_struct || fi.is_enum) init = "null";
            else if (fi.j_type == "long") init = "0L";
            else init = "0";
            ctx.line("public " + fi.j_type + " " + j_field(f->name) + " = " + init + ";");
        }
    }
    ctx.line();

    // wrap() static methods — one per leaf type
    for (const auto& lt : si.leaf_types) {
        std::string leaf_class = j_class(lt.name);
        ctx.line("public static " + cn + " wrap(" + leaf_class + " msg) {");
        ctx.indent();
        ctx.line(cn + " frame = new " + cn + "();");
        // Set constraint-equals header fields
        for (const auto& child : frame.header_fields) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                if (f->constraint && f->constraint->equals) {
                    ctx.line("frame." + j_field(f->name) + " = " + j_qualify_const(*f->constraint->equals) + ";");
                }
            }
        }
        // Set constraint-equals footer fields
        for (const auto& child : frame.footer_fields) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                if (f->constraint && f->constraint->equals) {
                    ctx.line("frame." + j_field(f->name) + " = " + j_qualify_const(*f->constraint->equals) + ";");
                }
            }
        }
        // Set id field from message's ID_VALUE
        if (!si.id_field_name.empty()) {
            ctx.line("frame." + j_field(si.id_field_name) + " = " + leaf_class + ".ID_VALUE;");
        }
        if (si.payload_is_array) {
            ctx.line("frame.payload.add(msg);");
        } else {
            ctx.line("frame.payload = msg;");
        }
        ctx.line("return frame;");
        ctx.dedent();
        ctx.line("}");
        ctx.line();
    }

    // encode() method
    ctx.line("public void encode(BitWriter w) {");
    ctx.indent();

    // Find the length field for backpatching
    const model::Field* length_field = nullptr;
    bool length_is_total_frame = false;
    bool length_is_payload = false;
    for (const auto& child : frame.header_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            if (f->auto_expr && f->auto_expr->kind == model::AutoKind::Length) {
                length_field = f;
                length_is_total_frame = f->auto_expr->field_ref.empty();
                length_is_payload = (!f->auto_expr->field_ref.empty() && f->auto_expr->field_ref == "payload");
            }
        }
    }

    if (length_is_total_frame) {
        ctx.line("int _frameStart = w.sizeBytes();");
    }

    // Write header fields
    for (const auto& child : frame.header_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fi = j_resolve_field(*f, index);
            if (f->auto_expr && f->auto_expr->kind == model::AutoKind::Length) {
                ctx.line("int _lenPos = w.sizeBytes();");
                ctx.line(j_write_stmt("0", fi) + ";");
                if (length_is_payload) {
                    ctx.line("int _payloadStart = w.sizeBytes();");
                }
            } else if (f->auto_expr && f->auto_expr->kind == model::AutoKind::Count) {
                if (f->auto_expr->field_ref == "payload" && si.payload_is_array) {
                    ctx.line(j_write_stmt("payload.size()", fi) + ";");
                } else {
                    ctx.line(j_write_stmt("this." + j_field(f->name), fi) + ";");
                }
            } else if (f->constraint && f->constraint->equals) {
                ctx.line(j_write_stmt(j_qualify_const(*f->constraint->equals), fi) + ";");
            } else {
                std::string val = "this." + j_field(f->name);
                if (fi.is_enum) {
                    val = val + ".value";
                }
                ctx.line(j_write_stmt(val, fi) + ";");
            }
        } else if (auto* res = std::get_if<model::Reserved>(&child)) {
            ctx.line("w.writeBits(0, " + std::to_string(res->bits) + ");");
        }
    }

    // Write payload
    if (si.payload_is_array) {
        ctx.line("for (Object item : payload) {");
        ctx.indent();
        bool first = true;
        for (const auto& lt : si.leaf_types) {
            std::string leaf_class = j_class(lt.name);
            ctx.line(std::string(first ? "if" : "} else if") + " (item instanceof " + leaf_class + ") {");
            ctx.indent();
            ctx.line(leaf_class + " _m = (" + leaf_class + ") item;");
            ctx.line("_m.encode(w);");
            ctx.dedent();
            first = false;
        }
        if (!first) ctx.line("}");
        ctx.dedent();
        ctx.line("}");
    } else {
        bool first = true;
        for (const auto& lt : si.leaf_types) {
            std::string leaf_class = j_class(lt.name);
            ctx.line(std::string(first ? "if" : "} else if") + " (payload instanceof " + leaf_class + ") {");
            ctx.indent();
            ctx.line(leaf_class + " _m = (" + leaf_class + ") payload;");
            ctx.line("_m.encode(w);");
            ctx.dedent();
            first = false;
        }
        if (!first) ctx.line("}");
    }

    // Write footer fields
    for (const auto& child : frame.footer_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fi = j_resolve_field(*f, index);
            if (f->constraint && f->constraint->equals) {
                // Constraint-equals: always write the constraint value
                ctx.line(j_write_stmt(j_qualify_const(*f->constraint->equals), fi) + ";");
            } else {
                std::string val = "this." + j_field(f->name);
                if (fi.is_enum) val = val + ".value";
                ctx.line(j_write_stmt(val, fi) + ";");
            }
        } else if (auto* res = std::get_if<model::Reserved>(&child)) {
            ctx.line("w.writeBits(0, " + std::to_string(res->bits) + ");");
        }
    }

    // Backpatch length
    if (length_field) {
        auto al_fi = j_resolve_field(*length_field, index);
        bool be = (al_fi.endian == model::Endian::Big);
        std::string raw_length;
        if (length_is_payload) {
            raw_length = "w.sizeBytes() - _payloadStart";
        } else {
            raw_length = "w.sizeBytes() - _frameStart";
        }
        // Apply arithmetic modifier
        if (length_field->auto_expr->modifier.has_modifier()) {
            std::string op_str;
            switch (length_field->auto_expr->modifier.op) {
                case model::ArithOp::Add: op_str = " + "; break;
                case model::ArithOp::Sub: op_str = " - "; break;
                case model::ArithOp::Mul: op_str = " * "; break;
                case model::ArithOp::Div: op_str = " / "; break;
                case model::ArithOp::Mod: op_str = " % "; break;
                default: break;
            }
            if (!op_str.empty()) {
                raw_length = "((" + raw_length + ")" + op_str +
                             std::to_string(length_field->auto_expr->modifier.literal) + ")";
            }
        }
        if (al_fi.bits <= 8) {
            ctx.line("w.patchU8(_lenPos, " + raw_length + ");");
        } else if (al_fi.bits <= 16) {
            ctx.line("w.patchU16(_lenPos, " + raw_length + ", " + (be ? "true" : "false") + ");");
        } else {
            ctx.line("w.patchU32(_lenPos, " + raw_length + ", " + (be ? "true" : "false") + ");");
        }
    }

    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // encodeBytes()
    ctx.line("public byte[] encodeBytes() { BitWriter w = new BitWriter(); encode(w); return w.toBytes(); }");
    ctx.line();

    // decode() static method
    ctx.line("public static " + cn + " decode(BitReader r) {");
    ctx.indent();
    ctx.line(cn + " result = new " + cn + "();");

    // Read header fields
    for (const auto& child : frame.header_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fi = j_resolve_field(*f, index);
            std::string m = "result." + j_field(f->name);
            if (fi.is_enum) {
                ctx.line(m + " = " + fi.j_type + ".decode(r);");
            } else if (fi.is_bool) {
                ctx.line(m + " = (" + j_read_expr(fi) + " != 0);");
            } else {
                ctx.line(m + " = " + (j_needs_int_cast(fi) ? "(int) " : "") + j_read_expr(fi) + ";");
            }
        } else if (auto* res = std::get_if<model::Reserved>(&child)) {
            ctx.line("r.skipBits(" + std::to_string(res->bits) + ");");
        }
    }

    // Compute header/footer sizes for payload bounding
    int header_bits = 0;
    for (const auto& child : frame.header_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fi = j_resolve_field(*f, index);
            header_bits += fi.bits;
        } else if (auto* r = std::get_if<model::Reserved>(&child)) {
            header_bits += r->bits;
        }
    }
    int footer_bits = 0;
    for (const auto& child : frame.footer_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fi = j_resolve_field(*f, index);
            footer_bits += fi.bits;
        } else if (auto* r = std::get_if<model::Reserved>(&child)) {
            footer_bits += r->bits;
        }
    }
    int header_bytes = (header_bits + 7) / 8;
    int footer_bytes = (footer_bits + 7) / 8;

    // Build payload sub-reader when bounded by length field
    bool use_sub_reader = false;
    if (!si.length_field_name.empty() && si.count_field_name.empty()) {
        std::string len_member = "result." + j_field(si.length_field_name);
        std::string raw_val = len_member;
        // Reverse arithmetic modifier
        std::string total_expr = raw_val;
        if (si.frame_length_modifier.has_modifier()) {
            switch (si.frame_length_modifier.op) {
                case model::ArithOp::Add:
                    total_expr = "(" + raw_val + " - " + std::to_string(si.frame_length_modifier.literal) + ")";
                    break;
                case model::ArithOp::Sub:
                    total_expr = "(" + raw_val + " + " + std::to_string(si.frame_length_modifier.literal) + ")";
                    break;
                case model::ArithOp::Mul:
                    total_expr = "(" + raw_val + " / " + std::to_string(si.frame_length_modifier.literal) + ")";
                    break;
                case model::ArithOp::Div:
                    total_expr = "(" + raw_val + " * " + std::to_string(si.frame_length_modifier.literal) + ")";
                    break;
                default: break;
            }
        }
        std::string size_expr;
        if (si.frame_length_field_ref.empty()) {
            // Total frame length: payload = total - header - footer
            int overhead = header_bytes + footer_bytes;
            size_expr = total_expr + " - " + std::to_string(overhead);
        } else {
            // Payload-only length
            if (footer_bytes > 0) {
                size_expr = total_expr + " - " + std::to_string(footer_bytes);
            } else {
                size_expr = total_expr;
            }
        }
        ctx.line("BitReader payloadReader = r.subReader(" + size_expr + ");");
        use_sub_reader = true;
    } else if (footer_bits > 0 && si.length_field_name.empty() && si.count_field_name.empty()) {
        ctx.line("BitReader payloadReader = r.subReader(r.remainingBytes() - " + std::to_string(footer_bytes) + ");");
        use_sub_reader = true;
    }

    std::string reader_name = use_sub_reader ? "payloadReader" : "r";

    // Dispatch on id field to decode payload
    std::string id_field = "result." + j_field(si.id_field_name);

    if (si.payload_is_array) {
        // Array payload: decode records
        if (!si.count_field_name.empty()) {
            ctx.line("for (int _i = 0; _i < result." + j_field(si.count_field_name) + "; _i++) {");
        } else {
            ctx.line("while (" + reader_name + ".remainingBytes() > 0) {");
        }
        ctx.indent();
        bool first = true;
        for (const auto& lt : si.leaf_types) {
            if (lt.send_only) continue;
            std::string leaf_class = j_class(lt.name);
            std::string id_val = lt.constraints.empty() ? "0" : lt.constraints[0].second;
            ctx.line(std::string(first ? "if" : "} else if") + " (" + id_field + " == " + id_val + ") {");
            ctx.indent();
            ctx.line(leaf_class + " _msg = " + leaf_class + ".decode(" + reader_name + ");");
            // Copy header fields into decoded message
            for (const auto& child : frame.header_fields) {
                if (auto* f = std::get_if<model::Field>(&child)) {
                    ctx.line("_msg." + j_field(f->name) + " = result." + j_field(f->name) + ";");
                }
            }
            ctx.line("result.payload.add(_msg);");
            ctx.dedent();
            first = false;
        }
        if (!first) ctx.line("}");
        ctx.dedent();
        ctx.line("}");
    } else {
        // Single payload dispatch
        bool first = true;
        // Group leaves by id to handle direction pairs
        std::map<std::string, std::vector<const analyzer::LeafTypeInfo*>> id_groups;
        for (const auto& lt : si.leaf_types) {
            std::string id_val = lt.constraints.empty() ? "0" : lt.constraints[0].second;
            id_groups[id_val].push_back(&lt);
        }
        for (const auto& [id_val, leaves] : id_groups) {
            const analyzer::LeafTypeInfo* decode_leaf = leaves[0];
            for (const auto* lt : leaves) {
                if (!lt->send_only) { decode_leaf = lt; break; }
            }
            std::string leaf_class = j_class(decode_leaf->name);
            ctx.line(std::string(first ? "if" : "} else if") + " (" + id_field + " == " + id_val + ") {");
            ctx.indent();
            ctx.line(leaf_class + " _msg = " + leaf_class + ".decode(" + reader_name + ");");
            // Copy header fields into decoded message
            for (const auto& child : frame.header_fields) {
                if (auto* f = std::get_if<model::Field>(&child)) {
                    ctx.line("_msg." + j_field(f->name) + " = result." + j_field(f->name) + ";");
                }
            }
            ctx.line("result.payload = _msg;");
            ctx.dedent();
            first = false;
        }
        if (!first) {
            ctx.line("} else {");
            ctx.indent();
            ctx.line("throw new ConduitCodecException(\"unknown message id: \" + " + id_field + ");");
            ctx.dedent();
            ctx.line("}");
        }
    }

    // Read footer fields
    for (const auto& child : frame.footer_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fi = j_resolve_field(*f, index);
            std::string m = "result." + j_field(f->name);
            if (fi.is_enum) {
                ctx.line(m + " = " + fi.j_type + ".decode(r);");
            } else if (fi.is_bool) {
                ctx.line(m + " = (" + j_read_expr(fi) + " != 0);");
            } else {
                ctx.line(m + " = " + (j_needs_int_cast(fi) ? "(int) " : "") + j_read_expr(fi) + ";");
            }
        } else if (auto* res = std::get_if<model::Reserved>(&child)) {
            ctx.line("r.skipBits(" + std::to_string(res->bits) + ");");
        }
    }

    // Copy footer fields into decoded payload messages
    if (footer_bits > 0) {
        if (si.payload_is_array) {
            ctx.line("for (Object item : result.payload) {");
            ctx.indent();
            for (const auto& lt : si.leaf_types) {
                std::string leaf_class = j_class(lt.name);
                ctx.line("if (item instanceof " + leaf_class + ") {");
                ctx.indent();
                ctx.line(leaf_class + " _fm = (" + leaf_class + ") item;");
                for (const auto& child : frame.footer_fields) {
                    if (auto* f = std::get_if<model::Field>(&child)) {
                        ctx.line("_fm." + j_field(f->name) + " = result." + j_field(f->name) + ";");
                    }
                }
                ctx.dedent();
                ctx.line("}");
            }
            ctx.dedent();
            ctx.line("}");
        } else {
            for (const auto& lt : si.leaf_types) {
                std::string leaf_class = j_class(lt.name);
                ctx.line("if (result.payload instanceof " + leaf_class + ") {");
                ctx.indent();
                ctx.line(leaf_class + " _fm = (" + leaf_class + ") result.payload;");
                for (const auto& child : frame.footer_fields) {
                    if (auto* f = std::get_if<model::Field>(&child)) {
                        ctx.line("_fm." + j_field(f->name) + " = result." + j_field(f->name) + ";");
                    }
                }
                ctx.dedent();
                ctx.line("}");
            }
        }
    }

    ctx.line("return result;");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // decodeBytes()
    ctx.line("public static " + cn + " decodeBytes(byte[] data) { return decode(new BitReader(data)); }");
    ctx.line();

    // toString()
    ctx.line("@Override public String toString() {");
    ctx.indent();
    std::string fmt = "return \"" + cn + "(\" + ";
    bool first = true;
    for (const auto& child : frame.header_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            if (!first) fmt += " + \", \" + ";
            fmt += "\"" + j_field(f->name) + "=\" + " + j_field(f->name);
            first = false;
        }
    }
    if (!first) fmt += " + \", \" + ";
    fmt += "\"payload=\" + payload";
    first = false;
    for (const auto& child : frame.footer_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            fmt += " + \", \" + ";
            fmt += "\"" + j_field(f->name) + "=\" + " + j_field(f->name);
        }
    }
    fmt += " + \")\";";
    ctx.line(fmt);
    ctx.dedent();
    ctx.line("}");

    ctx.dedent();
    ctx.line("}");
    return ctx.str();
}

// ============================================================================
// Java Session class generation
// ============================================================================

std::string generate_j_session_class(const model::Protocol& protocol,
                                      const analyzer::SessionInfo& si,
                                      [[maybe_unused]] const analyzer::TypeIndex& index,
                                      const std::string& pkg) {
    if (!si.frame) return {};
    std::string frame_class = j_class(si.frame->name);
    std::string session_class = frame_class + "Session";

    // Merge config fields (frame-level + message-level, deduplicated)
    std::map<std::string, analyzer::ConfigField> all_config;
    for (const auto& cf : si.config_fields)
        all_config.try_emplace(cf.key, cf);
    for (const auto& lt : si.leaf_types)
        for (const auto& cf : lt.config_fields)
            all_config.try_emplace(cf.key, cf);
    bool has_config = !all_config.empty();

    // Check if any leaf has auto-increment fields
    bool has_auto_fields = false;
    for (const auto& lt : si.leaf_types)
        if (!lt.auto_fields.empty()) { has_auto_fields = true; break; }

    std::set<std::string> type_imports;
    for (const auto& lt : si.leaf_types) type_imports.insert(j_class(lt.name));
    type_imports.insert(frame_class);
    type_imports.erase(session_class);

    EmitContext ctx;
    ctx.line("// Generated by bgen - DO NOT EDIT");
    ctx.line("package " + pkg + ";");
    ctx.line();
    j_emit_imports(ctx, pkg + ".codec", pkg, type_imports);
    ctx.line("public final class " + session_class + " {");
    ctx.indent();

    // LEAF_TYPES map (use Map.ofEntries for >10 entries to avoid Map.of() limit)
    if (si.leaf_types.size() > 10) {
        ctx.line("public static final Map<Long, String> LEAF_TYPES = Map.ofEntries(");
        ctx.indent();
        for (size_t i = 0; i < si.leaf_types.size(); i++) {
            const auto& lt = si.leaf_types[i];
            std::string comma = (i + 1 < si.leaf_types.size()) ? "," : "";
            ctx.line("Map.entry(" + j_hex64(lt.type_id) + ", \"" + lt.name + "\")" + comma);
        }
        ctx.dedent();
        ctx.line(");");
    } else {
        ctx.line("public static final Map<Long, String> LEAF_TYPES = Map.of(");
        ctx.indent();
        for (size_t i = 0; i < si.leaf_types.size(); i++) {
            const auto& lt = si.leaf_types[i];
            std::string comma = (i + 1 < si.leaf_types.size()) ? "," : "";
            ctx.line(j_hex64(lt.type_id) + ", \"" + lt.name + "\"" + comma);
        }
        ctx.dedent();
        ctx.line(");");
    }
    ctx.line();

    // Private state
    ctx.line("private long sequenceCounter = 0;");
    if (has_config) {
        ctx.line("private final Map<String, Object> config;");
    }
    ctx.line();

    // Constructor
    if (has_config) {
        ctx.line("public " + session_class + "(Map<String, Object> config) {");
        ctx.indent();
        ctx.line("this.config = config != null ? config : new HashMap<>();");
        ctx.dedent();
        ctx.line("}");
        ctx.line();
        ctx.line("public " + session_class + "() { this(null); }");
    } else {
        ctx.line("public " + session_class + "() {}");
    }
    ctx.line();

    // typeName
    ctx.line("public String typeName(long typeId) {");
    ctx.indent();
    ctx.line("return LEAF_TYPES.getOrDefault(typeId, \"unknown\");");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // leafTypeIds
    ctx.line("public long[] leafTypeIds() {");
    ctx.indent();
    ctx.line("return new long[] {");
    ctx.indent();
    for (size_t i = 0; i < si.leaf_types.size(); i++) {
        const auto& lt = si.leaf_types[i];
        std::string comma = (i + 1 < si.leaf_types.size()) ? "," : "";
        ctx.line(j_hex64(lt.type_id) + comma + " // " + lt.name);
    }
    ctx.dedent();
    ctx.line("};");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // protocolName
    ctx.line("public String protocolName() { return \"" + protocol.name + "\"; }");
    ctx.line();

    // isReceiveOnly
    {
        bool has_recv = false;
        for (const auto& lt : si.leaf_types)
            if (lt.receive_only) { has_recv = true; break; }
        ctx.line("public boolean isReceiveOnly(long typeId) {");
        ctx.indent();
        if (has_recv) {
            for (const auto& lt : si.leaf_types) {
                if (lt.receive_only) {
                    ctx.line("if (typeId == " + j_hex64(lt.type_id) + ") return true; // " + lt.name);
                }
            }
        }
        ctx.line("return false;");
        ctx.dedent();
        ctx.line("}");
        ctx.line();
    }

    // syncPattern
    ctx.line("public byte[] syncPattern() {");
    ctx.indent();
    if (!si.sync_pattern.empty()) {
        std::string bytes;
        for (size_t i = 0; i < si.sync_pattern.size(); i++) {
            if (i > 0) bytes += ", ";
            char buf[16];
            std::snprintf(buf, sizeof(buf), "(byte)0x%02x", si.sync_pattern[i]);
            bytes += buf;
        }
        ctx.line("return new byte[] { " + bytes + " };");
    } else {
        ctx.line("return new byte[0];");
    }
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // minFrameHeaderSize
    ctx.line("public int minFrameHeaderSize() { return " + std::to_string(si.min_frame_header_size) + "; }");
    ctx.line();

    // extractFrameLength
    ctx.line("public int extractFrameLength(byte[] header) {");
    ctx.indent();
    if (si.frame_length_bits > 0) {
        ctx.line("if (header.length < " + std::to_string(si.min_frame_header_size) + ") return 0;");
        ctx.line("BitReader r = new BitReader(header);");
        if (si.frame_length_bit_offset > 0) {
            ctx.line("r.skipBits(" + std::to_string(si.frame_length_bit_offset) + ");");
        }
        bool big = (si.frame_length_endian == model::Endian::Big);
        std::string read_call;
        if (si.frame_length_bits <= 8) {
            read_call = "r.readU8()";
        } else if (si.frame_length_bits <= 16) {
            read_call = std::string("r.readU16(") + (big ? "true" : "false") + ")";
        } else if (si.frame_length_bits <= 32) {
            read_call = std::string("r.readU32(") + (big ? "true" : "false") + ")";
        } else {
            read_call = std::string("(int) r.readU64(") + (big ? "true" : "false") + ")";
        }
        ctx.line("int val = " + read_call + ";");
        if (!si.frame_length_field_ref.empty() && si.frame_length_field_ref == "payload") {
            int overhead = (int)(si.min_frame_header_size + si.frame_footer_size);
            if (si.frame_length_modifier.has_modifier()) {
                std::string val_expr = "val";
                switch (si.frame_length_modifier.op) {
                    case model::ArithOp::Add:
                        val_expr = "(val - " + std::to_string(si.frame_length_modifier.literal) + ")";
                        break;
                    case model::ArithOp::Sub:
                        val_expr = "(val + " + std::to_string(si.frame_length_modifier.literal) + ")";
                        break;
                    case model::ArithOp::Mul:
                        val_expr = "(val / " + std::to_string(si.frame_length_modifier.literal) + ")";
                        break;
                    case model::ArithOp::Div:
                        val_expr = "(val * " + std::to_string(si.frame_length_modifier.literal) + ")";
                        break;
                    default: break;
                }
                ctx.line("return " + val_expr + " + " + std::to_string(overhead) + ";");
            } else {
                ctx.line("return val + " + std::to_string(overhead) + ";");
            }
        } else if (si.frame_length_modifier.has_modifier()) {
            std::string val_expr = "val";
            switch (si.frame_length_modifier.op) {
                case model::ArithOp::Add:
                    val_expr = "(val - " + std::to_string(si.frame_length_modifier.literal) + ")";
                    break;
                case model::ArithOp::Sub:
                    val_expr = "(val + " + std::to_string(si.frame_length_modifier.literal) + ")";
                    break;
                case model::ArithOp::Mul:
                    val_expr = "(val / " + std::to_string(si.frame_length_modifier.literal) + ")";
                    break;
                case model::ArithOp::Div:
                    val_expr = "(val * " + std::to_string(si.frame_length_modifier.literal) + ")";
                    break;
                default: break;
            }
            ctx.line("return " + val_expr + ";");
        } else {
            ctx.line("return val;");
        }
    } else {
        ctx.line("return header.length;");
    }
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // decodeFrame
    ctx.line("public List<Map<String, Object>> decodeFrame(byte[] data) {");
    ctx.indent();
    ctx.line(frame_class + " frame;");
    ctx.line("try { frame = " + frame_class + ".decodeBytes(data); }");
    ctx.line("catch (Exception e) { return new ArrayList<>(); }");
    ctx.line("List<Map<String, Object>> messages = new ArrayList<>();");
    if (si.payload_is_array) {
        ctx.line("for (Object item : frame.payload) {");
        ctx.indent();
        bool first = true;
        for (const auto& lt : si.leaf_types) {
            std::string leaf_class = j_class(lt.name);
            ctx.line(std::string(first ? "if" : "} else if") + " (item instanceof " + leaf_class + ") {");
            ctx.indent();
            ctx.line(leaf_class + " _m = (" + leaf_class + ") item;");
            ctx.line("Map<String, Object> dm = new HashMap<>();");
            ctx.line("dm.put(\"type_id\", " + j_hex64(lt.type_id) + ");");
            ctx.line("dm.put(\"type_name\", \"" + lt.name + "\");");
            ctx.line("dm.put(\"payload\", _m);");
            ctx.line("dm.put(\"raw\", data);");
            ctx.line("messages.add(dm);");
            ctx.dedent();
            first = false;
        }
        if (!first) ctx.line("}");
        ctx.dedent();
        ctx.line("}");
    } else {
        bool first = true;
        for (const auto& lt : si.leaf_types) {
            std::string leaf_class = j_class(lt.name);
            ctx.line(std::string(first ? "if" : "} else if") + " (frame.payload instanceof " + leaf_class + ") {");
            ctx.indent();
            ctx.line(leaf_class + " _m = (" + leaf_class + ") frame.payload;");
            ctx.line("Map<String, Object> dm = new HashMap<>();");
            ctx.line("dm.put(\"type_id\", " + j_hex64(lt.type_id) + ");");
            ctx.line("dm.put(\"type_name\", \"" + lt.name + "\");");
            ctx.line("dm.put(\"payload\", _m);");
            ctx.line("dm.put(\"raw\", data);");
            ctx.line("messages.add(dm);");
            ctx.dedent();
            first = false;
        }
        if (!first) ctx.line("}");
    }

    // Warn when receiving send-only message types
    bool has_send_only = false;
    for (const auto& lt : si.leaf_types)
        if (lt.send_only) { has_send_only = true; break; }
    if (has_send_only) {
        ctx.line("for (Map<String, Object> dm : messages) {");
        ctx.indent();
        ctx.line("long tid = (Long) dm.get(\"type_id\");");
        for (const auto& lt : si.leaf_types) {
            if (lt.send_only) {
                ctx.line("if (tid == " + j_hex64(lt.type_id) + ") {");
                ctx.indent();
                ctx.line("System.err.println(\"WARNING: Received send-only message type '" + lt.name + "'\");");
                ctx.dedent();
                ctx.line("}");
            }
        }
        ctx.dedent();
        ctx.line("}");
    }

    ctx.line("return messages;");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // encodeWrap
    ctx.line("public Map<String, Object> encodeWrap(long typeId, Object payload) {");
    ctx.indent();
    {
        bool first_branch = true;
        for (const auto& lt : si.leaf_types) {
            std::string leaf_class = j_class(lt.name);
            std::string prefix = first_branch ? "if" : "} else if";
            first_branch = false;
            ctx.line(prefix + " (typeId == " + j_hex64(lt.type_id) + ") {");
            ctx.indent();
            ctx.line(leaf_class + " msg = (" + leaf_class + ") payload;");
            ctx.line("List<String[]> autoFields = new ArrayList<>();");

            // Set message-level config fields
            if (!lt.config_fields.empty() && has_config) {
                for (const auto& cf : lt.config_fields) {
                    std::string cfg_key = cf.key;
                    std::string cast = "((Number) config.getOrDefault(\"" + cfg_key + "\", 0))";
                    if (cf.bits <= 32) cast += ".intValue()";
                    else cast += ".longValue()";
                    ctx.line("msg." + j_field(cf.field_name) + " = " + cast + ";");
                    ctx.line("autoFields.add(new String[]{\"" + cf.field_name + "\", String.valueOf(msg." + j_field(cf.field_name) + ")});");
                }
            }

            ctx.line(frame_class + " frame = " + frame_class + ".wrap(msg);");

            // Set frame-level config fields
            if (has_config) {
                for (const auto& cf : si.config_fields) {
                    std::string cfg_key = cf.key;
                    std::string cast;
                    if (cf.bits > 32) {
                        cast = "((Number) config.getOrDefault(\"" + cfg_key + "\", 0)).longValue()";
                    } else {
                        cast = "((Number) config.getOrDefault(\"" + cfg_key + "\", 0)).intValue()";
                    }
                    ctx.line("frame." + j_field(cf.field_name) + " = " + cast + ";");
                    ctx.line("autoFields.add(new String[]{\"" + cf.field_name + "\", String.valueOf(frame." + j_field(cf.field_name) + ")});");
                }
            }

            // Set auto-increment fields
            for (size_t ai = 0; ai < lt.auto_fields.size(); ++ai) {
                int bits = (ai < lt.auto_field_bits.size()) ? lt.auto_field_bits[ai] : 8;
                uint64_t mask_val = (bits >= 64) ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
                std::string field = j_field(lt.auto_fields[ai]);
                if (bits > 32) {
                    ctx.line("{ long seqVal = sequenceCounter & " + std::to_string(mask_val) + "L;");
                    ctx.line("  frame." + field + " = seqVal;");
                } else {
                    ctx.line("{ int seqVal = (int)(sequenceCounter & " + std::to_string(mask_val) + "L);");
                    ctx.line("  frame." + field + " = seqVal;");
                }
                ctx.line("  sequenceCounter++;");
                ctx.line("  autoFields.add(new String[]{\"" + lt.auto_fields[ai] + "\", String.valueOf(seqVal)}); }");
            }

            // Set auto-timestamp fields
            for (size_t ti = 0; ti < lt.timestamp_fields.size(); ++ti) {
                int bits = (ti < lt.timestamp_field_bits.size()) ? lt.timestamp_field_bits[ti] : 32;
                uint64_t mask_val = (bits >= 64) ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
                std::string field = j_field(lt.timestamp_fields[ti]);
                if (bits > 32) {
                    ctx.line("{ long tsVal = System.currentTimeMillis() & " + std::to_string(mask_val) + "L;");
                    ctx.line("  frame." + field + " = tsVal;");
                } else {
                    ctx.line("{ int tsVal = (int)(System.currentTimeMillis() & " + std::to_string(mask_val) + "L);");
                    ctx.line("  frame." + field + " = tsVal;");
                }
                ctx.line("  autoFields.add(new String[]{\"" + lt.timestamp_fields[ti] + "\", String.valueOf(tsVal)}); }");
            }

            // Record auto-id field
            if (!si.id_field_name.empty()) {
                ctx.line("autoFields.add(new String[]{\"" + si.id_field_name + "\", String.valueOf(" + leaf_class + ".ID_VALUE)});");
            }

            ctx.line("byte[] encoded = frame.encodeBytes();");

            // Record auto-length (wire value, with arithmetic modifier applied)
            if (!si.length_field_name.empty()) {
                std::string len_expr = "encoded.length";
                if (si.frame_length_modifier.has_modifier()) {
                    std::string op;
                    switch (si.frame_length_modifier.op) {
                        case model::ArithOp::Add: op = " + "; break;
                        case model::ArithOp::Sub: op = " - "; break;
                        case model::ArithOp::Mul: op = " * "; break;
                        case model::ArithOp::Div: op = " / "; break;
                        default: break;
                    }
                    if (!op.empty())
                        len_expr = "(" + len_expr + op + std::to_string(si.frame_length_modifier.literal) + ")";
                }
                ctx.line("autoFields.add(new String[]{\"" + si.length_field_name + "\", String.valueOf(" + len_expr + ")});");
            }

            ctx.line("Map<String, Object> result = new HashMap<>();");
            ctx.line("result.put(\"bytes\", encoded);");
            ctx.line("result.put(\"type_id\", typeId);");
            ctx.line("result.put(\"auto_fields\", autoFields);");
            ctx.line("return result;");
            ctx.dedent();
        }
        if (!first_branch) {
            ctx.line("}");
        }
    }
    ctx.line("return null;");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // encodeBatch — only for array-payload sessions
    if (si.payload_is_array) {
        ctx.line("public Map<String, Object> encodeBatch(long typeId, List<?> payloads) {");
        ctx.indent();
        {
            bool first = true;
            for (const auto& lt : si.leaf_types) {
                std::string leaf_class = j_class(lt.name);
                std::string prefix = first ? "if" : "} else if";
                first = false;
                ctx.line(prefix + " (typeId == " + j_hex64(lt.type_id) + ") {");
                ctx.indent();
                ctx.line(frame_class + " frame = new " + frame_class + "();");

                // Set constraint-equals header fields
                if (si.frame) {
                    for (const auto& hc : si.frame->header_fields) {
                        if (auto* f = std::get_if<model::Field>(&hc)) {
                            if (f->constraint && f->constraint->equals) {
                                ctx.line("frame." + j_field(f->name) + " = " + j_qualify_const(*f->constraint->equals) + ";");
                            }
                        }
                    }
                    // Set constraint-equals footer fields
                    for (const auto& fc : si.frame->footer_fields) {
                        if (auto* f = std::get_if<model::Field>(&fc)) {
                            if (f->constraint && f->constraint->equals) {
                                ctx.line("frame." + j_field(f->name) + " = " + j_qualify_const(*f->constraint->equals) + ";");
                            }
                        }
                    }
                }

                // Set id field
                if (!si.id_field_name.empty()) {
                    ctx.line("frame." + j_field(si.id_field_name) + " = " + leaf_class + ".ID_VALUE;");
                }

                // Set message-level config fields on copies
                if (!lt.config_fields.empty() && has_config) {
                    ctx.line("for (Object p : payloads) {");
                    ctx.indent();
                    ctx.line(leaf_class + " m = (" + leaf_class + ") p;");
                    for (const auto& cf : lt.config_fields) {
                        std::string cfg_key = cf.key;
                        std::string cast;
                        if (cf.bits > 32) {
                            cast = "((Number) config.getOrDefault(\"" + cfg_key + "\", 0)).longValue()";
                        } else {
                            cast = "((Number) config.getOrDefault(\"" + cfg_key + "\", 0)).intValue()";
                        }
                        ctx.line("m." + j_field(cf.field_name) + " = " + cast + ";");
                    }
                    ctx.line("frame.payload.add(m);");
                    ctx.dedent();
                    ctx.line("}");
                } else {
                    ctx.line("for (Object p : payloads) frame.payload.add(p);");
                }

                // Set frame-level config fields
                if (has_config) {
                    for (const auto& cf : si.config_fields) {
                        std::string cfg_key = cf.key;
                        std::string cast;
                        if (cf.bits > 32) {
                            cast = "((Number) config.getOrDefault(\"" + cfg_key + "\", 0)).longValue()";
                        } else {
                            cast = "((Number) config.getOrDefault(\"" + cfg_key + "\", 0)).intValue()";
                        }
                        ctx.line("frame." + j_field(cf.field_name) + " = " + cast + ";");
                    }
                }

                ctx.line("List<String[]> autoFields = new ArrayList<>();");

                // Set auto-increment fields
                for (size_t ai = 0; ai < lt.auto_fields.size(); ++ai) {
                    int bits = (ai < lt.auto_field_bits.size()) ? lt.auto_field_bits[ai] : 8;
                    uint64_t mask_val = (bits >= 64) ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
                    std::string field = j_field(lt.auto_fields[ai]);
                    if (bits > 32) {
                        ctx.line("{ long seqVal = sequenceCounter & " + std::to_string(mask_val) + "L;");
                        ctx.line("  frame." + field + " = seqVal;");
                    } else {
                        ctx.line("{ int seqVal = (int)(sequenceCounter & " + std::to_string(mask_val) + "L);");
                        ctx.line("  frame." + field + " = seqVal;");
                    }
                    ctx.line("  sequenceCounter++;");
                    ctx.line("  autoFields.add(new String[]{\"" + lt.auto_fields[ai] + "\", String.valueOf(seqVal)}); }");
                }

                // Set auto-timestamp fields
                for (size_t ti = 0; ti < lt.timestamp_fields.size(); ++ti) {
                    int bits = (ti < lt.timestamp_field_bits.size()) ? lt.timestamp_field_bits[ti] : 32;
                    uint64_t mask_val = (bits >= 64) ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
                    std::string field = j_field(lt.timestamp_fields[ti]);
                    if (bits > 32) {
                        ctx.line("{ long tsVal = System.currentTimeMillis() & " + std::to_string(mask_val) + "L;");
                        ctx.line("  frame." + field + " = tsVal;");
                    } else {
                        ctx.line("{ int tsVal = (int)(System.currentTimeMillis() & " + std::to_string(mask_val) + "L);");
                        ctx.line("  frame." + field + " = tsVal;");
                    }
                    ctx.line("  autoFields.add(new String[]{\"" + lt.timestamp_fields[ti] + "\", String.valueOf(tsVal)}); }");
                }

                // Record auto-id field
                if (!si.id_field_name.empty()) {
                    ctx.line("autoFields.add(new String[]{\"" + si.id_field_name + "\", String.valueOf(" + leaf_class + ".ID_VALUE)});");
                }

                ctx.line("byte[] encoded = frame.encodeBytes();");

                // Record auto-length (wire value, with arithmetic modifier applied)
                if (!si.length_field_name.empty()) {
                    std::string len_expr = "encoded.length";
                    if (si.frame_length_modifier.has_modifier()) {
                        std::string op;
                        switch (si.frame_length_modifier.op) {
                            case model::ArithOp::Add: op = " + "; break;
                            case model::ArithOp::Sub: op = " - "; break;
                            case model::ArithOp::Mul: op = " * "; break;
                            case model::ArithOp::Div: op = " / "; break;
                            default: break;
                        }
                        if (!op.empty())
                            len_expr = "(" + len_expr + op + std::to_string(si.frame_length_modifier.literal) + ")";
                    }
                    ctx.line("autoFields.add(new String[]{\"" + si.length_field_name + "\", String.valueOf(" + len_expr + ")});");
                }

                ctx.line("Map<String, Object> result = new HashMap<>();");
                ctx.line("result.put(\"bytes\", encoded);");
                ctx.line("result.put(\"type_id\", typeId);");
                ctx.line("result.put(\"auto_fields\", autoFields);");
                ctx.line("return result;");
                ctx.dedent();
            }
            if (!first) ctx.line("}");
        }
        ctx.line("return null;");
        ctx.dedent();
        ctx.line("}");
        ctx.line();
    }

    // formatMessage
    ctx.line("public String formatMessage(long typeId, Object payload) {");
    ctx.indent();
    {
        bool first = true;
        for (const auto& lt : si.leaf_types) {
            std::string leaf_class = j_class(lt.name);
            std::string prefix = first ? "if" : "} else if";
            first = false;
            ctx.line(prefix + " (typeId == " + j_hex64(lt.type_id) + " && payload instanceof " + leaf_class + ") {");
            ctx.indent();
            ctx.line(leaf_class + " _m = (" + leaf_class + ") payload;");
            ctx.line("return _m.toString();");
            ctx.dedent();
        }
        if (!first) ctx.line("}");
    }
    ctx.line("return payload != null ? payload.toString() : \"\";");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // formatOutbound
    ctx.line("public String formatOutbound(long typeId, Object payload, List<String[]> autoFields) {");
    ctx.indent();
    {
        bool first = true;
        for (const auto& lt : si.leaf_types) {
            std::string leaf_class = j_class(lt.name);
            std::string prefix = first ? "if" : "} else if";
            first = false;
            ctx.line(prefix + " (typeId == " + j_hex64(lt.type_id) + " && payload instanceof " + leaf_class + ") {");
            ctx.indent();
            ctx.line(leaf_class + " _m = (" + leaf_class + ") payload;");
            ctx.line("return _m.toString(autoFields);");
            ctx.dedent();
        }
        if (!first) ctx.line("}");
    }
    ctx.line("return payload != null ? payload.toString() : \"\";");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // reset
    ctx.line("public void reset() {");
    ctx.indent();
    ctx.line("sequenceCounter = 0;");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // sequenceCounter accessor
    if (has_auto_fields) {
        ctx.line("public long sequenceCounter() { return sequenceCounter; }");
        ctx.line();
    }

    ctx.dedent();
    ctx.line("}");
    return ctx.str();
}

} // anonymous namespace

// ============================================================================
// JavaBackend::generate
// ============================================================================

bool JavaBackend::generate(
    const model::Protocol& protocol,
    const analyzer::TypeIndex& index,
    const analyzer::WireSizeInfo& sizes,
    const std::vector<analyzer::SessionInfo>& sessions,
    const std::string& ns,
    const std::filesystem::path& output_dir) {

    std::string pkg = ns.empty() ? "io.conduit.gen" : ns;
    // Replace :: and hyphens for Java package naming
    std::string java_pkg;
    for (size_t i = 0; i < pkg.size(); i++) {
        char c = pkg[i];
        if (c == ':') c = '.';
        else if (c == '-') c = '_';
        // Skip consecutive dots
        if (c == '.' && !java_pkg.empty() && java_pkg.back() == '.') continue;
        java_pkg += c;
    }
    // Strip leading/trailing dots
    while (!java_pkg.empty() && java_pkg.front() == '.') java_pkg.erase(java_pkg.begin());
    while (!java_pkg.empty() && java_pkg.back() == '.') java_pkg.pop_back();
    if (java_pkg.empty()) java_pkg = "io.conduit.gen";

    // Build package subdirectory path (e.g. "io.conduit.asterix" -> "io/conduit/asterix")
    std::filesystem::path pkg_rel;
    {
        std::string seg;
        for (char c : java_pkg) {
            if (c == '.') { if (!seg.empty()) { pkg_rel /= seg; seg.clear(); } }
            else seg += c;
        }
        if (!seg.empty()) pkg_rel /= seg;
    }
    auto pkg_dir = output_dir / pkg_rel;
    std::filesystem::create_directories(pkg_dir);

    // Codec sub-package (BitReader, BitWriter, ConduitCodecException)
    auto codec_dir = pkg_dir / "codec";
    std::filesystem::create_directories(codec_dir);

    bool ok = true;

    // Codec infrastructure files (in <pkg>.codec subpackage)
    ok &= write_file(codec_dir / "BitReader.java", generate_j_bit_reader(java_pkg));
    ok &= write_file(codec_dir / "BitWriter.java", generate_j_bit_writer(java_pkg));
    ok &= write_file(codec_dir / "ConduitCodecException.java", generate_j_exception(java_pkg));

    // Protocol metadata files (in <pkg>)
    ok &= write_file(pkg_dir / "Constants.java", generate_j_constants(protocol, java_pkg));
    ok &= write_file(pkg_dir / "Protocol.java", generate_j_protocol(protocol, sessions, java_pkg));

    int file_count = 5;

    // Type wrapper files (one per type)
    {
        for (const auto& t : protocol.types) {
            bool is_enum = !t.enum_values.empty();
            bool is_flags = !t.flags.empty();
            bool has_scale = t.scale.has_value() || t.offset.has_value();
            bool is_string = (t.base == model::PrimitiveBase::String);
            // Generate wrapper for all type aliases (enum, flags, scaled, string,
            // constrained, and plain integer types used as array elements).
            {
                std::string name = j_class(t.name);
                // Generate individual file for this type
                EmitContext tctx;
                tctx.line("// Generated by bgen - DO NOT EDIT");
                tctx.line("package " + java_pkg + ";");
                tctx.line();
                tctx.line("import " + java_pkg + ".codec.*;");
                tctx.line();

                if (is_string && t.length) {
                    // String type wrapper class
                    bool type_has_enc = (t.encoding == model::StringEncoding::Ia5 || t.encoding == model::StringEncoding::Ebcdic);
                    std::string type_enc_arg;
                    if (type_has_enc) {
                        type_enc_arg = (t.encoding == model::StringEncoding::Ia5) ? ", 1" : ", 2";
                    }
                    bool is_ebcdic = (t.encoding == model::StringEncoding::Ebcdic);
                    int pad = (t.padding == model::StringPadding::Space) ? (is_ebcdic ? 0x40 : 0x20) : 0;
                    tctx.line("import java.nio.charset.StandardCharsets;");
                    tctx.line();
                    j_emit_doc(tctx, t.doc);
                    tctx.line("public final class " + name + " {");
                    tctx.indent();
                    tctx.line("public static final int WIRE_SIZE = " + std::to_string(*t.length) + ";");
                    tctx.line("private String value;");
                    tctx.line("public " + name + "() { this.value = \"\"; }");
                    tctx.line("public " + name + "(String value) { this.value = value; }");
                    tctx.line("public String value() { return value; }");
                    tctx.line("public void setValue(String v) { value = v; }");
                    tctx.line();
                    tctx.line("public static " + name + " decode(BitReader r) {");
                    tctx.indent();
                    if (t.char_bits) {
                        tctx.line("String s = r.readPackedChars(" + std::to_string(*t.length) + ", " + std::to_string(*t.char_bits) + ");");
                    } else if (type_has_enc) {
                        tctx.line("String s = r.readStringEncoded(" + std::to_string(*t.length) + type_enc_arg + ");");
                    } else {
                        tctx.line("String s = r.readString(" + std::to_string(*t.length) + ");");
                    }
                    // Apply trim settings
                    if (t.trim == model::StringTrim::Right || t.trim == model::StringTrim::Both) {
                        std::string ch = (t.padding == model::StringPadding::Space) ? " " : "\\0";
                        tctx.line("int end = s.length(); while (end > 0 && s.charAt(end - 1) == '" + ch + "') end--;");
                        tctx.line("s = s.substring(0, end);");
                    }
                    if (t.trim == model::StringTrim::Left || t.trim == model::StringTrim::Both) {
                        std::string ch = (t.padding == model::StringPadding::Space) ? " " : "\\0";
                        tctx.line("int start = 0; while (start < s.length() && s.charAt(start) == '" + ch + "') start++;");
                        tctx.line("s = s.substring(start);");
                    }
                    tctx.line("return new " + name + "(s);");
                    tctx.dedent();
                    tctx.line("}");
                    tctx.line();
                    if (t.char_bits) {
                        tctx.line("public void encode(BitWriter w) { w.writePackedChars(value, " + std::to_string(*t.length) + ", " + std::to_string(*t.char_bits) + ", " + std::to_string(pad) + "); }");
                    } else if (type_has_enc) {
                        tctx.line("public void encode(BitWriter w) { w.writeStringEncoded(value, " + std::to_string(*t.length) + ", " + std::to_string(pad) + type_enc_arg + "); }");
                    } else {
                        tctx.line("public void encode(BitWriter w) { w.writeString(value, " + std::to_string(*t.length) + ", " + std::to_string(pad) + "); }");
                    }
                    tctx.dedent();
                    tctx.line("}");
                } else if (is_enum) {
                    j_emit_doc(tctx, t.doc);
                    tctx.line("public enum " + name + " {");
                    tctx.indent();
                    {
                        bool use_long_val = (t.bits > 32);
                        std::string lit_suffix = use_long_val ? "L" : "";
                        for (size_t i = 0; i < t.enum_values.size(); i++) {
                            std::string comma = (i + 1 < t.enum_values.size()) ? "," : ";";
                            tctx.line(j_const(t.enum_values[i].name) + "(" + std::to_string(t.enum_values[i].id) + lit_suffix + ")" + comma);
                        }
                    }
                    tctx.line();
                    {
                        bool use_long = (t.bits > 32);
                        std::string val_type = use_long ? "long" : "int";
                        tctx.line("public final " + val_type + " value;");
                        tctx.line(name + "(" + val_type + " v) { this.value = v; }");
                        tctx.line();
                        tctx.line("public static " + name + " decode(BitReader r) {");
                        tctx.indent();
                        bool enum_signed = (t.base == model::PrimitiveBase::Int);
                        std::string enum_rd;
                        if (t.wire_encoding == model::WireEncoding::BCD) enum_rd = "readBcd";
                        else if (t.wire_encoding == model::WireEncoding::BCD_S) enum_rd = "readBcdSigned";
                        else if (t.wire_encoding == model::WireEncoding::BNR_S) enum_rd = "readSignMagnitude";
                        else enum_rd = enum_signed ? "readSignedBits" : "readBits";
                        std::string cast = use_long ? "" : "(int) ";
                        tctx.line(val_type + " raw = " + cast + "r." + enum_rd + "(" + std::to_string(t.bits) + ");");
                    }
                    tctx.line("for (" + name + " v : values()) if (v.value == raw) return v;");
                    tctx.line("throw new ConduitCodecException(\"unknown " + name + " value: \" + raw);");
                    tctx.dedent();
                    tctx.line("}");
                    tctx.line();
                    {
                        bool enum_signed = (t.base == model::PrimitiveBase::Int);
                        std::string enum_wr;
                        if (t.wire_encoding == model::WireEncoding::BCD) enum_wr = "writeBcd";
                        else if (t.wire_encoding == model::WireEncoding::BCD_S) enum_wr = "writeBcdSigned";
                        else if (t.wire_encoding == model::WireEncoding::BNR_S) enum_wr = "writeSignMagnitude";
                        else enum_wr = enum_signed ? "writeSignedBits" : "writeBits";
                        tctx.line("public void encode(BitWriter w) { w." + enum_wr + "(value, " + std::to_string(t.bits) + "); }");
                    }
                    tctx.dedent();
                    tctx.line("}");
                } else if (t.base == model::PrimitiveBase::Float) {
                    // Float TypeDef wrapper: use native float/double storage
                    bool use_double = (t.bits > 32);
                    std::string val_type = use_double ? "double" : "float";
                    std::string be_str = "true"; // TypeDef wrappers default to big-endian
                    j_emit_doc(tctx, t.doc);
                    tctx.line("public final class " + name + " {");
                    tctx.indent();
                    tctx.line("private " + val_type + " raw;");
                    tctx.line("public " + name + "() {}");
                    tctx.line("public " + name + "(" + val_type + " raw) { this.raw = raw; }");
                    tctx.line("public " + val_type + " raw() { return raw; }");
                    tctx.line("public void setRaw(" + val_type + " v) { raw = v; }");
                    tctx.line("public " + val_type + " value() { return raw; }");
                    tctx.line("public void setValue(" + val_type + " v) { raw = v; }");
                    // decode/encode using proper float read/write
                    std::string rd_expr;
                    std::string wr_stmt;
                    if (t.bits == 16) {
                        rd_expr = "r.readF16(" + be_str + ")";
                        wr_stmt = "w.writeF16(raw, " + be_str + ")";
                    } else if (t.bits <= 32) {
                        rd_expr = "r.readF32(" + be_str + ")";
                        wr_stmt = "w.writeF32(raw, " + be_str + ")";
                    } else if (t.bits <= 48) {
                        rd_expr = "r.readF48(" + be_str + ")";
                        wr_stmt = "w.writeF48(raw, " + be_str + ")";
                    } else if (t.bits <= 64) {
                        rd_expr = "r.readF64(" + be_str + ")";
                        wr_stmt = "w.writeF64(raw, " + be_str + ")";
                    } else {
                        rd_expr = "r.readF64(" + be_str + ")";
                        wr_stmt = "w.writeF64(raw, " + be_str + ")";
                    }
                    tctx.line("public static " + name + " decode(BitReader r) { return new " + name + "(" + rd_expr + "); }");
                    tctx.line("public void encode(BitWriter w) { " + wr_stmt + "; }");
                    tctx.line("@Override public String toString() { return \"" + name + "(\" + raw + \")\"; }");
                    tctx.dedent();
                    tctx.line("}");
                } else {
                    bool is_signed = (t.base == model::PrimitiveBase::Int);
                    j_emit_doc(tctx, t.doc);
                    tctx.line("public final class " + name + " {");
                    tctx.indent();
                    tctx.line("private long raw;");
                    tctx.line("public " + name + "() {}");
                    tctx.line("public " + name + "(long raw) { this.raw = raw; }");
                    tctx.line("public long raw() { return raw; }");
                    tctx.line("public void setRaw(long v) { raw = v; }");
                    if (is_flags) {
                        for (const auto& fl : t.flags) {
                            std::string bs = std::to_string(fl.bit);
                            tctx.line("public boolean " + j_field(fl.name) + "() { return ((raw >> " + bs + ") & 1) != 0; }");
                            tctx.line("public void set" + j_class(fl.name) + "(boolean v) { if (v) raw |= (1L<<" + bs + "); else raw &= ~(1L<<" + bs + "); }");
                        }
                    }
                    if (has_scale) {
                        std::string ve = "(double) raw";
                        if (t.scale) ve += " * " + j_double(*t.scale);
                        if (t.offset) ve += " + " + j_double(*t.offset);
                        tctx.line("public double value() { return " + ve + "; }");
                        // setValue: inverse of value(), matching C++ set_value(double)
                        std::string inv = "v";
                        if (t.offset) inv = "(" + inv + " - " + j_double(*t.offset) + ")";
                        if (t.scale) inv = "(" + inv + " / " + j_double(*t.scale) + ")";
                        tctx.line("public void setValue(double v) { raw = (long)" + inv + "; }");
                    } else {
                        tctx.line("public long value() { return raw; }");
                    }
                    // Determine read/write method based on wire encoding
                    std::string rd, wr;
                    if (t.wire_encoding == model::WireEncoding::BCD) {
                        rd = "readBcd"; wr = "writeBcd";
                    } else if (t.wire_encoding == model::WireEncoding::BCD_S) {
                        rd = "readBcdSigned"; wr = "writeBcdSigned";
                    } else if (t.wire_encoding == model::WireEncoding::BNR_S) {
                        rd = "readSignMagnitude"; wr = "writeSignMagnitude";
                    } else {
                        rd = is_signed ? "readSignedBits" : "readBits";
                        wr = is_signed ? "writeSignedBits" : "writeBits";
                    }
                    // Generate decode with constraint validation (matching C++ emit_constraint_check)
                    if (t.constraint && (t.constraint->max || (t.constraint->min && (*t.constraint->min != "0" || is_signed)) || t.constraint->equals)) {
                        tctx.line("public static " + name + " decode(BitReader r) {");
                        tctx.indent();
                        tctx.line("long raw = r." + rd + "(" + std::to_string(t.bits) + ");");
                        if (t.constraint->equals) {
                            tctx.line("if (raw != " + j_qualify_const(*t.constraint->equals) + ") throw new ConduitCodecException(\"" + name + " constraint violation: expected " + *t.constraint->equals + "\");");
                        }
                        if (t.constraint->max) {
                            tctx.line("if (raw > " + j_qualify_const(*t.constraint->max) + ") throw new ConduitCodecException(\"" + name + " exceeds max " + *t.constraint->max + "\");");
                        }
                        // Skip min=0 for unsigned types (always true)
                        if (t.constraint->min && (*t.constraint->min != "0" || is_signed)) {
                            tctx.line("if (raw < " + j_qualify_const(*t.constraint->min) + ") throw new ConduitCodecException(\"" + name + " below min " + *t.constraint->min + "\");");
                        }
                        tctx.line("return new " + name + "(raw);");
                        tctx.dedent();
                        tctx.line("}");
                    } else {
                        tctx.line("public static " + name + " decode(BitReader r) { return new " + name + "(r." + rd + "(" + std::to_string(t.bits) + ")); }");
                    }
                    tctx.line("public void encode(BitWriter w) { w." + wr + "(raw, " + std::to_string(t.bits) + "); }");
                    if (is_flags) {
                        tctx.line("@Override public String toString() { return \"" + name + "(0x\" + Long.toHexString(raw) + \")\"; }");
                    } else if (has_scale) {
                        tctx.line("@Override public String toString() { return \"" + name + "(\" + value() + \")\"; }");
                    } else {
                        tctx.line("@Override public String toString() { return \"" + name + "(\" + raw + \")\"; }");
                    }
                    tctx.dedent();
                    tctx.line("}");
                }
                ok &= write_file(pkg_dir / (name + ".java"), tctx.str());
                file_count++;
            }
        }
    }

    // Build type_id map
    std::unordered_map<std::string, uint64_t> tid_map;
    for (const auto& si : sessions)
        for (const auto& lt : si.leaf_types)
            tid_map[lt.name] = lt.type_id;

    // Struct classes (including inline children)
    std::unordered_map<std::string, uint64_t> empty;
    for (const auto& sd : protocol.structs) {
        // Generate inline struct/array types first
        std::vector<std::pair<std::string, std::string>> inline_files;
        JOuterScopeMap scope_map;
        JInlineNameMap name_map;
        collect_inline_types(sd.children, index, java_pkg, empty, scope_map, sd.name, inline_files, name_map);
        for (const auto& [fname, fcode] : inline_files) {
            ok &= write_file(pkg_dir / fname, fcode);
            file_count++;
        }
        std::string code;
        if (sd.is_bitmap) {
            code = generate_j_bitmap_class(sd, index, java_pkg, empty, scope_map, name_map, {}, &sizes);
        } else {
            code = generate_j_class(sd.name, sd.children, index, java_pkg, empty, {}, scope_map, name_map, {}, {}, sd.doc, &sizes);
        }
        ok &= write_file(pkg_dir / (j_class(sd.name) + ".java"), code);
        file_count++;
    }

    // Collect frame header/footer fields that need to be injected into message classes
    std::unordered_map<std::string, std::vector<JFieldDef>> msg_frame_fields;
    for (const auto& si : sessions) {
        if (!si.is_frame_based || !si.frame) continue;
        std::vector<JFieldDef> frame_fields;
        auto add_frame_field = [&](const model::Field* f) {
            auto fi = j_resolve_field(*f, index);
            JFieldDef fd;
            fd.name = j_field(f->name);
            fd.bmdl_name = f->name;
            fd.j_type = fi.j_type;
            fd.is_numeric = !fi.is_struct && !fi.is_enum && !fi.is_string && !fi.is_bytes && !fi.is_bool &&
                            !fi.is_float && !fi.has_scale && (fi.bits > 0);
            fd.is_struct = fi.is_struct;
            fd.is_type_wrapper = fi.is_type_wrapper;
            fd.is_string_wrapper = fi.is_string_wrapper;
            fd.is_scaled_wrapper = fi.is_scaled_wrapper;
            fd.is_string = fi.is_string;
            fd.is_bytes = fi.is_bytes;
            fd.is_bool = fi.is_bool;
            fd.is_enum = fi.is_enum;
            fd.has_scale = fi.has_scale;
            if (fi.is_string) fd.init = "\"\"";
            else if (fi.is_bytes) fd.init = "new byte[0]";
            else if (fi.is_bool) fd.init = "false";
            else if (fi.j_type == "long") fd.init = "0L";
            else fd.init = "0";
            frame_fields.push_back(fd);
        };
        for (const auto& child : si.frame->header_fields) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                add_frame_field(f);
            }
        }
        for (const auto& child : si.frame->footer_fields) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                add_frame_field(f);
            }
        }
        if (!frame_fields.empty()) {
            for (const auto& lt : si.leaf_types) {
                msg_frame_fields[lt.name] = frame_fields;
            }
        }
    }

    // Message classes (including inline children)
    for (const auto& md : protocol.messages) {
        std::vector<std::pair<std::string, std::string>> inline_files;
        JOuterScopeMap scope_map;
        JInlineNameMap name_map;
        collect_inline_types(md.children, index, java_pkg, tid_map, scope_map, md.name, inline_files, name_map);
        for (const auto& [fname, fcode] : inline_files) {
            ok &= write_file(pkg_dir / fname, fcode);
            file_count++;
        }
        auto mff_it = msg_frame_fields.find(md.name);
        std::vector<JFieldDef> extra_fields = (mff_it != msg_frame_fields.end()) ? mff_it->second : std::vector<JFieldDef>{};
        std::string code = generate_j_class(md.name, md.children, index, java_pkg, tid_map, md.id, scope_map, name_map, {}, extra_fields, md.doc, &sizes);
        ok &= write_file(pkg_dir / (j_class(md.name) + ".java"), code);
        file_count++;
    }

    // Frame classes (one per frame-based session)
    for (const auto& si : sessions) {
        if (si.is_frame_based && si.frame) {
            std::string code = generate_j_frame_class(si, index, java_pkg);
            ok &= write_file(pkg_dir / (j_class(si.frame->name) + ".java"), code);
            file_count++;
        }
    }

    // Session classes (one per frame-based session)
    for (const auto& si : sessions) {
        if (si.is_frame_based && si.frame) {
            std::string code = generate_j_session_class(protocol, si, index, java_pkg);
            std::string session_name = j_class(si.frame->name) + "Session";
            ok &= write_file(pkg_dir / (session_name + ".java"), code);
            file_count++;
        }
    }

    if (ok) Logger::info("generated " + std::to_string(file_count) + " Java files in " + pkg_dir.string());
    return ok;
}

} // namespace bgen::codegen
