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
    if (fi.bits == 8 && !fi.is_signed) return "w.writeU8((int)" + val + ")";
    if (fi.bits == 16 && !fi.is_signed) return std::string("w.writeU16((int)") + val + ", " + (be ? "true" : "false") + ")";
    if (fi.bits == 32 && !fi.is_signed) return std::string("w.writeU32(") + val + ", " + (be ? "true" : "false") + ")";
    if (fi.bits == 64 && !fi.is_signed) return std::string("w.writeU64(") + val + ", " + (be ? "true" : "false") + ")";
    if (fi.is_bool) return "w.writeBits(" + val + " ? 1 : 0, " + std::to_string(fi.bits) + ")";
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
// Types.java — enums, flags, scaled, constrained
// ============================================================================

std::string generate_j_types(const model::Protocol& protocol, const std::string& pkg) {
    std::string result;
    EmitContext ctx;

    for (const auto& t : protocol.types) {
        bool is_enum = !t.enum_values.empty();
        bool is_flags = !t.flags.empty();
        bool has_scale = t.scale.has_value() || t.offset.has_value();
        bool is_signed = (t.base == model::PrimitiveBase::Int);

        if (is_enum) {
            std::string name = j_class(t.name);
            ctx.clear();
            ctx.line("// Generated by bgen - DO NOT EDIT");
            ctx.line("package " + pkg + ";");
            ctx.line();
            ctx.line("public enum " + name + " {");
            ctx.indent();
            for (size_t i = 0; i < t.enum_values.size(); i++) {
                std::string comma = (i + 1 < t.enum_values.size()) ? "," : ";";
                ctx.line(j_const(t.enum_values[i].name) + "(" + std::to_string(t.enum_values[i].id) + ")" + comma);
            }
            ctx.line();
            ctx.line("public final int value;");
            ctx.line(name + "(int v) { this.value = v; }");
            ctx.line();
            ctx.line("public static " + name + " decode(BitReader r) {");
            ctx.indent();
            ctx.line("int raw = (int) r.readBits(" + std::to_string(t.bits) + ");");
            ctx.line("for (" + name + " v : values()) if (v.value == raw) return v;");
            ctx.line("throw new ConduitCodecException(\"unknown " + name + " value: \" + raw);");
            ctx.dedent();
            ctx.line("}");
            ctx.line();
            ctx.line("public void encode(BitWriter w) { w.writeBits(value, " + std::to_string(t.bits) + "); }");
            ctx.dedent();
            ctx.line("}");
            result += ctx.str();
        } else if (is_flags) {
            std::string name = j_class(t.name);
            ctx.clear();
            ctx.line("// Generated by bgen - DO NOT EDIT");
            ctx.line("package " + pkg + ";");
            ctx.line();
            ctx.line("public final class " + name + " {");
            ctx.indent();
            ctx.line("private int raw;");
            ctx.line("public " + name + "() { this.raw = 0; }");
            ctx.line("public " + name + "(int raw) { this.raw = raw; }");
            ctx.line("public int raw() { return raw; }");
            ctx.line("public void setRaw(int v) { raw = v; }");
            for (const auto& f : t.flags) {
                std::string getter = j_field(f.name);
                std::string bs = std::to_string(f.bit);
                ctx.line("public boolean " + getter + "() { return ((raw >> " + bs + ") & 1) != 0; }");
                ctx.line("public void set" + j_class(f.name) + "(boolean v) { if (v) raw |= (1<<" + bs + "); else raw &= ~(1<<" + bs + "); }");
            }
            ctx.line("public static " + name + " decode(BitReader r) { return new " + name + "((int)r.readBits(" + std::to_string(t.bits) + ")); }");
            ctx.line("public void encode(BitWriter w) { w.writeBits(raw, " + std::to_string(t.bits) + "); }");
            ctx.dedent();
            ctx.line("}");
            result += ctx.str();
        } else if (has_scale || t.constraint) {
            std::string name = j_class(t.name);
            ctx.clear();
            ctx.line("// Generated by bgen - DO NOT EDIT");
            ctx.line("package " + pkg + ";");
            ctx.line();
            ctx.line("public final class " + name + " {");
            ctx.indent();
            ctx.line("private long raw;");
            ctx.line("public " + name + "() {}");
            ctx.line("public " + name + "(long raw) { this.raw = raw; }");
            ctx.line("public long raw() { return raw; }");
            ctx.line("public void setRaw(long v) { raw = v; }");
            if (has_scale) {
                std::string ve = "(double) raw";
                if (t.scale) ve += " * " + std::to_string(*t.scale);
                if (t.offset) ve += " + " + std::to_string(*t.offset);
                ctx.line("public double value() { return " + ve + "; }");
                std::string inv = "v";
                if (t.offset) inv = "(" + inv + " - " + std::to_string(*t.offset) + ")";
                if (t.scale) inv = "(" + inv + " / " + std::to_string(*t.scale) + ")";
                ctx.line("public void setValue(double v) { raw = (long)(" + inv + "); }");
            } else {
                ctx.line("public long value() { return raw; }");
            }
            std::string rd = is_signed ? "readSignedBits" : "readBits";
            std::string wr = is_signed ? "writeSignedBits" : "writeBits";
            ctx.line("public static " + name + " decode(BitReader r) { return new " + name + "(r." + rd + "(" + std::to_string(t.bits) + ")); }");
            ctx.line("public void encode(BitWriter w) { w." + wr + "(raw, " + std::to_string(t.bits) + "); }");
            ctx.dedent();
            ctx.line("}");
            result += ctx.str();
        }
    }
    return result;
}

// ============================================================================
// Struct/Message Java class generation
// ============================================================================

struct JFieldDef {
    std::string name;
    std::string j_type;
    std::string init;
};

void collect_j_fields(const std::vector<model::StructChild>& children,
                      const analyzer::TypeIndex& index,
                      std::vector<JFieldDef>& fields) {
    for (const auto& child : children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fi = j_resolve_field(*f, index);
            JFieldDef jf;
            jf.name = j_field(f->name);
            jf.j_type = fi.j_type;
            if (fi.is_string) jf.init = "\"\"";
            else if (fi.is_bytes) jf.init = "new byte[0]";
            else if (fi.is_bool) jf.init = "false";
            else if (fi.is_float) jf.init = "0.0f";
            else if (fi.j_type == "double") jf.init = "0.0";
            else if (fi.is_struct || fi.is_enum) jf.init = "null";
            else if (fi.j_type == "long") jf.init = "0L";
            else jf.init = "0";
            fields.push_back(jf);
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            fields.push_back({j_field(sd->name), j_class(sd->name), "null"});
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            std::string elem = ad->type_ref.empty() ? j_class(ad->name) : j_class(ad->type_ref);
            fields.push_back({j_field(ad->name), "java.util.List<" + elem + ">", "new java.util.ArrayList<>()"});
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            fields.push_back({j_field(cd->name), "Object", "null"});
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            collect_j_fields(fx->children, index, fields);
        }
    }
}

// Emit Java decode for field
void emit_j_field_decode(EmitContext& ctx, const model::Field& f,
                          const analyzer::TypeIndex& index, const std::string& pfx) {
    auto fi = j_resolve_field(f, index);
    std::string m = pfx + "." + j_field(f.name);
    if (fi.is_struct || fi.is_enum) { ctx.line(m + " = " + fi.j_type + ".decode(r);"); return; }
    if (fi.is_string) {
        if (f.length) {
            ctx.line(m + " = r.readString(" + std::to_string(*f.length) + ");");
            std::string ch = (f.padding && *f.padding == model::StringPadding::Space) ? "\" \"" : "\"\\0\"";
            ctx.line("while (" + m + ".endsWith(" + ch + ")) " + m + " = " + m + ".substring(0, " + m + ".length()-1);");
        } else if (f.length_from) {
            ctx.line(m + " = r.readString((int)(" + j_expr(*f.length_from, pfx) + "));");
        } else {
            ctx.line(m + " = r.readString(r.remainingBytes());");
        }
        return;
    }
    if (fi.is_bytes) {
        int len = f.length ? *f.length : (f.bytes_attr ? *f.bytes_attr : 0);
        if (len > 0) ctx.line(m + " = r.readBytes(" + std::to_string(len) + ");");
        else if (f.length_from) ctx.line(m + " = r.readBytes((int)(" + j_expr(*f.length_from, pfx) + "));");
        else ctx.line(m + " = r.readBytes(r.remainingBytes());");
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
}

// Emit Java encode for field
void emit_j_field_encode(EmitContext& ctx, const model::Field& f,
                          const analyzer::TypeIndex& index, const std::string& pfx) {
    auto fi = j_resolve_field(f, index);
    std::string m = pfx + "." + j_field(f.name);
    if (f.auto_expr && f.auto_expr->kind == model::AutoKind::Length) {
        ctx.line("int _lenPos = w.sizeBytes();");
        ctx.line(j_write_stmt("0", fi) + ";");
        return;
    }
    if (fi.is_struct || fi.is_enum) { ctx.line(m + ".encode(w);"); return; }
    if (fi.is_string) {
        if (f.length) {
            int pad = (f.padding && *f.padding == model::StringPadding::Space) ? 0x20 : 0;
            ctx.line("w.writeString(" + m + ", " + std::to_string(*f.length) + ", " + std::to_string(pad) + ");");
        } else ctx.line("w.writeString(" + m + ", " + m + ".length(), 0);");
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
                             const analyzer::TypeIndex& index, const std::string& pfx) {
    for (const auto& child : children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            if (f->present_when) {
                ctx.line("if (" + j_expr(*f->present_when, pfx) + ") {");
                ctx.indent(); emit_j_field_decode(ctx, *f, index, pfx); ctx.dedent(); ctx.line("}");
            } else emit_j_field_decode(ctx, *f, index, pfx);
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            ctx.line(pfx + "." + j_field(sd->name) + " = " + j_class(sd->name) + ".decode(r);");
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            std::string m = pfx + "." + j_field(ad->name);
            std::string elem = ad->type_ref.empty() ? j_class(ad->name) : j_class(ad->type_ref);
            if (ad->fixed_count) {
                ctx.line("for (int _i=0; _i<" + std::to_string(*ad->fixed_count) + "; _i++) " + m + ".add(" + elem + ".decode(r));");
            } else if (ad->count_from) {
                ctx.line("for (int _i=0; _i<(int)(" + j_expr(*ad->count_from, pfx) + "); _i++) " + m + ".add(" + elem + ".decode(r));");
            } else {
                ctx.line("while (r.remainingBytes() > 0) " + m + ".add(" + elem + ".decode(r));");
            }
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            if (!cd->switch_expr) continue;
            std::string sv = j_expr(*cd->switch_expr, pfx);
            std::string m = pfx + "." + j_field(cd->name);
            bool first = true;
            for (const auto& cs : cd->cases) {
                std::string cond = sv + " == " + (cs.value ? *cs.value : "0");
                ctx.line(std::string(first ? "if (" : "} else if (") + cond + ") {");
                ctx.indent();
                std::string et = cs.type_ref.empty() ? j_class(cs.name) : j_class(cs.type_ref);
                ctx.line(m + " = " + et + ".decode(r);");
                ctx.dedent();
                first = false;
            }
            if (cd->otherwise) {
                ctx.line("} else {");
                ctx.indent();
                std::string et = cd->otherwise->type_ref.empty() ? j_class(cd->otherwise->name) : j_class(cd->otherwise->type_ref);
                ctx.line(m + " = " + et + ".decode(r);");
                ctx.dedent();
            }
            if (!first) ctx.line("}");
        } else if (auto* res = std::get_if<model::Reserved>(&child)) {
            ctx.line("r.skipBits(" + std::to_string(res->bits) + ");");
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            emit_j_decode_children(ctx, fx->children, index, pfx);
        }
    }
}

void emit_j_encode_children(EmitContext& ctx, const std::vector<model::StructChild>& children,
                             const analyzer::TypeIndex& index, const std::string& pfx) {
    for (const auto& child : children) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            if (f->present_when) {
                ctx.line("if (" + j_expr(*f->present_when, pfx) + ") {");
                ctx.indent(); emit_j_field_encode(ctx, *f, index, pfx); ctx.dedent(); ctx.line("}");
            } else emit_j_field_encode(ctx, *f, index, pfx);
        } else if (auto* sd = std::get_if<model::StructDef>(&child)) {
            ctx.line("if (" + pfx + "." + j_field(sd->name) + " != null) " + pfx + "." + j_field(sd->name) + ".encode(w);");
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            ctx.line("for (var _item : " + pfx + "." + j_field(ad->name) + ") _item.encode(w);");
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            // Encode by calling encode on the variant object directly
            // This requires all choice types to have an encode(BitWriter) method
            std::string m = pfx + "." + j_field(cd->name);
            // Emit type-checked encoding for each case
            bool first = true;
            for (const auto& cs : cd->cases) {
                std::string et = cs.type_ref.empty() ? j_class(cs.name) : j_class(cs.type_ref);
                ctx.line(std::string(first ? "if" : "} else if") + " (" + m + " instanceof " + et + " _cv) {");
                ctx.indent();
                ctx.line("_cv.encode(w);");
                ctx.dedent();
                first = false;
            }
            if (!first) ctx.line("}");
        } else if (auto* res = std::get_if<model::Reserved>(&child)) {
            ctx.line("w.writeBits(0, " + std::to_string(res->bits) + ");");
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            emit_j_encode_children(ctx, fx->children, index, pfx);
        }
    }
}

// Generate one Java class file for a struct or message
std::string generate_j_class(const std::string& name,
                              const std::vector<model::StructChild>& children,
                              const analyzer::TypeIndex& index,
                              const std::string& pkg,
                              const std::unordered_map<std::string, uint64_t>& tid_map,
                              const std::string& msg_id = "") {
    std::string cn = j_class(name);
    std::vector<JFieldDef> fields;
    collect_j_fields(children, index, fields);

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

    // decode
    ctx.line("public static " + cn + " decode(BitReader r) {");
    ctx.indent();
    ctx.line(cn + " result = new " + cn + "();");
    emit_j_decode_children(ctx, children, index, "result");
    ctx.line("return result;");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // decode from bytes
    ctx.line("public static " + cn + " decodeBytes(byte[] data) { return decode(new BitReader(data)); }");
    ctx.line();

    // encode
    ctx.line("public void encode(BitWriter w) {");
    ctx.indent();
    emit_j_encode_children(ctx, children, index, "this");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

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
    // Replace :: with .
    std::string java_pkg = pkg;
    for (size_t i = 0; i < java_pkg.size(); i++) {
        if (java_pkg[i] == ':') java_pkg[i] = '.';
    }
    // Remove consecutive dots
    std::string clean_pkg;
    for (size_t i = 0; i < java_pkg.size(); i++) {
        if (java_pkg[i] == '.' && !clean_pkg.empty() && clean_pkg.back() == '.') continue;
        if (java_pkg[i] != ':') clean_pkg += java_pkg[i];
    }
    java_pkg = clean_pkg.empty() ? "io.conduit.gen" : clean_pkg;

    bool ok = true;

    // Infrastructure files
    ok &= write_file(output_dir / "BitReader.java", generate_j_bit_reader(java_pkg));
    ok &= write_file(output_dir / "BitWriter.java", generate_j_bit_writer(java_pkg));
    ok &= write_file(output_dir / "ConduitCodecException.java", generate_j_exception(java_pkg));
    ok &= write_file(output_dir / "Constants.java", generate_j_constants(protocol, java_pkg));
    ok &= write_file(output_dir / "Protocol.java", generate_j_protocol(protocol, sessions, java_pkg));

    int file_count = 5;

    // Type wrapper files (one per type)
    std::string type_code = generate_j_types(protocol, java_pkg);
    if (!type_code.empty()) {
        // Split by class and write each to its own file
        // For simplicity, write all enum/type classes to individual files
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
                    tctx.line("public static " + name + " decode(BitReader r) { return new " + name + "(r." + rd + "(" + std::to_string(t.bits) + ")); }");
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

    // Struct classes
    std::unordered_map<std::string, uint64_t> empty;
    for (const auto& sd : protocol.structs) {
        std::string code = generate_j_class(sd.name, sd.children, index, java_pkg, empty);
        ok &= write_file(output_dir / (j_class(sd.name) + ".java"), code);
        file_count++;
    }

    // Message classes
    for (const auto& md : protocol.messages) {
        std::string code = generate_j_class(md.name, md.children, index, java_pkg, tid_map, md.id);
        ok &= write_file(output_dir / (j_class(md.name) + ".java"), code);
        file_count++;
    }

    if (ok) Logger::info("generated " + std::to_string(file_count) + " Java files in " + output_dir.string());
    return ok;
}

} // namespace bgen::codegen
