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

std::string j_hex64(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%016llxL", static_cast<unsigned long long>(v));
    return buf;
}

// ============================================================================
// Java expression codegen
// ============================================================================

std::string j_expr(const model::Expr& e, const std::string& obj = "this") {
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
        case model::ExprOp::Add:    return "(" + j_expr(*e.left, obj) + " + " + j_expr(*e.right, obj) + ")";
        case model::ExprOp::Sub:    return "(" + j_expr(*e.left, obj) + " - " + j_expr(*e.right, obj) + ")";
        case model::ExprOp::Mul:    return "(" + j_expr(*e.left, obj) + " * " + j_expr(*e.right, obj) + ")";
        case model::ExprOp::Div:    return "(" + j_expr(*e.left, obj) + " / " + j_expr(*e.right, obj) + ")";
        case model::ExprOp::Mod:    return "(" + j_expr(*e.left, obj) + " % " + j_expr(*e.right, obj) + ")";
        case model::ExprOp::Eq:     return "(" + j_expr(*e.left, obj) + " == " + j_expr(*e.right, obj) + ")";
        case model::ExprOp::Neq:    return "(" + j_expr(*e.left, obj) + " != " + j_expr(*e.right, obj) + ")";
        case model::ExprOp::Lt:     return "(" + j_expr(*e.left, obj) + " < " + j_expr(*e.right, obj) + ")";
        case model::ExprOp::Lte:    return "(" + j_expr(*e.left, obj) + " <= " + j_expr(*e.right, obj) + ")";
        case model::ExprOp::Gt:     return "(" + j_expr(*e.left, obj) + " > " + j_expr(*e.right, obj) + ")";
        case model::ExprOp::Gte:    return "(" + j_expr(*e.left, obj) + " >= " + j_expr(*e.right, obj) + ")";
        case model::ExprOp::LogAnd: return "(" + j_expr(*e.left, obj) + " && " + j_expr(*e.right, obj) + ")";
        case model::ExprOp::LogOr:  return "(" + j_expr(*e.left, obj) + " || " + j_expr(*e.right, obj) + ")";
        case model::ExprOp::BitAnd: return "(" + j_expr(*e.left, obj) + " & " + j_expr(*e.right, obj) + ")";
        case model::ExprOp::BitOr:  return "(" + j_expr(*e.left, obj) + " | " + j_expr(*e.right, obj) + ")";
        case model::ExprOp::BitXor: return "(" + j_expr(*e.left, obj) + " ^ " + j_expr(*e.right, obj) + ")";
        case model::ExprOp::ShiftLeft:  return "(" + j_expr(*e.left, obj) + " << " + j_expr(*e.right, obj) + ")";
        case model::ExprOp::ShiftRight: return "(" + j_expr(*e.left, obj) + " >> " + j_expr(*e.right, obj) + ")";
        case model::ExprOp::Negate: return "(-" + j_expr(*e.left, obj) + ")";
        case model::ExprOp::BitNot: return "(~" + j_expr(*e.left, obj) + ")";
        case model::ExprOp::LogNot: return "(!" + j_expr(*e.left, obj) + ")";
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

// Evaluate an expression with outer-scope context awareness
std::string j_expr_ctx(const model::Expr& e, const std::string& obj,
                        const JOuterContext& outer_ctx) {
    if (e.op == model::ExprOp::FieldRef && !outer_ctx.empty()) {
        auto dot = e.name.find('.');
        std::string root = (dot != std::string::npos) ? e.name.substr(0, dot) : e.name;
        auto it = outer_ctx.find(root);
        if (it != outer_ctx.end()) {
            return it->second;
        }
    }
    return j_expr(e, obj);
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

std::string j_read_expr(const JFieldInfo& fi) {
    if (fi.wire_enc == model::WireEncoding::BCD) return "r.readBcd(" + std::to_string(fi.bits) + ")";
    if (fi.wire_enc == model::WireEncoding::BCD_S) return "r.readBcdSigned(" + std::to_string(fi.bits) + ")";
    if (fi.wire_enc == model::WireEncoding::BNR_S) return "r.readSignMagnitude(" + std::to_string(fi.bits) + ")";
    bool be = (fi.endian == model::Endian::Big);
    if (fi.is_float) {
        if (fi.bits <= 32) return std::string("r.readF32(") + (be ? "true" : "false") + ")";
        return std::string("r.readF64(") + (be ? "true" : "false") + ")";
    }
    if (fi.bits == 8 && !fi.is_signed) return "r.readU8()";
    if (fi.bits == 16 && !fi.is_signed) return std::string("r.readU16(") + (be ? "true" : "false") + ")";
    if (fi.bits == 32 && !fi.is_signed) return std::string("r.readU32(") + (be ? "true" : "false") + ")";
    if (fi.bits == 64 && !fi.is_signed) return std::string("r.readU64(") + (be ? "true" : "false") + ")";
    if (fi.is_signed) return "r.readSignedBits(" + std::to_string(fi.bits) + ")";
    return "r.readBits(" + std::to_string(fi.bits) + ")";
}

std::string j_write_stmt(const std::string& val, const JFieldInfo& fi) {
    if (fi.wire_enc == model::WireEncoding::BCD) return "w.writeBcd(" + val + ", " + std::to_string(fi.bits) + ")";
    if (fi.wire_enc == model::WireEncoding::BCD_S) return "w.writeBcdSigned(" + val + ", " + std::to_string(fi.bits) + ")";
    if (fi.wire_enc == model::WireEncoding::BNR_S) return "w.writeSignMagnitude(" + val + ", " + std::to_string(fi.bits) + ")";
    bool be = (fi.endian == model::Endian::Big);
    if (fi.is_float) {
        if (fi.bits <= 32) return std::string("w.writeF32(") + val + ", " + (be ? "true" : "false") + ")";
        return std::string("w.writeF64(") + val + ", " + (be ? "true" : "false") + ")";
    }
    // bool must be checked before bit-width checks since (int)boolean is illegal in Java
    if (fi.is_bool) return "w.writeBits(" + val + " ? 1 : 0, " + std::to_string(fi.bits) + ")";
    if (fi.bits == 8 && !fi.is_signed) return "w.writeU8((int)" + val + ")";
    if (fi.bits == 16 && !fi.is_signed) return std::string("w.writeU16((int)") + val + ", " + (be ? "true" : "false") + ")";
    if (fi.bits == 32 && !fi.is_signed) return std::string("w.writeU32(") + val + ", " + (be ? "true" : "false") + ")";
    if (fi.bits == 64 && !fi.is_signed) return std::string("w.writeU64(") + val + ", " + (be ? "true" : "false") + ")";
    if (fi.is_signed) return "w.writeSignedBits(" + val + ", " + std::to_string(fi.bits) + ")";
    return "w.writeBits(" + val + ", " + std::to_string(fi.bits) + ")";
}

// ============================================================================
// BitReader.java / BitWriter.java
// ============================================================================

std::string generate_j_bit_reader(const std::string& pkg) {
    EmitContext ctx;
    ctx.line("// Generated by bgen - DO NOT EDIT");
    ctx.line("package " + pkg + ";");
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
    ctx.line("if (n > 0 && ((val >> (n - 1)) & 1) != 0) val -= (1L << n);");
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
    ctx.line("public float readF32(boolean bigEndian) {");
    ctx.indent();
    ctx.line("byte[] b = new byte[4]; for (int i=0;i<4;i++) b[i]=(byte)readBits(8);");
    ctx.line("return ByteBuffer.wrap(b).order(bigEndian ? ByteOrder.BIG_ENDIAN : ByteOrder.LITTLE_ENDIAN).getFloat();");
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
    ctx.line("else if (enc == 1) { for (int i=0;i<b.length;i++) { int c=b[i]&0x3F; b[i]=(byte)(c<32?c+0x40:c); } }");
    ctx.line("return new String(b, StandardCharsets.ISO_8859_1);");
    ctx.dedent();
    ctx.line("}");
    // Packed character read: reads char_bits per character with IA5 6-bit mapping
    ctx.line("public String readPackedChars(int count, int charBits) {");
    ctx.indent();
    ctx.line("StringBuilder sb = new StringBuilder(count);");
    ctx.line("for (int i=0;i<count;i++) { int c=(int)readBits(charBits); sb.append((char)(c==0?0:(c<32?c+0x40:c))); }");
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
    ctx.line("package " + pkg + ";");
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
    ctx.line("if (value < 0) value += (1L << n);");
    ctx.line("writeBits(value & ((1L << n) - 1), n);");
    ctx.dedent();
    ctx.line("}");
    ctx.line();
    ctx.line("public void writeU8(int v) { writeBits(v & 0xFF, 8); }");
    ctx.line("public void writeU16(int v, boolean be) { byte[] b = new byte[2]; ByteBuffer.wrap(b).order(be?ByteOrder.BIG_ENDIAN:ByteOrder.LITTLE_ENDIAN).putShort((short)v); for (byte x:b) writeBits(x&0xFF,8); }");
    ctx.line("public void writeU32(int v, boolean be) { byte[] b = new byte[4]; ByteBuffer.wrap(b).order(be?ByteOrder.BIG_ENDIAN:ByteOrder.LITTLE_ENDIAN).putInt(v); for (byte x:b) writeBits(x&0xFF,8); }");
    ctx.line("public void writeU64(long v, boolean be) { byte[] b = new byte[8]; ByteBuffer.wrap(b).order(be?ByteOrder.BIG_ENDIAN:ByteOrder.LITTLE_ENDIAN).putLong(v); for (byte x:b) writeBits(x&0xFF,8); }");
    ctx.line("public void writeF32(float v, boolean be) { byte[] b = new byte[4]; ByteBuffer.wrap(b).order(be?ByteOrder.BIG_ENDIAN:ByteOrder.LITTLE_ENDIAN).putFloat(v); for (byte x:b) writeBits(x&0xFF,8); }");
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
    ctx.line("else if (enc == 1) { for (int i=0;i<b.length;i++) { int c=b[i]&0xFF; b[i]=(byte)(c>=0x40?c-0x40:c); } }");
    ctx.line("for (int i=0;i<len;i++) writeU8(i<b.length ? b[i]&0xFF : pad);");
    ctx.dedent();
    ctx.line("}");
    // Packed character write: writes char_bits per character with IA5 6-bit mapping
    ctx.line("public void writePackedChars(String s, int count, int charBits) {");
    ctx.indent();
    ctx.line("for (int i=0;i<count;i++) { int c=i<s.length()?(s.charAt(i)&0xFF):0; writeBits(c>=0x40?c-0x40:c, charBits); }");
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
    ctx.line("long raw=0; long v=Math.abs(val);");
    ctx.line("for (int i=0;i<bits/4;i++) { raw|=(v%10)<<(i*4); v/=10; }");
    ctx.line("writeBits(raw, bits);");
    ctx.dedent();
    ctx.line("}");
    ctx.line("public void writeBcdSigned(long val, int bits) { writeBits(val<0?1:0,1); writeBcd(Math.abs(val),bits-1); }");
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
    ctx.line("package " + pkg + ";");
    ctx.line();
    ctx.line("public class ConduitCodecException extends RuntimeException {");
    ctx.indent();
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
void emit_j_field_trim(EmitContext& ctx, const std::string& m, const model::Field& f) {
    // Determine effective trim — default to Right if not specified
    auto eff_trim = f.trim.value_or(model::StringTrim::Right);
    std::string ch = (f.padding && *f.padding == model::StringPadding::Space) ? "\" \"" : "\"\\0\"";
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
                      const std::string& parent_class_name = {}) {
    for (const auto& child : children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            // Inline enum field: enum_values populated, type_ref empty
            if (!f->enum_values.empty() && f->type_ref.empty() && !parent_class_name.empty()) {
                std::string enum_name = parent_class_name + j_class(f->name);
                fields.push_back({j_field(f->name), enum_name, "null"});
                continue;
            }
            auto fi = j_resolve_field(*f, index);
            JFieldDef jf;
            jf.name = j_field(f->name);
            jf.j_type = fi.j_type;
            jf.format = f->format;
            jf.is_numeric = !fi.is_struct && !fi.is_enum && !fi.is_string && !fi.is_bytes && !fi.is_bool &&
                            !fi.is_float && !fi.has_scale && (fi.bits > 0);
            if (fi.is_string) jf.init = "\"\"";
            else if (fi.is_bytes) jf.init = "new byte[0]";
            else if (fi.is_bool) jf.init = "false";
            else if (fi.j_type == "float") jf.init = "0.0f";
            else if (fi.j_type == "double") jf.init = "0.0";
            else if (fi.is_struct || fi.is_enum) jf.init = "null";
            else if (fi.j_type == "long") jf.init = "0L";
            else jf.init = "0";
            // Apply explicit default value from BMDL spec (matching C++/Python)
            if (f->default_value) jf.init = *f->default_value;
            fields.push_back(jf);
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            fields.push_back({j_field(sd->name), j_inline_class(sd->name, name_map), "null"});
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            std::string elem = ad->type_ref.empty() ? j_inline_class(ad->name, name_map) : j_class(ad->type_ref);
            fields.push_back({j_field(ad->name), "java.util.List<" + elem + ">", "new java.util.ArrayList<>()"});
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            fields.push_back({j_field(cd->name), "Object", "null"});
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            collect_j_fields(fx->children, index, fields, name_map, parent_class_name);
        }
    }
}

// Emit Java decode for field
void emit_j_field_decode(EmitContext& ctx, const model::Field& f,
                          const analyzer::TypeIndex& index, const std::string& pfx,
                          const std::string& parent_class_name = {}) {
    // Inline enum field: enum_values populated, type_ref empty
    if (!f.enum_values.empty() && f.type_ref.empty() && !parent_class_name.empty()) {
        std::string enum_name = parent_class_name + j_class(f.name);
        std::string m = pfx + "." + j_field(f.name);
        ctx.line(m + " = " + enum_name + ".decode(r);");
        return;
    }
    auto fi = j_resolve_field(f, index);
    std::string m = pfx + "." + j_field(f.name);
    if (fi.is_struct || fi.is_enum) { ctx.line(m + " = " + fi.j_type + ".decode(r);"); return; }
    if (fi.is_string) {
        bool has_enc = j_field_needs_encoding(f);
        std::string enc_arg = has_enc ? (", " + j_encoding_const(f)) : "";
        std::string read_fn = has_enc ? "readStringEncoded" : "readString";
        if (f.char_bits && f.length) {
            // Packed character decode (e.g., ICAO 6-bit chars)
            ctx.line(m + " = r.readPackedChars(" + std::to_string(*f.length) + ", " + std::to_string(*f.char_bits) + ");");
            emit_j_field_trim(ctx, m, f);
        } else if (f.terminated) {
            // Terminated string decode
            int max_len = f.max_length ? *f.max_length : 65535;
            if (*f.terminated == "crlf") {
                ctx.line(m + " = r.readCrlfTerminatedString(" + std::to_string(max_len) + ");");
            } else {
                // Parse hex terminator like "0x00" or use null
                std::string term = "0";
                if (f.terminated->size() > 2 && f.terminated->substr(0, 2) == "0x") {
                    term = *f.terminated;
                }
                ctx.line(m + " = r.readTerminatedString(" + term + ", " + std::to_string(max_len) + ");");
            }
            emit_j_field_trim(ctx, m, f);
        } else if (f.length) {
            ctx.line(m + " = r." + read_fn + "(" + std::to_string(*f.length) + enc_arg + ");");
            emit_j_field_trim(ctx, m, f);
        } else if (f.length_from) {
            ctx.line(m + " = r." + read_fn + "((int)(" + j_expr(*f.length_from, pfx) + ")" + enc_arg + ");");
        } else if (f.length_prefix) {
            auto pti = resolve_prefix_type(*f.length_prefix, index);
            bool pbe = (pti.endian == model::Endian::Big);
            std::string rd;
            if (pti.bits <= 8) rd = "(int) r.readU8()";
            else if (pti.bits <= 16) rd = std::string("(int) r.readU16(") + (pbe ? "true" : "false") + ")";
            else rd = std::string("(int) r.readU32(") + (pbe ? "true" : "false") + ")";
            ctx.line("int _pl = " + rd + ";");
            if (f.length_includes_prefix)
                ctx.line("_pl -= " + std::to_string(get_prefix_bytes(pti)) + ";");
            ctx.line(m + " = r." + read_fn + "(_pl" + enc_arg + ");");
        } else if (f.length_star) {
            ctx.line(m + " = r." + read_fn + "(r.remainingBytes()" + enc_arg + ");");
        } else {
            ctx.line(m + " = r." + read_fn + "(r.remainingBytes()" + enc_arg + ");");
        }
        // max_length validation (matching C++ MaxLengthExceeded check)
        if (f.max_length) {
            ctx.line("if (" + m + ".length() > " + std::to_string(*f.max_length) + ") throw new ConduitCodecException(\"" + f.name + " exceeds max length " + std::to_string(*f.max_length) + "\");");
        }
        return;
    }
    if (fi.is_bytes) {
        int len = f.length ? *f.length : (f.bytes_attr ? *f.bytes_attr : 0);
        if (len > 0) ctx.line(m + " = r.readBytes(" + std::to_string(len) + ");");
        else if (f.length_from) ctx.line(m + " = r.readBytes((int)(" + j_expr(*f.length_from, pfx) + "));");
        else ctx.line(m + " = r.readBytes(r.remainingBytes());");
        if (f.max_length) {
            ctx.line("if (" + m + ".length > " + std::to_string(*f.max_length) + ") throw new ConduitCodecException(\"" + f.name + " exceeds max length " + std::to_string(*f.max_length) + "\");");
        }
        return;
    }
    if (fi.has_scale) {
        JFieldInfo raw_fi = fi; raw_fi.bits = fi.raw_bits; raw_fi.is_signed = fi.raw_signed;
        raw_fi.is_float = false; raw_fi.has_scale = false;
        std::string ve = j_read_expr(raw_fi);
        std::string expr = ve;
        if (fi.scale != 1.0) expr += " * " + std::to_string(fi.scale);
        if (fi.offset != 0.0) expr += " + " + std::to_string(fi.offset);
        ctx.line(m + " = " + expr + ";");
        return;
    }
    if (fi.is_bool) { ctx.line(m + " = (" + j_read_expr(fi) + " != 0);"); return; }
    ctx.line(m + " = " + (fi.j_type == "int" ? "(int) " : "") + j_read_expr(fi) + ";");
    // Field-level constraint checks (matching C++ emit_constraint_check)
    // Skip deferred constraints (validated externally, not at decode time)
    if (f.constraint && f.constraint->validate != model::ValidateTiming::Deferred) {
        if (f.constraint->equals) {
            ctx.line("if (" + m + " != " + *f.constraint->equals + ") throw new ConduitCodecException(\"" + f.name + " constraint violation: expected " + *f.constraint->equals + "\");");
        }
        if (f.constraint->max) {
            ctx.line("if (" + m + " > " + *f.constraint->max + ") throw new ConduitCodecException(\"" + f.name + " exceeds max " + *f.constraint->max + "\");");
        }
        bool is_signed = fi.is_signed;
        if (f.constraint->min && (*f.constraint->min != "0" || is_signed)) {
            ctx.line("if (" + m + " < " + *f.constraint->min + ") throw new ConduitCodecException(\"" + f.name + " below min " + *f.constraint->min + "\");");
        }
    }
}

// Emit Java encode for field
void emit_j_field_encode(EmitContext& ctx, const model::Field& f,
                          const analyzer::TypeIndex& index, const std::string& pfx,
                          const std::string& parent_class_name = {}) {
    // Inline enum field: enum_values populated, type_ref empty
    if (!f.enum_values.empty() && f.type_ref.empty() && !parent_class_name.empty()) {
        std::string m = pfx + "." + j_field(f.name);
        ctx.line(m + ".encode(w);");
        return;
    }
    auto fi = j_resolve_field(f, index);
    std::string m = pfx + "." + j_field(f.name);
    if (f.auto_expr && f.auto_expr->kind == model::AutoKind::Length) {
        if (f.auto_expr->field_ref.empty()) {
            // auto="length" (whole struct): record start pos, write placeholder
            ctx.line("int _lenPos = w.sizeBytes();");
        } else {
            // auto="length(field)": record position for field-specific backpatch
            ctx.line("int _lenRefPos = w.sizeBytes();");
        }
        ctx.line(j_write_stmt("0", fi) + ";");
        return;
    }
    if (f.auto_expr && f.auto_expr->kind == model::AutoKind::Count) {
        // auto="count(field)": write the size of the referenced array
        std::string array_member = pfx + "." + j_field(f.auto_expr->field_ref);
        ctx.line(j_write_stmt("(int)" + array_member + ".size()", fi) + ";");
        return;
    }
    if (f.auto_expr && f.auto_expr->kind == model::AutoKind::Id) {
        // auto="id": write the message's ID_VALUE constant
        ctx.line(j_write_stmt("ID_VALUE", fi) + ";");
        return;
    }
    if (fi.is_struct || fi.is_enum) { ctx.line(m + ".encode(w);"); return; }
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
                if (f.terminated->size() > 2 && f.terminated->substr(0, 2) == "0x") {
                    term = *f.terminated;
                }
                ctx.line("w.writeTerminatedString(" + m + ", " + term + ");");
            }
        } else if (f.length) {
            int pad = (f.padding && *f.padding == model::StringPadding::Space) ? 0x20 : 0;
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
        return;
    }
    if (fi.is_bytes) { ctx.line("w.writeBytes(" + m + ");"); return; }
    if (fi.has_scale) {
        std::string inv = m;
        if (fi.offset != 0.0) inv = "(" + inv + " - " + std::to_string(fi.offset) + ")";
        if (fi.scale != 1.0) inv = "(" + inv + " / " + std::to_string(fi.scale) + ")";
        JFieldInfo raw_fi = fi; raw_fi.bits = fi.raw_bits; raw_fi.is_signed = fi.raw_signed;
        raw_fi.is_float = false; raw_fi.has_scale = false;
        ctx.line(j_write_stmt("(long)(" + inv + ")", raw_fi) + ";");
        return;
    }
    ctx.line(j_write_stmt(m, fi) + ";");
}

void emit_j_decode_children(EmitContext& ctx, const std::vector<model::StructChild>& children,
                             const analyzer::TypeIndex& index, const std::string& pfx,
                             const JOuterScopeMap& scope_map = {},
                             const JOuterContext& outer_ctx = {},
                             const JInlineNameMap& name_map = {},
                             const std::string& parent_class_name = {}) {
    for (const auto& child : children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            if (f->present_when) {
                ctx.line("if (" + j_expr_ctx(*f->present_when, pfx, outer_ctx) + ") {");
                ctx.indent(); emit_j_field_decode(ctx, *f, index, pfx, parent_class_name); ctx.dedent(); ctx.line("}");
            } else emit_j_field_decode(ctx, *f, index, pfx, parent_class_name);
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            std::string resolved = j_inline_class(sd->name, name_map);
            std::string args = j_build_outer_args(sd->name, scope_map, pfx, outer_ctx);
            std::string decode_line = pfx + "." + j_field(sd->name) + " = " + resolved + ".decode(r" + args + ");";
            if (sd->present_when) {
                ctx.line("if (" + j_expr_ctx(*sd->present_when, pfx, outer_ctx) + ") {");
                ctx.indent(); ctx.line(decode_line); ctx.dedent(); ctx.line("}");
            } else {
                ctx.line(decode_line);
            }
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            std::string m = pfx + "." + j_field(ad->name);
            std::string elem = ad->type_ref.empty() ? j_inline_class(ad->name, name_map) : j_class(ad->type_ref);
            auto emit_array_decode = [&]() {
                if (ad->fixed_count) {
                    ctx.line("for (int _i=0; _i<" + std::to_string(*ad->fixed_count) + "; _i++) " + m + ".add(" + elem + ".decode(r));");
                } else if (ad->count_from) {
                    ctx.line("for (int _i=0; _i<(int)(" + j_expr_ctx(*ad->count_from, pfx, outer_ctx) + "); _i++) " + m + ".add(" + elem + ".decode(r));");
                } else {
                    ctx.line("while (r.remainingBytes() > 0) " + m + ".add(" + elem + ".decode(r));");
                }
            };
            if (ad->present_when) {
                ctx.line("if (" + j_expr_ctx(*ad->present_when, pfx, outer_ctx) + ") {");
                ctx.indent(); emit_array_decode(); ctx.dedent(); ctx.line("}");
            } else {
                emit_array_decode();
            }
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            if (!cd->switch_expr) continue;
            std::string sv = j_expr_ctx(*cd->switch_expr, pfx, outer_ctx);
            std::string m = pfx + "." + j_field(cd->name);
            auto emit_choice_decode = [&]() {
                bool first = true;
                for (const auto& cs : cd->cases) {
                    std::string val = cs.value ? *cs.value : "0";
                    if (cs.value && index.constants.count(*cs.value)) {
                        val = "Constants." + j_const(*cs.value);
                    }
                    std::string cond = sv + " == " + val;
                    ctx.line(std::string(first ? "if (" : "} else if (") + cond + ") {");
                    ctx.indent();
                    std::string et = cs.type_ref.empty() ? j_inline_class(cs.name, name_map) : j_class(cs.type_ref);
                    std::string case_name = cs.type_ref.empty() ? cs.name : cs.type_ref;
                    std::string args = j_build_outer_args(case_name, scope_map, pfx, outer_ctx);
                    ctx.line(m + " = " + et + ".decode(r" + args + ");");
                    ctx.dedent();
                    first = false;
                }
                if (cd->otherwise) {
                    ctx.line("} else {");
                    ctx.indent();
                    std::string et = cd->otherwise->type_ref.empty() ? j_inline_class(cd->otherwise->name, name_map) : j_class(cd->otherwise->type_ref);
                    std::string ow_name = cd->otherwise->type_ref.empty() ? cd->otherwise->name : cd->otherwise->type_ref;
                    std::string args = j_build_outer_args(ow_name, scope_map, pfx, outer_ctx);
                    ctx.line(m + " = " + et + ".decode(r" + args + ");");
                    ctx.dedent();
                }
                if (!first) ctx.line("}");
            };
            if (cd->present_when) {
                ctx.line("if (" + j_expr_ctx(*cd->present_when, pfx, outer_ctx) + ") {");
                ctx.indent(); emit_choice_decode(); ctx.dedent(); ctx.line("}");
            } else {
                emit_choice_decode();
            }
        } else if (auto* res = std::get_if<model::Reserved>(&child)) {
            ctx.line("r.skipBits(" + std::to_string(res->bits) + ");");
        } else if (auto* al = std::get_if<model::Align>(&child)) {
            ctx.line("r.alignTo(" + std::to_string(al->to) + ");");
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            emit_j_decode_children(ctx, fx->children, index, pfx, scope_map, outer_ctx, name_map, parent_class_name);
        }
    }
}

void emit_j_encode_children(EmitContext& ctx, const std::vector<model::StructChild>& children,
                             const analyzer::TypeIndex& index, const std::string& pfx,
                             const std::string& len_ref_target = "",
                             const JInlineNameMap& name_map = {},
                             const std::string& parent_class_name = {}) {
    // Get the BMDL name from a StructChild
    auto get_child_name = [](const model::StructChild& child) -> std::string {
        return std::visit([](const auto& c) -> std::string {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, model::Field>) return c.name;
            else if constexpr (std::is_same_v<T, model::StructDef>) return c.name;
            else if constexpr (std::is_same_v<T, model::ArrayDef>) return c.name;
            else if constexpr (std::is_same_v<T, model::ChoiceDef>) return c.name;
            else return {};
        }, child);
    };

    for (const auto& child : children) {
        // auto-length(field) start marker: record position before the target child
        if (!len_ref_target.empty() && get_child_name(child) == len_ref_target) {
            ctx.line("int _" + j_field(len_ref_target) + "Start = w.sizeBytes();");
        }

        if (auto* f = std::get_if<model::Field>(&child)) {
            if (f->present_when) {
                ctx.line("if (" + j_expr(*f->present_when, pfx) + ") {");
                ctx.indent(); emit_j_field_encode(ctx, *f, index, pfx, parent_class_name); ctx.dedent(); ctx.line("}");
            } else emit_j_field_encode(ctx, *f, index, pfx, parent_class_name);
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            std::string m = pfx + "." + j_field(sd->name);
            if (sd->present_when) {
                ctx.line("if (" + m + " != null) " + m + ".encode(w);");
            } else {
                ctx.line(m + ".encode(w);");
            }
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            std::string m = pfx + "." + j_field(ad->name);
            if (ad->present_when) {
                ctx.line("if (" + m + " != null) { for (var _item : " + m + ") _item.encode(w); }");
            } else {
                ctx.line("for (var _item : " + m + ") _item.encode(w);");
            }
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            std::string m = pfx + "." + j_field(cd->name);
            auto emit_choice_encode = [&]() {
                bool first = true;
                for (const auto& cs : cd->cases) {
                    std::string et = cs.type_ref.empty() ? j_inline_class(cs.name, name_map) : j_class(cs.type_ref);
                    ctx.line(std::string(first ? "if" : "} else if") + " (" + m + " instanceof " + et + " _cv) {");
                    ctx.indent();
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
        } else if (auto* res = std::get_if<model::Reserved>(&child)) {
            ctx.line("w.writeBits(0, " + std::to_string(res->bits) + ");");
        } else if (auto* al = std::get_if<model::Align>(&child)) {
            ctx.line("w.alignTo(" + std::to_string(al->to) + ");");
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            emit_j_encode_children(ctx, fx->children, index, pfx, len_ref_target, name_map, parent_class_name);
        }
    }
}

// Generate one Java class file for a struct or message
std::string generate_j_class(const std::string& name,
                              const std::vector<model::StructChild>& children,
                              const analyzer::TypeIndex& index,
                              const std::string& pkg,
                              const std::unordered_map<std::string, uint64_t>& tid_map,
                              const std::string& msg_id = "",
                              const JOuterScopeMap& scope_map = {},
                              const JInlineNameMap& name_map = {},
                              const std::string& class_name_override = {}) {
    std::string cn = class_name_override.empty() ? j_class(name) : class_name_override;
    std::vector<JFieldDef> fields;
    collect_j_fields(children, index, fields, name_map, cn);

    EmitContext ctx;
    ctx.line("// Generated by bgen - DO NOT EDIT");
    ctx.line("package " + pkg + ";");
    ctx.line();
    ctx.line("import java.util.*;");
    ctx.line();
    ctx.line("public final class " + cn + " {");
    ctx.indent();

    auto tid_it = tid_map.find(name);
    if (tid_it != tid_map.end()) {
        ctx.line("public static final long TYPE_ID = " + j_hex64(tid_it->second) + ";");
        ctx.line("public static final String TYPE_NAME = \"" + name + "\";");
    }
    if (!msg_id.empty()) ctx.line("public static final int ID_VALUE = " + msg_id + ";");
    ctx.line();

    // Fields
    for (const auto& f : fields)
        ctx.line("public " + f.j_type + " " + f.name + " = " + f.init + ";");
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
    ctx.line("public static " + cn + " decode(BitReader r" + decode_params + ") {");
    ctx.indent();
    ctx.line(cn + " result = new " + cn + "();");
    emit_j_decode_children(ctx, children, index, "result", scope_map, outer_ctx, name_map, cn);
    ctx.line("return result;");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // decode from bytes (only when no outer-scope params — otherwise it's an inline type)
    if (osp_it == scope_map.end()) {
        ctx.line("public static " + cn + " decodeBytes(byte[] data) { return decode(new BitReader(data)); }");
        ctx.line();
    }

    // encode - with auto-length backpatch support
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
        }
        std::string len_ref_target = auto_len_ref_field ? auto_len_ref_field->auto_expr->field_ref : "";
        emit_j_encode_children(ctx, children, index, "this", len_ref_target, name_map, cn);

        // Backpatch auto-length (whole struct)
        if (auto_len_field) {
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
                    default: break;
                }
                if (!op_str.empty()) {
                    size_expr = "((" + size_expr + ")" + op_str +
                                std::to_string(auto_len_field->auto_expr->modifier.literal) + ")";
                }
            }
            if (al_fi.bits <= 8) {
                ctx.line("w.patchU8(_lenPos, (int)(" + size_expr + "));");
            } else if (al_fi.bits <= 16) {
                ctx.line("w.patchU16(_lenPos, (int)(" + size_expr + "), " + (be ? "true" : "false") + ");");
            } else {
                ctx.line("w.patchU32(_lenPos, (int)(" + size_expr + "), " + (be ? "true" : "false") + ");");
            }
        }

        // Backpatch auto-length(field) - field-specific length
        if (auto_len_ref_field) {
            auto al_fi = j_resolve_field(*auto_len_ref_field, index);
            bool be = (al_fi.endian == model::Endian::Big);
            std::string target_field = j_field(auto_len_ref_field->auto_expr->field_ref);
            std::string size_expr = "w.sizeBytes() - _" + target_field + "Start";
            if (auto_len_ref_field->auto_expr->modifier.has_modifier()) {
                std::string op_str;
                switch (auto_len_ref_field->auto_expr->modifier.op) {
                    case model::ArithOp::Add: op_str = " + "; break;
                    case model::ArithOp::Sub: op_str = " - "; break;
                    case model::ArithOp::Mul: op_str = " * "; break;
                    case model::ArithOp::Div: op_str = " / "; break;
                    default: break;
                }
                if (!op_str.empty()) {
                    size_expr = "((" + size_expr + ")" + op_str +
                                std::to_string(auto_len_ref_field->auto_expr->modifier.literal) + ")";
                }
            }
            if (al_fi.bits <= 8) {
                ctx.line("w.patchU8(_lenRefPos, (int)(" + size_expr + "));");
            } else if (al_fi.bits <= 16) {
                ctx.line("w.patchU16(_lenRefPos, (int)(" + size_expr + "), " + (be ? "true" : "false") + ");");
            } else {
                ctx.line("w.patchU32(_lenRefPos, (int)(" + size_expr + "), " + (be ? "true" : "false") + ");");
            }
        }

        ctx.dedent();
        ctx.line("}");
        ctx.line();
    }

    // encodeBytes
    ctx.line("public byte[] encodeBytes() { BitWriter w = new BitWriter(); encode(w); return w.toBytes(); }");
    ctx.line();

    // toString
    ctx.line("@Override public String toString() {");
    ctx.indent();
    if (fields.empty()) {
        ctx.line("return \"" + cn + "()\";");
    } else {
        std::string fmt = "return \"" + cn + "(\" + ";
        for (size_t i = 0; i < fields.size(); i++) {
            if (i > 0) fmt += " + \", \" + ";
            if (fields[i].j_type == "byte[]")
                fmt += "\"" + fields[i].name + "=\" + java.util.Arrays.toString(" + fields[i].name + ")";
            else if (fields[i].is_numeric && fields[i].format == model::DisplayFormat::Hex)
                fmt += "\"" + fields[i].name + "=0x\" + Long.toHexString(" + fields[i].name + ")";
            else if (fields[i].is_numeric && fields[i].format == model::DisplayFormat::Octal)
                fmt += "\"" + fields[i].name + "=0\" + Long.toOctalString(" + fields[i].name + ")";
            else if (fields[i].is_numeric && fields[i].format == model::DisplayFormat::Binary)
                fmt += "\"" + fields[i].name + "=0b\" + Long.toBinaryString(" + fields[i].name + ")";
            else
                fmt += "\"" + fields[i].name + "=\" + " + fields[i].name;
        }
        fmt += " + \")\";";
        ctx.line(fmt);
    }
    ctx.dedent();
    ctx.line("}");

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
    ctx.line("public record TypeInfo(long typeId, String typeName) {}");
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
                EmitContext tctx;
                tctx.line("// Generated by bgen - DO NOT EDIT");
                tctx.line("package " + pkg + ";");
                tctx.line();
                tctx.line("public enum " + enum_name + " {");
                tctx.indent();
                for (size_t i = 0; i < f->enum_values.size(); i++) {
                    std::string comma = (i + 1 < f->enum_values.size()) ? "," : ";";
                    tctx.line(j_const(f->enum_values[i].name) + "(" + std::to_string(f->enum_values[i].id) + ")" + comma);
                }
                tctx.line();
                tctx.line("public final int value;");
                tctx.line(enum_name + "(int v) { this.value = v; }");
                tctx.line();
                tctx.line("public static " + enum_name + " decode(BitReader r) {");
                tctx.indent();
                tctx.line("int raw = (int) r.readBits(" + std::to_string(bits) + ");");
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
            std::string code = generate_j_class(sd->name, sd->children, index, pkg, tid_map, {}, scope_map, name_map, resolved);
            out_files.push_back({resolved + ".java", code});
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            if (ad->type_ref.empty() && !ad->children.empty()) {
                std::string resolved = j_resolve_inline_name(ad->name, prefix, ad->type_name);
                name_map[ad->name] = resolved;
                collect_inline_types(ad->children, index, pkg, tid_map, scope_map, ad->name, out_files, name_map, resolved);
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
                    std::string code = generate_j_class(cs.name, cs.children, index, pkg, tid_map, {}, scope_map, name_map, resolved);
                    out_files.push_back({resolved + ".java", code});
                }
            }
            if (cd->otherwise && cd->otherwise->type_ref.empty() && !cd->otherwise->children.empty()) {
                std::string resolved = j_resolve_inline_name(cd->otherwise->name, prefix, cd->otherwise->type_name);
                name_map[cd->otherwise->name] = resolved;
                j_analyze_outer_scope(cd->otherwise->name, cd->otherwise->children, children, index, current_bmdl_name, scope_map);
                collect_inline_types(cd->otherwise->children, index, pkg, tid_map, scope_map, cd->otherwise->name, out_files, name_map, resolved);
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

    EmitContext ctx;
    ctx.line("// Generated by bgen - DO NOT EDIT");
    ctx.line("package " + pkg + ";");
    ctx.line();
    ctx.line("import java.util.*;");
    ctx.line();
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
                    ctx.line("frame." + j_field(f->name) + " = " + *f->constraint->equals + ";");
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
                    ctx.line(j_write_stmt("(int)payload.size()", fi) + ";");
                } else {
                    ctx.line(j_write_stmt("this." + j_field(f->name), fi) + ";");
                }
            } else if (f->constraint && f->constraint->equals) {
                ctx.line(j_write_stmt(*f->constraint->equals, fi) + ";");
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
            ctx.line(std::string(first ? "if" : "} else if") + " (item instanceof " + leaf_class + " _m) {");
            ctx.indent();
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
            ctx.line(std::string(first ? "if" : "} else if") + " (payload instanceof " + leaf_class + " _m) {");
            ctx.indent();
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
            std::string val = "this." + j_field(f->name);
            if (fi.is_enum) val = val + ".value";
            ctx.line(j_write_stmt(val, fi) + ";");
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
                default: break;
            }
            if (!op_str.empty()) {
                raw_length = "((" + raw_length + ")" + op_str +
                             std::to_string(length_field->auto_expr->modifier.literal) + ")";
            }
        }
        if (al_fi.bits <= 8) {
            ctx.line("w.patchU8(_lenPos, (int)(" + raw_length + "));");
        } else if (al_fi.bits <= 16) {
            ctx.line("w.patchU16(_lenPos, (int)(" + raw_length + "), " + (be ? "true" : "false") + ");");
        } else {
            ctx.line("w.patchU32(_lenPos, (int)(" + raw_length + "), " + (be ? "true" : "false") + ");");
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
                ctx.line(m + " = " + (fi.j_type == "int" ? "(int) " : "") + j_read_expr(fi) + ";");
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
        std::string raw_val = "(int)(" + len_member + ")";
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
            ctx.line("for (int _i = 0; _i < (int)(result." + j_field(si.count_field_name) + "); _i++) {");
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
        if (!first) ctx.line("}");
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
                ctx.line(m + " = " + (fi.j_type == "int" ? "(int) " : "") + j_read_expr(fi) + ";");
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
                ctx.line("if (item instanceof " + leaf_class + " _fm) {");
                ctx.indent();
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
                ctx.line("if (result.payload instanceof " + leaf_class + " _fm) {");
                ctx.indent();
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

    EmitContext ctx;
    ctx.line("// Generated by bgen - DO NOT EDIT");
    ctx.line("package " + pkg + ";");
    ctx.line();
    ctx.line("import java.util.*;");
    ctx.line();
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
    ctx.line(frame_class + " frame = " + frame_class + ".decodeBytes(data);");
    ctx.line("List<Map<String, Object>> messages = new ArrayList<>();");
    if (si.payload_is_array) {
        ctx.line("for (Object item : frame.payload) {");
        ctx.indent();
        bool first = true;
        for (const auto& lt : si.leaf_types) {
            std::string leaf_class = j_class(lt.name);
            ctx.line(std::string(first ? "if" : "} else if") + " (item instanceof " + leaf_class + " _m) {");
            ctx.indent();
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
            ctx.line(std::string(first ? "if" : "} else if") + " (frame.payload instanceof " + leaf_class + " _m) {");
            ctx.indent();
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

            // Set message-level config fields
            if (!lt.config_fields.empty() && has_config) {
                for (const auto& cf : lt.config_fields) {
                    std::string cfg_key = cf.key;
                    std::string cfg_type = (cf.bits > 32) ? "long" : "int";
                    std::string cast = "((" + std::string(cf.bits > 32 ? "Long" : "Number") + ") config.getOrDefault(\"" + cfg_key + "\", 0))";
                    if (cf.bits <= 32) cast += ".intValue()";
                    else cast += ".longValue()";
                    ctx.line("msg." + j_field(cf.field_name) + " = " + cast + ";");
                }
            }

            ctx.line(frame_class + " frame = " + frame_class + ".wrap(msg);");

            // Set frame-level config fields
            if (has_config) {
                for (const auto& cf : si.config_fields) {
                    std::string cfg_key = cf.key;
                    std::string cast = "((Number) config.getOrDefault(\"" + cfg_key + "\", 0)).intValue()";
                    ctx.line("frame." + j_field(cf.field_name) + " = " + cast + ";");
                }
            }

            // Set auto-increment fields
            for (size_t ai = 0; ai < lt.auto_fields.size(); ++ai) {
                int bits = (ai < lt.auto_field_bits.size()) ? lt.auto_field_bits[ai] : 8;
                uint64_t mask_val = (bits >= 64) ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
                std::string field = j_field(lt.auto_fields[ai]);
                if (bits > 32) {
                    ctx.line("frame." + field + " = sequenceCounter & " + std::to_string(mask_val) + "L;");
                } else {
                    ctx.line("frame." + field + " = (int)(sequenceCounter & " + std::to_string(mask_val) + "L);");
                }
                ctx.line("sequenceCounter++;");
            }

            // Set auto-timestamp fields
            for (size_t ti = 0; ti < lt.timestamp_fields.size(); ++ti) {
                int bits = (ti < lt.timestamp_field_bits.size()) ? lt.timestamp_field_bits[ti] : 32;
                uint64_t mask_val = (bits >= 64) ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
                std::string field = j_field(lt.timestamp_fields[ti]);
                if (bits > 32) {
                    ctx.line("frame." + field + " = System.currentTimeMillis() & " + std::to_string(mask_val) + "L;");
                } else {
                    ctx.line("frame." + field + " = (int)(System.currentTimeMillis() & " + std::to_string(mask_val) + "L);");
                }
            }

            ctx.line("byte[] encoded = frame.encodeBytes();");
            ctx.line("Map<String, Object> result = new HashMap<>();");
            ctx.line("result.put(\"bytes\", encoded);");
            ctx.line("result.put(\"type_id\", typeId);");
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
                                ctx.line("frame." + j_field(f->name) + " = " + *f->constraint->equals + ";");
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
                        std::string cast = "((Number) config.getOrDefault(\"" + cfg_key + "\", 0)).intValue()";
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
                        std::string cast = "((Number) config.getOrDefault(\"" + cfg_key + "\", 0)).intValue()";
                        ctx.line("frame." + j_field(cf.field_name) + " = " + cast + ";");
                    }
                }

                // Set auto-increment fields
                for (size_t ai = 0; ai < lt.auto_fields.size(); ++ai) {
                    int bits = (ai < lt.auto_field_bits.size()) ? lt.auto_field_bits[ai] : 8;
                    uint64_t mask_val = (bits >= 64) ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
                    std::string field = j_field(lt.auto_fields[ai]);
                    if (bits > 32) {
                        ctx.line("frame." + field + " = sequenceCounter & " + std::to_string(mask_val) + "L;");
                    } else {
                        ctx.line("frame." + field + " = (int)(sequenceCounter & " + std::to_string(mask_val) + "L);");
                    }
                    ctx.line("sequenceCounter++;");
                }

                // Set auto-timestamp fields
                for (size_t ti = 0; ti < lt.timestamp_fields.size(); ++ti) {
                    int bits = (ti < lt.timestamp_field_bits.size()) ? lt.timestamp_field_bits[ti] : 32;
                    uint64_t mask_val = (bits >= 64) ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
                    std::string field = j_field(lt.timestamp_fields[ti]);
                    if (bits > 32) {
                        ctx.line("frame." + field + " = System.currentTimeMillis() & " + std::to_string(mask_val) + "L;");
                    } else {
                        ctx.line("frame." + field + " = (int)(System.currentTimeMillis() & " + std::to_string(mask_val) + "L);");
                    }
                }

                ctx.line("byte[] encoded = frame.encodeBytes();");
                ctx.line("Map<String, Object> result = new HashMap<>();");
                ctx.line("result.put(\"bytes\", encoded);");
                ctx.line("result.put(\"type_id\", typeId);");
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

    (void)sizes;

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

    bool ok = true;

    // Infrastructure files
    ok &= write_file(output_dir / "BitReader.java", generate_j_bit_reader(java_pkg));
    ok &= write_file(output_dir / "BitWriter.java", generate_j_bit_writer(java_pkg));
    ok &= write_file(output_dir / "ConduitCodecException.java", generate_j_exception(java_pkg));
    ok &= write_file(output_dir / "Constants.java", generate_j_constants(protocol, java_pkg));
    ok &= write_file(output_dir / "Protocol.java", generate_j_protocol(protocol, sessions, java_pkg));

    int file_count = 5;

    // Type wrapper files (one per type)
    {
        for (const auto& t : protocol.types) {
            bool is_enum = !t.enum_values.empty();
            bool is_flags = !t.flags.empty();
            bool has_scale = t.scale.has_value() || t.offset.has_value();
            if (is_enum || is_flags || has_scale || t.constraint) {
                std::string name = j_class(t.name);
                // Generate individual file for this type
                EmitContext tctx;
                tctx.line("// Generated by bgen - DO NOT EDIT");
                tctx.line("package " + java_pkg + ";");
                tctx.line();

                if (is_enum) {
                    tctx.line("public enum " + name + " {");
                    tctx.indent();
                    for (size_t i = 0; i < t.enum_values.size(); i++) {
                        std::string comma = (i + 1 < t.enum_values.size()) ? "," : ";";
                        tctx.line(j_const(t.enum_values[i].name) + "(" + std::to_string(t.enum_values[i].id) + ")" + comma);
                    }
                    tctx.line();
                    tctx.line("public final int value;");
                    tctx.line(name + "(int v) { this.value = v; }");
                    tctx.line();
                    tctx.line("public static " + name + " decode(BitReader r) {");
                    tctx.indent();
                    tctx.line("int raw = (int) r.readBits(" + std::to_string(t.bits) + ");");
                    tctx.line("for (" + name + " v : values()) if (v.value == raw) return v;");
                    tctx.line("throw new ConduitCodecException(\"unknown " + name + " value: \" + raw);");
                    tctx.dedent();
                    tctx.line("}");
                    tctx.line();
                    tctx.line("public void encode(BitWriter w) { w.writeBits(value, " + std::to_string(t.bits) + "); }");
                    tctx.dedent();
                    tctx.line("}");
                } else {
                    bool is_signed = (t.base == model::PrimitiveBase::Int);
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
                        if (t.scale) ve += " * " + std::to_string(*t.scale);
                        if (t.offset) ve += " + " + std::to_string(*t.offset);
                        tctx.line("public double value() { return " + ve + "; }");
                    } else {
                        tctx.line("public long value() { return raw; }");
                    }
                    std::string rd = is_signed ? "readSignedBits" : "readBits";
                    std::string wr = is_signed ? "writeSignedBits" : "writeBits";
                    // Generate decode with constraint validation (matching C++ emit_constraint_check)
                    if (t.constraint && (t.constraint->max || (t.constraint->min && (*t.constraint->min != "0" || is_signed)) || t.constraint->equals)) {
                        tctx.line("public static " + name + " decode(BitReader r) {");
                        tctx.indent();
                        tctx.line("long raw = r." + rd + "(" + std::to_string(t.bits) + ");");
                        if (t.constraint->equals) {
                            tctx.line("if (raw != " + *t.constraint->equals + ") throw new ConduitCodecException(\"" + name + " constraint violation: expected " + *t.constraint->equals + "\");");
                        }
                        if (t.constraint->max) {
                            tctx.line("if (raw > " + *t.constraint->max + ") throw new ConduitCodecException(\"" + name + " exceeds max " + *t.constraint->max + "\");");
                        }
                        // Skip min=0 for unsigned types (always true)
                        if (t.constraint->min && (*t.constraint->min != "0" || is_signed)) {
                            tctx.line("if (raw < " + *t.constraint->min + ") throw new ConduitCodecException(\"" + name + " below min " + *t.constraint->min + "\");");
                        }
                        tctx.line("return new " + name + "(raw);");
                        tctx.dedent();
                        tctx.line("}");
                    } else {
                        tctx.line("public static " + name + " decode(BitReader r) { return new " + name + "(r." + rd + "(" + std::to_string(t.bits) + ")); }");
                    }
                    tctx.line("public void encode(BitWriter w) { w." + wr + "(raw, " + std::to_string(t.bits) + "); }");
                    tctx.dedent();
                    tctx.line("}");
                }
                ok &= write_file(output_dir / (name + ".java"), tctx.str());
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
            ok &= write_file(output_dir / fname, fcode);
            file_count++;
        }
        std::string code = generate_j_class(sd.name, sd.children, index, java_pkg, empty, {}, scope_map, name_map);
        ok &= write_file(output_dir / (j_class(sd.name) + ".java"), code);
        file_count++;
    }

    // Message classes (including inline children)
    for (const auto& md : protocol.messages) {
        std::vector<std::pair<std::string, std::string>> inline_files;
        JOuterScopeMap scope_map;
        JInlineNameMap name_map;
        collect_inline_types(md.children, index, java_pkg, tid_map, scope_map, md.name, inline_files, name_map);
        for (const auto& [fname, fcode] : inline_files) {
            ok &= write_file(output_dir / fname, fcode);
            file_count++;
        }
        std::string code = generate_j_class(md.name, md.children, index, java_pkg, tid_map, md.id, scope_map, name_map);
        ok &= write_file(output_dir / (j_class(md.name) + ".java"), code);
        file_count++;
    }

    // Frame classes (one per frame-based session)
    for (const auto& si : sessions) {
        if (si.is_frame_based && si.frame) {
            std::string code = generate_j_frame_class(si, index, java_pkg);
            ok &= write_file(output_dir / (j_class(si.frame->name) + ".java"), code);
            file_count++;
        }
    }

    // Session classes (one per frame-based session)
    for (const auto& si : sessions) {
        if (si.is_frame_based && si.frame) {
            std::string code = generate_j_session_class(protocol, si, index, java_pkg);
            std::string session_name = j_class(si.frame->name) + "Session";
            ok &= write_file(output_dir / (session_name + ".java"), code);
            file_count++;
        }
    }

    if (ok) Logger::info("generated " + std::to_string(file_count) + " Java files in " + output_dir.string());
    return ok;
}

} // namespace bgen::codegen
