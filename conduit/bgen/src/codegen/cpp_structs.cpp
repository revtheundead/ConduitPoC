// SPDX-License-Identifier: MIT
// Bgen - Struct/Message Code Generator: Core (constructor, struct/message emission, accessors, bitmap)

#include "cpp_structs.hpp"
#include "cpp_structs_emitter.hpp"
#include "cpp_enum_emitter.hpp"
#include "../logger.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <sstream>

namespace bgen::codegen {

namespace { constexpr int BITS_PER_BYTE = 8; }

// ============================================================================
// Frame class emission (v2 frame-based protocols)
// ============================================================================

namespace {

// Convert an Expr to C++ code in frame decode context (result_var = "result")
std::string emit_frame_expr(const model::Expr& expr) {
    switch (expr.op) {
        case model::ExprOp::NumberLit:
            return std::to_string(expr.number_value);
        case model::ExprOp::BoolLit:
            return expr.bool_value ? "true" : "false";
        case model::ExprOp::FieldRef:
            return "result." + to_member_name(expr.name);
        case model::ExprOp::ConstantRef:
            return expr.name;
        case model::ExprOp::Remaining:
            return "(r.remaining_bits() / 8)";
        case model::ExprOp::Add:
            return "(" + emit_frame_expr(*expr.left) + " + " + emit_frame_expr(*expr.right) + ")";
        case model::ExprOp::Sub:
            return "(" + emit_frame_expr(*expr.left) + " - " + emit_frame_expr(*expr.right) + ")";
        case model::ExprOp::Mul:
            return "(" + emit_frame_expr(*expr.left) + " * " + emit_frame_expr(*expr.right) + ")";
        case model::ExprOp::Div:
            return "(" + emit_frame_expr(*expr.left) + " / " + emit_frame_expr(*expr.right) + ")";
        case model::ExprOp::Mod:
            return "(" + emit_frame_expr(*expr.left) + " % " + emit_frame_expr(*expr.right) + ")";
        case model::ExprOp::Eq:
            return "(" + emit_frame_expr(*expr.left) + " == " + emit_frame_expr(*expr.right) + ")";
        case model::ExprOp::Neq:
            return "(" + emit_frame_expr(*expr.left) + " != " + emit_frame_expr(*expr.right) + ")";
        case model::ExprOp::Lt:
            return "(" + emit_frame_expr(*expr.left) + " < " + emit_frame_expr(*expr.right) + ")";
        case model::ExprOp::Lte:
            return "(" + emit_frame_expr(*expr.left) + " <= " + emit_frame_expr(*expr.right) + ")";
        case model::ExprOp::Gt:
            return "(" + emit_frame_expr(*expr.left) + " > " + emit_frame_expr(*expr.right) + ")";
        case model::ExprOp::Gte:
            return "(" + emit_frame_expr(*expr.left) + " >= " + emit_frame_expr(*expr.right) + ")";
        case model::ExprOp::LogAnd:
            return "(" + emit_frame_expr(*expr.left) + " && " + emit_frame_expr(*expr.right) + ")";
        case model::ExprOp::LogOr:
            return "(" + emit_frame_expr(*expr.left) + " || " + emit_frame_expr(*expr.right) + ")";
        case model::ExprOp::BitAnd:
            return "(" + emit_frame_expr(*expr.left) + " & " + emit_frame_expr(*expr.right) + ")";
        case model::ExprOp::BitOr:
            return "(" + emit_frame_expr(*expr.left) + " | " + emit_frame_expr(*expr.right) + ")";
        case model::ExprOp::BitXor:
            return "(" + emit_frame_expr(*expr.left) + " ^ " + emit_frame_expr(*expr.right) + ")";
        case model::ExprOp::ShiftLeft:
            return "(" + emit_frame_expr(*expr.left) + " << " + emit_frame_expr(*expr.right) + ")";
        case model::ExprOp::ShiftRight:
            return "(" + emit_frame_expr(*expr.left) + " >> " + emit_frame_expr(*expr.right) + ")";
        case model::ExprOp::Negate:
            return "(-" + emit_frame_expr(*expr.left) + ")";
        case model::ExprOp::BitNot:
            return "(~" + emit_frame_expr(*expr.left) + ")";
        case model::ExprOp::LogNot:
            return "(!" + emit_frame_expr(*expr.left) + ")";
    }
    throw std::logic_error("unhandled ExprOp in emit_frame_expr: " +
                           std::to_string(static_cast<int>(expr.op)));
}

// Emit a frame field write: for auto="length", writes a zero placeholder
// and stores the byte offset. For other fields, writes the member value.
void emit_frame_field_write(EmitContext& ctx, const model::Field& f,
                            const FieldTypeInfo& fti,
                            [[maybe_unused]] const analyzer::TypeIndex& index,
                            const analyzer::SessionInfo& session) {
    std::string member = to_member_name(f.name);
    if (f.auto_expr && f.auto_expr->kind == model::AutoKind::Length) {
        ctx.line("auto length_byte_pos_ = w.size_bytes();");
        // Write zero placeholder for backpatching
        FieldTypeInfo zero_fti = fti;
        emit_write_stmt(ctx, "0", zero_fti, f.endian);
        // For length(payload), record the position AFTER the placeholder
        // so payload_start_pos_ marks where the payload actually begins
        if (!f.auto_expr->field_ref.empty() && f.auto_expr->field_ref == "payload") {
            ctx.line("auto payload_start_pos_ = w.size_bytes();");
        }
    } else if (f.auto_expr && f.auto_expr->kind == model::AutoKind::Count) {
        // Auto-count: write count of payload items (for array payloads)
        std::string cast_type = storage_type_for_bits(fti.bits, fti.is_signed);
        if (f.auto_expr->field_ref == "payload" && session.payload_is_array) {
            emit_write_stmt(ctx, "static_cast<" + cast_type + ">(payload_.size())", fti, f.endian);
        } else {
            // Fallback: write the member value
            emit_write_stmt(ctx, member, fti, f.endian);
        }
    } else if (f.constraint && f.constraint->equals) {
        // Constraint-equals: always write the constraint value (e.g., sync words)
        std::string cast_val = "static_cast<" + storage_type_for_bits(fti.bits, fti.is_signed) + ">(" + *f.constraint->equals + ")";
        emit_write_stmt(ctx, cast_val, fti, f.endian);
    } else {
        std::string value = member;
        // Enum fields: cast to underlying storage for write
        if (fti.is_enum) {
            value = "static_cast<" + storage_type_for_bits(fti.bits, fti.is_signed) + ">(" + member + ")";
        }
        emit_write_stmt(ctx, value, fti, f.endian);
    }
}

// Emit frame field read into result.member_
void emit_frame_field_read(EmitContext& ctx, const model::Field& f,
                           const FieldTypeInfo& fti,
                           [[maybe_unused]] const analyzer::TypeIndex& index) {
    std::string member = to_member_name(f.name);
    std::string var = to_accessor_name(f.name) + "_val_";
    std::string read_expr = emit_read_expr(fti, f.endian);
    ctx.line("auto " + var + " = " + read_expr + ";");
    ctx.line("if (!" + var + ") return std::unexpected(" + var + ".error());");
    if (fti.is_enum) {
        ctx.line("result." + member + " = static_cast<" + fti.cpp_type + ">(*" + var + ");");
    } else {
        ctx.line("result." + member + " = *" + var + ";");
    }
    // constraint-equals fields (e.g., sync words) are encode-only constraints.
    // Frame::decode reads the value but does not validate it.
}

// Emit the length backpatch after payload and footer encoding
void emit_frame_length_backpatch(EmitContext& ctx, const model::Field& f,
                                 const FieldTypeInfo& fti) {
    if (!f.auto_expr) return;
    const auto& auto_expr = *f.auto_expr;
    bool payload_only = !auto_expr.field_ref.empty() && auto_expr.field_ref == "payload";
    std::string raw_length;
    if (payload_only) {
        raw_length = "w.size_bytes() - payload_start_pos_";
    } else {
        raw_length = "w.size_bytes() - frame_start_pos_";
    }
    std::string length_expr = apply_arith(raw_length, auto_expr.modifier);
    std::string cast_type = storage_type_for_bits(fti.bits, false);
    std::string patch_call;
    if (fti.bits <= 8) {
        patch_call = "w.patch_u8(length_byte_pos_, static_cast<" + cast_type + ">(" + length_expr + "))";
    } else if (fti.bits <= 16) {
        patch_call = "w.patch_u16(length_byte_pos_, static_cast<" + cast_type + ">(" + length_expr + "), "
                     + endian_str(f.endian) + ")";
    } else {
        patch_call = "w.patch_u32(length_byte_pos_, static_cast<" + cast_type + ">(" + length_expr + "), "
                     + endian_str(f.endian) + ")";
    }
    ctx.line("if (!" + patch_call + ")");
    ctx.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::InvalidArgument, \"failed to patch frame length\"));");
}

void emit_frame_class(EmitContext& ctx, const model::FrameDef& frame,
                      const analyzer::TypeIndex& index,
                      const analyzer::SessionInfo& session,
                      const std::string& ns) {
    std::string class_name = to_cpp_type_name(frame.name);

    // Build payload variant type list (all leaf message types)
    std::string variant_types;
    for (const auto& lt : session.leaf_types) {
        if (!variant_types.empty()) variant_types += ", ";
        variant_types += to_cpp_type_name(lt.name);
    }

    ctx.line("class " + class_name + " {");
    ctx.line("public:");
    ctx.indent();

    // Payload variant type alias
    ctx.line("using PayloadVariant = std::variant<" + variant_types + ">;");
    if (session.payload_is_array) {
        ctx.line("using PayloadContainer = std::vector<PayloadVariant>;");
    }
    ctx.line();

    // Header field accessors
    for (const auto& child : frame.header_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fti = resolve_field_type(*f, index);
            std::string accessor = to_accessor_name(f->name);
            std::string member = to_member_name(f->name);
            ctx.line(fti.cpp_type + " " + accessor + "() const { return " + member + "; }");
            ctx.line("void set_" + accessor + "(" + fti.cpp_type + " v) { " + member + " = v; }");
        }
    }

    // Footer field accessors
    for (const auto& child : frame.footer_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fti = resolve_field_type(*f, index);
            std::string accessor = to_accessor_name(f->name);
            std::string member = to_member_name(f->name);
            ctx.line(fti.cpp_type + " " + accessor + "() const { return " + member + "; }");
            ctx.line("void set_" + accessor + "(" + fti.cpp_type + " v) { " + member + " = v; }");
        }
    }
    ctx.line();

    // Payload accessor
    if (session.payload_is_array) {
        ctx.line("const PayloadContainer& payload() const { return payload_; }");
        ctx.line("PayloadContainer& payload() { return payload_; }");
    } else {
        ctx.line("const PayloadVariant& payload() const { return payload_; }");
        ctx.line("PayloadVariant& payload() { return payload_; }");
    }
    ctx.line();

    // Helper: emit frame field setup in wrap()
    // - constraint-equals: set to constant value
    // - auto-managed: skip (computed during encode)
    // - other: copy from message to frame
    auto emit_wrap_frame_fields = [&](const std::vector<model::StructChild>& children,
                                       bool copy_from_msg) {
        for (const auto& child : children) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                std::string member = to_member_name(f->name);
                if (f->constraint && f->constraint->equals) {
                    auto fti = resolve_field_type(*f, index);
                    ctx.line("frame." + member + " = static_cast<" +
                             fti.cpp_type + ">(" + *f->constraint->equals + ");");
                } else if (f->auto_expr) {
                    // Auto-managed: skip (computed during encode)
                } else if (copy_from_msg) {
                    ctx.line("frame." + member + " = msg." + member + ";");
                }
            }
        }
    };

    // wrap() overloads — one per message type
    for (const auto& lt : session.leaf_types) {
        std::string msg_type = to_cpp_type_name(lt.name);
        ctx.line("static " + class_name + " wrap(const " + msg_type + "& msg) {");
        ctx.indent();
        ctx.line(class_name + " frame;");
        emit_wrap_frame_fields(frame.header_fields, true);
        emit_wrap_frame_fields(frame.footer_fields, true);
        // Set id field from message's ID_VALUE
        if (!session.id_field_name.empty()) {
            ctx.line("frame." + to_member_name(session.id_field_name) + " = " + msg_type + "::ID_VALUE;");
        }
        if (session.payload_is_array) {
            ctx.line("frame.payload_.push_back(msg);");
        } else {
            ctx.line("frame.payload_ = msg;");
        }
        ctx.line("return frame;");
        ctx.dedent();
        ctx.line("}");
        ctx.line();
    }

    // Batch wrap() overloads — one per message type (array-payload only)
    if (session.payload_is_array) {
        for (const auto& lt : session.leaf_types) {
            std::string msg_type = to_cpp_type_name(lt.name);
            ctx.line("static " + class_name + " wrap(std::span<const " + msg_type + "> msgs) {");
            ctx.indent();
            ctx.line(class_name + " frame;");
            // Batch wrap: set constraint-equals and skip auto-managed (no msg to copy from)
            emit_wrap_frame_fields(frame.header_fields, false);
            emit_wrap_frame_fields(frame.footer_fields, false);
            if (!session.id_field_name.empty()) {
                ctx.line("frame." + to_member_name(session.id_field_name) + " = " + msg_type + "::ID_VALUE;");
            }
            ctx.line("if (!msgs.empty()) {");
            ctx.indent();
            ctx.line("frame.payload_.reserve(msgs.size());");
            ctx.line("for (const auto& m : msgs)");
            ctx.line("    frame.payload_.push_back(m);");
            ctx.dedent();
            ctx.line("}");
            ctx.line("return frame;");
            ctx.dedent();
            ctx.line("}");
            ctx.line();
        }
    }

    // encode()
    ctx.line("conduit::VoidResult encode(conduit::io::BitWriter& w) const {");
    ctx.indent();

    // Find the length field for backpatching
    const model::Field* length_field = nullptr;
    FieldTypeInfo length_fti;
    bool length_is_total_frame = false;
    for (const auto& child : frame.header_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            if (f->auto_expr && f->auto_expr->kind == model::AutoKind::Length) {
                length_field = f;
                length_fti = resolve_field_type(*f, index);
                length_is_total_frame = f->auto_expr->field_ref.empty();
            }
        }
    }
    // Record writer position at frame start for relative length computation
    if (length_is_total_frame) {
        ctx.line("auto frame_start_pos_ = w.size_bytes();");
    }

    // Write header fields
    for (const auto& child : frame.header_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fti = resolve_field_type(*f, index);
            emit_frame_field_write(ctx, *f, fti, index, session);
        }
    }

    // Write payload
    if (session.payload_is_array) {
        ctx.line("for (const auto& item : payload_) {");
        ctx.indent();
        ctx.line("CONDUIT_TRY(std::visit([&w](const auto& m) -> conduit::VoidResult {");
        ctx.line("    return m.encode(w);");
        ctx.line("}, item));");
        ctx.dedent();
        ctx.line("}");
    } else {
        ctx.line("CONDUIT_TRY(std::visit([&w](const auto& m) -> conduit::VoidResult {");
        ctx.line("    return m.encode(w);");
        ctx.line("}, payload_));");
    }

    // Write footer fields
    for (const auto& child : frame.footer_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fti = resolve_field_type(*f, index);
            emit_frame_field_write(ctx, *f, fti, index, session);
        }
    }

    // Backpatch length
    if (length_field) {
        emit_frame_length_backpatch(ctx, *length_field, length_fti);
    }

    ctx.line("return {};");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // decode()
    ctx.line("static conduit::Result<" + class_name + "> decode(conduit::io::BitReader& r) {");
    ctx.indent();
    ctx.line(class_name + " result;");

    // Read header fields
    for (const auto& child : frame.header_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fti = resolve_field_type(*f, index);
            emit_frame_field_read(ctx, *f, fti, index);
        }
    }

    // Find the id field for dispatch
    std::string id_member = to_member_name(session.id_field_name);
    // Resolve id field C++ type for the switch cast
    std::string id_cast_type;
    for (const auto& child : frame.header_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            if (f->name == session.id_field_name) {
                auto fti = resolve_field_type(*f, index);
                id_cast_type = fti.is_enum
                    ? storage_type_for_bits(fti.bits, fti.is_signed)
                    : fti.cpp_type;
                break;
            }
        }
    }

    // Collect header/footer field member names for copying into messages
    std::vector<std::string> header_members;
    for (const auto& child : frame.header_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            header_members.push_back(to_member_name(f->name));
        }
    }
    std::vector<std::string> footer_members;
    for (const auto& child : frame.footer_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            footer_members.push_back(to_member_name(f->name));
        }
    }

    // Helper: emit statements to copy header frame fields into payload_val_
    auto emit_copy_header = [&]() {
        for (const auto& m : header_members) {
            ctx.line("payload_val_->" + m + " = result." + m + ";");
        }
    };

    // Compute footer and header sizes for payload bounds (used in both array and single paths)
    int footer_bits = 0;
    for (const auto& child : frame.footer_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto ffti = resolve_field_type(*f, index);
            footer_bits += ffti.bits;
        }
    }
    auto footer_bytes = static_cast<size_t>((footer_bits + 7) / 8);
    int header_bits = 0;
    for (const auto& child : frame.header_fields) {
        if (auto* f = std::get_if<model::Field>(&child))
            header_bits += resolve_field_type(*f, index).bits;
        else if (auto* r = std::get_if<model::Reserved>(&child))
            header_bits += r->bits;
    }
    auto header_bytes = static_cast<size_t>((header_bits + 7) / 8);

    // Dispatch on id value
    if (session.payload_is_array) {
        // Array payload: decode records from the payload region.
        std::string reader_name = "r";
        if (!session.length_field_name.empty() && session.count_field_name.empty()) {
            // Use the length field to create a bounded sub-reader for payload.
            std::string len_member = to_member_name(session.length_field_name);
            std::string raw_val = "static_cast<size_t>(result." + len_member + ")";
            std::string total_expr = reverse_arith(raw_val, session.frame_length_modifier);
            std::string size_expr;
            if (session.frame_length_field_ref.empty()) {
                // Total frame length: payload = total - header - footer
                auto overhead = header_bytes + footer_bytes;
                size_expr = total_expr + " - " + std::to_string(overhead);
            } else {
                // Payload-only length: payload = value - footer
                if (footer_bytes > 0) {
                    size_expr = total_expr + " - " + std::to_string(footer_bytes);
                } else {
                    size_expr = total_expr;
                }
            }
            ctx.line("auto payload_reader_ = r.sub_reader(" + size_expr + ");");
            ctx.line("if (!payload_reader_) return std::unexpected(payload_reader_.error());");
            reader_name = "(*payload_reader_)";
        } else if (footer_bits > 0 && session.count_field_name.empty()) {
            // No length field for bounding but need to exclude footer bytes.
            ctx.line("if (r.remaining_bytes() < " + std::to_string(footer_bytes) + ")");
            ctx.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::BufferUnderrun, \"frame too short for footer\"));");
            ctx.line("auto payload_reader_ = r.sub_reader(r.remaining_bytes() - " + std::to_string(footer_bytes) + ");");
            ctx.line("if (!payload_reader_) return std::unexpected(payload_reader_.error());");
            reader_name = "(*payload_reader_)";
        } else if (session.payload_length_from) {
            std::string size_expr = "static_cast<size_t>(" + emit_frame_expr(*session.payload_length_from) + ")";
            ctx.line("auto payload_reader_ = r.sub_reader(" + size_expr + ");");
            ctx.line("if (!payload_reader_) return std::unexpected(payload_reader_.error());");
            reader_name = "(*payload_reader_)";
        }
        // When a count field is available, use count-bounded iteration;
        // otherwise fall back to reading until payload bytes are exhausted.
        if (!session.count_field_name.empty()) {
            std::string count_member = to_member_name(session.count_field_name);
            ctx.line("for (size_t i_ = 0; i_ < static_cast<size_t>(result." + count_member + "); ++i_) {");
        } else {
            ctx.line("while (" + reader_name + ".remaining_bytes() > 0) {");
        }
        ctx.indent();
        // Each record in the array has the same message type from the id
        ctx.line("switch (static_cast<" + id_cast_type + ">(result." + id_member + ")) {");
        ctx.indent();
        for (const auto& lt : session.leaf_types) {
            if (lt.send_only) continue; // skip send-only in decode
            std::string msg_type = to_cpp_type_name(lt.name);
            std::string id_val = lt.constraints.empty() ? "0" : lt.constraints[0].second;
            ctx.line("case " + id_val + ": {");
            ctx.indent();
            ctx.line("auto payload_val_ = " + msg_type + "::decode(" + reader_name + ");");
            ctx.line("if (!payload_val_) return std::unexpected(payload_val_.error());");
            emit_copy_header();
            ctx.line("result.payload_.push_back(std::move(*payload_val_));");
            ctx.line("break;");
            ctx.dedent();
            ctx.line("}");
        }
        ctx.line("default:");
        ctx.indent();
        ctx.line("return std::unexpected(conduit::Error(conduit::ErrorCode::UnknownDiscriminator,");
        ctx.line("    \"unknown frame message id: \" + std::to_string(static_cast<int>(result." + id_member + "))));");
        ctx.dedent();
        ctx.dedent();
        ctx.line("}");
        ctx.dedent();
        ctx.line("}");
        // sub_reader() already advanced r past the payload bytes
    } else {
        // Single payload dispatch
        // When payload length is available, create a bounded sub-reader
        std::string single_reader = "r";
        if (!session.length_field_name.empty() && !session.frame_length_field_ref.empty()
            && session.frame_length_field_ref == "payload") {
            std::string length_member = to_member_name(session.length_field_name);
            std::string raw_val = "static_cast<size_t>(result." + length_member + ")";
            std::string size_expr = reverse_arith(raw_val, session.frame_length_modifier);
            ctx.line("auto payload_reader_ = r.sub_reader(" + size_expr + ");");
            ctx.line("if (!payload_reader_) return std::unexpected(payload_reader_.error());");
            single_reader = "(*payload_reader_)";
        } else if (!session.length_field_name.empty() && session.frame_length_field_ref.empty()
                   && footer_bits > 0) {
            // Total frame length with footer: bound payload to total - header - footer
            std::string length_member = to_member_name(session.length_field_name);
            std::string raw_val = "static_cast<size_t>(result." + length_member + ")";
            std::string total_expr = reverse_arith(raw_val, session.frame_length_modifier);
            auto overhead = header_bytes + footer_bytes;
            std::string size_expr = total_expr + " - " + std::to_string(overhead);
            ctx.line("auto payload_reader_ = r.sub_reader(" + size_expr + ");");
            ctx.line("if (!payload_reader_) return std::unexpected(payload_reader_.error());");
            single_reader = "(*payload_reader_)";
        } else if (footer_bits > 0 && session.length_field_name.empty()) {
            // No length field but footer present: bound payload by remaining - footer
            ctx.line("if (r.remaining_bytes() < " + std::to_string(footer_bytes) + ")");
            ctx.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::BufferUnderrun, \"frame too short for footer\"));");
            ctx.line("auto payload_reader_ = r.sub_reader(r.remaining_bytes() - " + std::to_string(footer_bytes) + ");");
            ctx.line("if (!payload_reader_) return std::unexpected(payload_reader_.error());");
            single_reader = "(*payload_reader_)";
        } else if (session.payload_length_from) {
            std::string size_expr = "static_cast<size_t>(" + emit_frame_expr(*session.payload_length_from) + ")";
            ctx.line("auto payload_reader_ = r.sub_reader(" + size_expr + ");");
            ctx.line("if (!payload_reader_) return std::unexpected(payload_reader_.error());");
            single_reader = "(*payload_reader_)";
        }

        ctx.line("switch (static_cast<" + id_cast_type + ">(result." + id_member + ")) {");
        ctx.indent();

        // Group leaves by id to handle direction pairs
        std::map<std::string, std::vector<const analyzer::LeafTypeInfo*>> id_groups;
        for (const auto& lt : session.leaf_types) {
            std::string id_val = lt.constraints.empty() ? "0" : lt.constraints[0].second;
            id_groups[id_val].push_back(&lt);
        }

        for (const auto& [id_val, leaves] : id_groups) {
            // For decode, prefer the receive or both-direction type
            const analyzer::LeafTypeInfo* decode_leaf = leaves[0];
            for (const auto* lt : leaves) {
                if (!lt->send_only) { decode_leaf = lt; break; }
            }
            std::string msg_type = to_cpp_type_name(decode_leaf->name);
            ctx.line("case " + id_val + ": {");
            ctx.indent();
            ctx.line("auto payload_val_ = " + msg_type + "::decode(" + single_reader + ");");
            ctx.line("if (!payload_val_) return std::unexpected(payload_val_.error());");
            emit_copy_header();
            ctx.line("result.payload_ = std::move(*payload_val_);");
            ctx.line("break;");
            ctx.dedent();
            ctx.line("}");
        }

        ctx.line("default:");
        ctx.indent();
        ctx.line("return std::unexpected(conduit::Error(conduit::ErrorCode::UnknownDiscriminator,");
        ctx.line("    \"unknown frame message id: \" + std::to_string(static_cast<int>(result." + id_member + "))));");
        ctx.dedent();
        ctx.dedent();
        ctx.line("}");
    }

    // Read footer fields
    for (const auto& child : frame.footer_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fti = resolve_field_type(*f, index);
            emit_frame_field_read(ctx, *f, fti, index);
        }
    }

    // Copy footer fields into decoded messages
    if (!footer_members.empty()) {
        if (session.payload_is_array) {
            ctx.line("for (auto& item_ : result.payload_) {");
            ctx.indent();
            ctx.line("std::visit([&](auto& msg_) {");
            ctx.indent();
            for (const auto& m : footer_members) {
                ctx.line("msg_." + m + " = result." + m + ";");
            }
            ctx.dedent();
            ctx.line("}, item_);");
            ctx.dedent();
            ctx.line("}");
        } else {
            ctx.line("std::visit([&](auto& msg_) {");
            ctx.indent();
            for (const auto& m : footer_members) {
                ctx.line("msg_." + m + " = result." + m + ";");
            }
            ctx.dedent();
            ctx.line("}, result.payload_);");
        }
    }

    ctx.line("return result;");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // Convenience encode_bytes / decode_bytes
    ctx.line("conduit::Result<std::vector<uint8_t>> encode_bytes() const {");
    ctx.indent();
    ctx.line("conduit::io::BitWriter w;");
    ctx.line("CONDUIT_TRY(encode(w));");
    ctx.line("return w.finish();");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    ctx.line("static conduit::Result<" + class_name + "> decode_bytes(std::span<const uint8_t> data) {");
    ctx.indent();
    ctx.line("conduit::io::BitReader r(data);");
    ctx.line("return decode(r);");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // to_string()
    ctx.line("std::string to_string() const {");
    ctx.indent();
    ctx.line("std::ostringstream oss;");
    ctx.line("oss << \"" + class_name + "{\"");
    ctx.indent();

    bool first = true;
    auto emit_frame_field_str = [&](const model::Field& f) {
        auto fti = resolve_field_type(f, index);
        std::string sep = first ? "" : ", ";
        std::string member = to_member_name(f.name);
        first = false;

        if (fti.is_enum) {
            ctx.line("<< \"" + sep + f.name + "=\" << ::" + ns + "::to_string(" + member + ")");
        } else if (fti.is_struct) {
            // Typedef wrapper — use .raw() with unary + for safe uint8_t display
            ctx.line("<< \"" + sep + f.name + "=\" << +(" + member + ".raw())");
        } else {
            // Plain numeric
            ctx.line("<< \"" + sep + f.name + "=\" << +(" + member + ")");
        }
    };

    for (const auto& child : frame.header_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            emit_frame_field_str(*f);
        }
    }

    // Payload
    std::string sep = first ? "" : ", ";
    if (session.payload_is_array) {
        ctx.line("<< \"" + sep + "payload=[\"");
        ctx.line(";");
        ctx.line("for (size_t i = 0; i < payload_.size(); ++i) {");
        ctx.indent();
        ctx.line("if (i > 0) oss << \", \";");
        ctx.line("oss << std::visit([](const auto& m) { return m.to_string(); }, payload_[i]);");
        ctx.dedent();
        ctx.line("}");
        ctx.line("oss << \"]\"");
    } else {
        ctx.line("<< \"" + sep + "payload=\" << std::visit([](const auto& m) { return m.to_string(); }, payload_)");
    }
    first = false;

    for (const auto& child : frame.footer_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            emit_frame_field_str(*f);
        }
    }

    ctx.line("<< \"}\";");
    ctx.dedent();
    ctx.line("return oss.str();");
    ctx.dedent();
    ctx.line("}");
    ctx.line();

    // Private section
    ctx.dedent();
    ctx.line("private:");
    ctx.indent();

    // Header field members
    for (const auto& child : frame.header_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fti = resolve_field_type(*f, index);
            ctx.line(fti.cpp_type + " " + to_member_name(f->name) + "{};");
        }
    }

    // Payload member
    if (session.payload_is_array) {
        ctx.line("PayloadContainer payload_;");
    } else {
        ctx.line("PayloadVariant payload_;");
    }

    // Footer field members
    for (const auto& child : frame.footer_fields) {
        if (auto* f = std::get_if<model::Field>(&child)) {
            auto fti = resolve_field_type(*f, index);
            ctx.line(fti.cpp_type + " " + to_member_name(f->name) + "{};");
        }
    }

    ctx.dedent();
    ctx.line("};");
    ctx.line();
}

} // anonymous namespace (frame emission)

// ============================================================================
// StructEmitter constructor and inline public methods
// ============================================================================

StructEmitter::StructEmitter(EmitContext& ctx, const analyzer::TypeIndex& index,
                              const analyzer::WireSizeInfo& sizes, const std::string& ns)
    : ctx_(ctx), index_(index), sizes_(sizes), ns_(ns) {
    // Build enum value lookup map for O(1) resolution.
    // On collision (same value name in different enum types), mark the entry
    // as ambiguous so resolve_enum_value returns the raw name (fallback).
    // Use an empty string as the sentinel for ambiguous entries.
    for (const auto& [type_name, td] : index.types) {
        for (const auto& ev : td->enum_values) {
            auto [it, inserted] = enum_value_lookup_.try_emplace(
                ev.name, to_cpp_type_name(type_name) + "::" + to_enum_value_name(ev.name));
            if (!inserted) {
                // Collision: mark as ambiguous (empty sentinel)
                it->second.clear();
            }
        }
    }
}

void StructEmitter::set_leaf_type_ids(const std::unordered_map<std::string, uint64_t>& map) {
    leaf_type_ids_ = map;
}

void StructEmitter::set_current_session(const analyzer::SessionInfo* session) {
    current_session_ = session;
}

bool StructEmitter::is_byte_aligned() const {
    return bit_mod8_ == 0;
}

void StructEmitter::advance_bits(int bits) {
    if (bit_mod8_ < 0) return; // already unknown
    bit_mod8_ = (bit_mod8_ + bits) % BITS_PER_BYTE;
}

void StructEmitter::advance_bits_variable() {
    if (bit_mod8_ != 0) bit_mod8_ = -1;
    // else: stays 0 (byte-aligned → still byte-aligned after whole-byte field)
}

void StructEmitter::advance_fx_field_bits(const model::Field& f, const FieldTypeInfo& fti) {
    if (fti.is_struct && !fti.is_string && !fti.is_bytes) {
        advance_bits_variable();
    } else if (fti.is_string) {
        if (f.length) advance_bits(static_cast<int>(*f.length * 8));
        else advance_bits_variable();
    } else if (fti.is_bytes) {
        if (f.length) advance_bits(static_cast<int>(*f.length * 8));
        else if (f.bytes_attr) advance_bits(static_cast<int>(*f.bytes_attr * 8));
        else advance_bits_variable();
    } else if (fti.has_field_scale) {
        advance_bits(fti.raw_bits);
    } else {
        advance_bits(fti.bits);
    }
}

void StructEmitter::reset_alignment() { bit_mod8_ = 0; fx_depth_ = 0; }

std::string StructEmitter::resolve_enum_value(const std::string& name) const {
    auto it = enum_value_lookup_.find(name);
    if (it != enum_value_lookup_.end() && !it->second.empty()) return it->second;
    return name; // fallback: return as-is (ambiguous or not found)
}

// ============================================================================
// Variant aliases and child class emission
// ============================================================================

void StructEmitter::emit_variant_aliases(const std::vector<model::StructChild>& children) {
    for (const auto& child : children) {
        if (auto* c = std::get_if<model::ChoiceDef>(&child)) {
            std::string variant_name = get_variant_alias_name(c->name);
            emitted_variant_aliases_.insert(variant_name);
            std::string cases_str;
            for (size_t i = 0; i < c->cases.size(); i++) {
                if (i > 0) cases_str += ", ";
                const auto& cs = c->cases[i];
                if (!cs.type_ref.empty()) {
                    cases_str += to_cpp_type_name(cs.type_ref);
                } else {
                    cases_str += get_child_class_name(cs.name);
                }
            }
            if (c->otherwise) {
                const auto& ow = *c->otherwise;
                if (!ow.type_ref.empty()) {
                    if (!cases_str.empty()) cases_str += ", ";
                    cases_str += to_cpp_type_name(ow.type_ref);
                } else if (!ow.children.empty()) {
                    if (!cases_str.empty()) cases_str += ", ";
                    cases_str += get_child_class_name(c->name + "Otherwise");
                }
            }
            ctx_.line("using " + variant_name + " = std::variant<" + cases_str + ">;");
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            // Recurse into FX block children to find nested choices
            emit_variant_aliases(fx->children);
        }
    }
}

void StructEmitter::emit_child_class_defs(const std::vector<model::StructChild>& children,
                                            const std::string& parent_name) {
    for (const auto& child : children) {
        if (auto* sd = std::get_if<model::StructDef>(&child)) {
            if (!sd->name.empty()) {
                // Register typeName override if present
                if (sd->type_name) {
                    register_type_name_override(parent_name, sd->name, *sd->type_name);
                }
                // Analyze outer-scope refs: child struct's expressions may reference parent fields
                analyze_outer_scope(sd->name, sd->children, children);
                emit_struct(*sd, parent_name);
            }
        } else if (auto* ad = std::get_if<model::ArrayDef>(&child)) {
            if (ad->type_ref.empty() && !ad->children.empty()) {
                // Register typeName override for array element if present
                if (ad->type_name) {
                    register_type_name_override(parent_name, ad->name + "Element", *ad->type_name);
                }
                emit_synthetic_struct(ad->name + "Element", ad->children, parent_name);
            }
        } else if (auto* cd = std::get_if<model::ChoiceDef>(&child)) {
            // Register typeName override for choice variant alias if present
            if (cd->type_name) {
                register_type_name_override(parent_name, cd->name + "Variant", *cd->type_name);
            }
            for (const auto& cs : cd->cases) {
                if (cs.type_ref.empty() && !cs.children.empty()) {
                    // Register typeName override if present
                    if (cs.type_name) {
                        register_type_name_override(parent_name, cs.name, *cs.type_name);
                    }
                    // Analyze outer-scope refs: case children may reference parent struct fields
                    analyze_outer_scope(cs.name, cs.children, children);
                    // Always prefix inline case types to prevent cross-message collisions
                    emit_synthetic_struct(cs.name, cs.children, parent_name);
                }
            }
            if (cd->otherwise && cd->otherwise->type_ref.empty() && !cd->otherwise->children.empty()) {
                std::string otherwise_name = cd->name + "Otherwise";
                // Register typeName override if present
                if (cd->otherwise->type_name) {
                    register_type_name_override(parent_name, otherwise_name, *cd->otherwise->type_name);
                }
                // Analyze outer-scope refs: otherwise children may reference parent struct fields
                analyze_outer_scope(otherwise_name, cd->otherwise->children, children);
                // Always prefix otherwise types to prevent cross-message collisions
                emit_synthetic_struct(otherwise_name, cd->otherwise->children, parent_name);
            }
        } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
            // Recurse into FX block children to find inline enums and nested types
            emit_child_class_defs(fx->children, parent_name);
        } else if (auto* f = std::get_if<model::Field>(&child)) {
            // Emit inline enum types for fields with enum_values
            if (!f->enum_values.empty() && f->type_ref.empty()) {
                std::string enum_name = to_pascal_case(parent_name) + "_" + to_pascal_case(f->name);
                if (emitted_inline_enums_.insert(enum_name).second) {
                    int bits = f->bits.value_or(8);
                    std::string underlying = storage_type_for_bits(bits, false);
                    emit_enum_class(ctx_, enum_name, underlying, bits, f->endian, f->enum_values);
                }
            }
        }
    }
}

void StructEmitter::emit_synthetic_struct(const std::string& bmdl_name,
                                           const std::vector<model::StructChild>& children,
                                           const std::string& parent_name) {
    std::string name = resolve_child_class_name(bmdl_name, parent_name);
    if (name.empty()) return;

    if (!emitted_classes_.insert(name).second) return;

    auto prev_parent = current_parent_;
    auto prev_bmdl = current_bmdl_name_;
    current_parent_ = name;
    current_bmdl_name_ = bmdl_name;

    emit_child_class_defs(children, name);
    emit_variant_aliases(children);

    // Set outer-scope params if this struct has any
    auto prev_outer = outer_scope_params_;
    auto it = struct_decode_params_.find(bmdl_name);
    if (it != struct_decode_params_.end()) {
        outer_scope_params_.clear();
        for (const auto& p : it->second) {
            outer_scope_params_[p.bmdl_name] = to_accessor_name(p.bmdl_name);
        }
    }

    ctx_.line("class " + name + " {");
    ctx_.line("public:");
    ctx_.indent();

    // Emit TYPE_ID and TYPE_NAME for leaf synthetic struct types (e.g., batch case wrappers)
    auto leaf_it = leaf_type_ids_.find(bmdl_name);
    if (leaf_it != leaf_type_ids_.end()) {
        ctx_.line("static constexpr uint64_t TYPE_ID = " + type_id_literal(leaf_it->second) + ";");
        ctx_.line("static constexpr std::string_view TYPE_NAME = \"" + bmdl_name + "\";");
        ctx_.line();
    }

    emit_plain_struct(children, name);

    current_parent_ = prev_parent;
    current_bmdl_name_ = prev_bmdl;
    outer_scope_params_ = prev_outer;

    ctx_.dedent();
    ctx_.line("};");
    ctx_.line();
}

void StructEmitter::emit_doc_comment(const std::string& doc) {
    std::istringstream stream(doc);
    std::string line;
    while (std::getline(stream, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        size_t end = line.find_last_not_of(" \t\r\n");
        if (start != std::string::npos && end != std::string::npos) {
            ctx_.line("// " + line.substr(start, end - start + 1));
        }
    }
}

// ============================================================================
// emit_struct / emit_message
// ============================================================================

void StructEmitter::emit_struct(const model::StructDef& sd, const std::string& parent_name) {
    std::string name = resolve_child_class_name(sd.name, parent_name);
    if (name.empty()) return;

    if (!emitted_classes_.insert(name).second) return;

    auto prev_parent = current_parent_;
    auto prev_bmdl = current_bmdl_name_;
    current_parent_ = name;
    current_bmdl_name_ = sd.name;

    emit_child_class_defs(sd.children, name);
    emit_variant_aliases(sd.children);

    if (!sd.doc.empty()) {
        emit_doc_comment(sd.doc);
    }

    // Set outer-scope params if this struct has any
    auto prev_outer = outer_scope_params_;
    auto osp_it = struct_decode_params_.find(sd.name);
    if (osp_it != struct_decode_params_.end()) {
        outer_scope_params_.clear();
        for (const auto& p : osp_it->second) {
            outer_scope_params_[p.bmdl_name] = to_accessor_name(p.bmdl_name);
        }
    }

    ctx_.line("class " + name + " {");
    ctx_.line("public:");
    ctx_.indent();

    // Emit TYPE_ID and TYPE_NAME for leaf struct types (used in session dispatch)
    auto leaf_it = leaf_type_ids_.find(sd.name);
    if (leaf_it != leaf_type_ids_.end()) {
        ctx_.line("static constexpr uint64_t TYPE_ID = " + type_id_literal(leaf_it->second) + ";");
        ctx_.line("static constexpr std::string_view TYPE_NAME = \"" + sd.name + "\";");
        ctx_.line();
    }

    if (sd.is_bitmap) {
        emit_bitmap_struct(sd, name);
        optional_field_names_.clear();
    } else {
        emit_plain_struct(sd.children, name);
    }

    current_parent_ = prev_parent;
    current_bmdl_name_ = prev_bmdl;
    outer_scope_params_ = prev_outer;

    ctx_.dedent();
    ctx_.line("};");
    ctx_.line();
}

void StructEmitter::emit_message(const model::MessageDef& md,
                                  const analyzer::SessionInfo* session) {
    std::string name = to_cpp_type_name(md.name);

    auto prev_session = current_session_;

    if (session) {
        current_session_ = session;
    }

    auto prev_parent = current_parent_;
    auto prev_bmdl = current_bmdl_name_;
    current_parent_ = name;
    current_bmdl_name_ = md.name;

    emit_child_class_defs(md.children, name);
    emit_variant_aliases(md.children);

    if (!md.doc.empty()) {
        emit_doc_comment(md.doc);
    }

    ctx_.line("class " + name + " {");
    ctx_.line("public:");
    ctx_.indent();

    // Type ID and name
    uint64_t type_id = analyzer::fnv1a_hash(md.name.c_str());
    ctx_.line("static constexpr uint64_t TYPE_ID = " + type_id_literal(type_id) + ";");
    ctx_.line("static constexpr std::string_view TYPE_NAME = \"" + md.name + "\";");

    // v2: message ID value (when frame-based session exists)
    if (!md.id.empty() && current_session_ && current_session_->is_frame_based) {
        // Resolve the id field type from the frame
        std::string id_cpp_type = "uint8_t"; // default
        if (current_session_->frame) {
            for (const auto& child : current_session_->frame->header_fields) {
                if (auto* f = std::get_if<model::Field>(&child)) {
                    if (f->name == current_session_->id_field_name) {
                        auto fti = resolve_field_type(*f, index_);
                        id_cpp_type = fti.cpp_type;
                        break;
                    }
                }
            }
        }
        ctx_.line("static constexpr " + id_cpp_type + " ID_VALUE = " + md.id + ";");
    }
    ctx_.line();

    // Collect frame header/footer fields for frame-based messages
    std::vector<FrameFieldInfo> header_frame_fields, footer_frame_fields;
    if (!md.id.empty() && current_session_ && current_session_->is_frame_based && current_session_->frame) {
        collect_frame_fields(*current_session_->frame, header_frame_fields, footer_frame_fields);
    }

    // Emit frame field accessors (header + footer together)
    if (!header_frame_fields.empty() || !footer_frame_fields.empty()) {
        emit_frame_field_accessors(header_frame_fields);
        emit_frame_field_accessors(footer_frame_fields);
        ctx_.line();
    }

    emit_plain_struct(md.children, name, header_frame_fields, footer_frame_fields);

    // Re-enter public section for convenience methods
    ctx_.dedent();
    ctx_.line("public:");
    ctx_.indent();

    // Convenience encode/decode from bytes
    ctx_.line("conduit::Result<std::vector<uint8_t>> encode_bytes() const {");
    ctx_.indent();
    ctx_.line("conduit::io::BitWriter w;");
    ctx_.line("CONDUIT_TRY(encode(w));");
    ctx_.line("return w.finish();");
    ctx_.dedent();
    ctx_.line("}");
    ctx_.line();

    ctx_.line("static conduit::Result<" + name + "> decode_bytes(std::span<const uint8_t> data, size_t max_bytes = 0) {");
    ctx_.indent();
    ctx_.line("if (max_bytes > 0 && data.size() > max_bytes) {");
    ctx_.indent();
    ctx_.line("return std::unexpected(conduit::Error(conduit::ErrorCode::MaxLengthExceeded,");
    ctx_.line("    \"decode " + current_bmdl_name_ + ": message size \" + std::to_string(data.size()) + \" exceeds limit \" + std::to_string(max_bytes)));");
    ctx_.dedent();
    ctx_.line("}");
    ctx_.line("conduit::io::BitReader r(data);");
    ctx_.line("return decode(r);");
    ctx_.dedent();
    ctx_.line("}");

    current_parent_ = prev_parent;
    current_bmdl_name_ = prev_bmdl;
    current_session_ = prev_session;

    ctx_.dedent();
    ctx_.line("};");
    ctx_.line();
}

// ============================================================================
// Frame field helpers (push frame header/footer into message classes)
// ============================================================================

void StructEmitter::collect_frame_fields(const model::FrameDef& frame,
                                          std::vector<FrameFieldInfo>& header_fields,
                                          std::vector<FrameFieldInfo>& footer_fields) {
    auto collect = [&](const std::vector<model::StructChild>& children,
                       std::vector<FrameFieldInfo>& out) {
        for (const auto& child : children) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                FrameFieldInfo ffi;
                ffi.source = f;
                ffi.fti = resolve_field_type(*f, index_);
                ffi.fi.name = f->name;
                ffi.fi.cpp_type = ffi.fti.cpp_type;
                ffi.fi.is_auto_managed = f->auto_expr.has_value();
                ffi.fi.constraint = f->constraint ? &*f->constraint : nullptr;
                ffi.fi.is_signed = ffi.fti.is_signed;
                ffi.fi.is_enum = ffi.fti.is_enum;
                ffi.fi.is_bytes = ffi.fti.is_bytes;
                out.push_back(std::move(ffi));
            }
        }
    };
    collect(frame.header_fields, header_fields);
    collect(frame.footer_fields, footer_fields);
}

void StructEmitter::emit_frame_field_accessors(const std::vector<FrameFieldInfo>& frame_fields) {
    for (const auto& ffi : frame_fields) {
        std::string accessor = to_accessor_name(ffi.fi.name);
        std::string member = to_member_name(ffi.fi.name);
        std::string qual_type = ffi.fi.cpp_type;
        bool by_value = (qual_type == "bool") || ffi.fi.is_enum;
        // Getter
        if (by_value) {
            ctx_.line(qual_type + " " + accessor + "() const { return " + member + "; }");
        } else {
            ctx_.line("const " + qual_type + "& " + accessor + "() const { return " + member + "; }");
        }
        // Deprecation for auto-managed fields
        if (ffi.fi.is_auto_managed) {
            ctx_.line("[[deprecated(\"auto-managed: value is set automatically during frame encoding\")]]");
        }
        // Setter with constraint validation (matching emit_plain_accessors pattern)
        const auto* constraint = ffi.fi.constraint;
        bool has_constraint = constraint
            && constraint->validate != model::ValidateTiming::Deferred
            && (constraint->equals || constraint->min || constraint->max);
        if (has_constraint) {
            if (by_value) {
                ctx_.line("[[nodiscard]] conduit::VoidResult set_" + accessor + "(" + qual_type + " v) {");
            } else {
                ctx_.line("[[nodiscard]] conduit::VoidResult set_" + accessor + "(const " + qual_type + "& v) {");
            }
            ctx_.indent();
            emit_setter_constraint_checks(ffi.fi.name, qual_type, constraint,
                ffi.fi.is_signed, std::nullopt, ffi.fi.is_bytes);
            ctx_.line(member + " = v;");
            ctx_.line("return {};");
            ctx_.dedent();
            ctx_.line("}");
        } else {
            if (by_value) {
                ctx_.line("void set_" + accessor + "(" + qual_type + " v) { " + member + " = v; }");
            } else {
                ctx_.line("void set_" + accessor + "(const " + qual_type + "& v) { " + member + " = v; }");
            }
        }
    }
}

// ============================================================================
// Plain struct
// ============================================================================

void StructEmitter::emit_plain_struct(const std::vector<model::StructChild>& children,
                                       const std::string& class_name,
                                       const std::vector<FrameFieldInfo>& header_frame_fields,
                                       const std::vector<FrameFieldInfo>& footer_frame_fields) {
    std::vector<FieldInfo> fields;
    bool has_fx = false;

    collect_fields(children, fields, has_fx, false, 0);

    for (const auto& fi : fields) {
        if (fi.is_optional) {
            emit_optional_accessors(fi);
        } else {
            emit_plain_accessors(fi);
        }
    }

    auto ws = sizes_.get(class_name);
    if (ws) {
        ctx_.line("static constexpr size_t WIRE_SIZE = " + std::to_string(*ws) + ";");
        ctx_.line();
    }

    ctx_.line("bool operator==(const " + class_name + "&) const = default;");
    ctx_.line();

    populate_optional_field_names(children);
    emit_encode(children);
    emit_decode(children, class_name);
    emit_to_string(children, fields, class_name, header_frame_fields, footer_frame_fields);
    emit_deferred_validate(children);

    // Private section
    ctx_.dedent();
    ctx_.line("private:");
    ctx_.indent();

    // Friend declaration for the frame class (so it can write frame fields directly)
    if (!header_frame_fields.empty() || !footer_frame_fields.empty()) {
        if (current_session_ && current_session_->frame) {
            ctx_.line("friend class " + to_cpp_type_name(current_session_->frame->name) + ";");
            ctx_.line();
        }
    }

    // Frame field members (header + footer together)
    for (const auto& ffi : header_frame_fields) {
        ctx_.line(ffi.fi.cpp_type + " " + to_member_name(ffi.fi.name) + "{};");
    }
    for (const auto& ffi : footer_frame_fields) {
        ctx_.line(ffi.fi.cpp_type + " " + to_member_name(ffi.fi.name) + "{};");
    }

    // Regular members
    for (const auto& fi : fields) {
        std::string decl_type = qualify_type_if_shadowed(fi.name, fi.cpp_type);
        if (fi.is_optional) {
            ctx_.line("std::optional<" + decl_type + "> " + to_member_name(fi.name) + ";");
        } else if (fi.default_value) {
            ctx_.line(decl_type + " " + to_member_name(fi.name) + "{" + *fi.default_value + "};");
        } else {
            ctx_.line(decl_type + " " + to_member_name(fi.name) + "{};");
        }
    }
}

void StructEmitter::collect_fields(const std::vector<model::StructChild>& children,
                                    std::vector<FieldInfo>& fields, bool& has_fx,
                                    bool in_fx, int fx_depth) {
    for (const auto& child : children) {
        FieldInfo fi;
        std::visit([&](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, model::Field>) {
                fi.name = c.name;
                auto fti = resolve_field_type(c, index_);
                fi.cpp_type = fti.cpp_type;
                fi.is_signed = fti.is_signed;
                // Override type for inline enum fields
                if (!c.enum_values.empty() && c.type_ref.empty()) {
                    std::string enum_name = to_pascal_case(current_parent_) + "_" + to_pascal_case(c.name);
                    fi.cpp_type = enum_name;
                    fti.cpp_type = enum_name;
                    fti.is_enum = true;
                    // Initialize to first enum value
                    if (!c.enum_values.empty()) {
                        fi.default_value = enum_name + "::" + to_enum_value_name(c.enum_values[0].name);
                    }
                }
                fi.is_enum = fti.is_enum;
                fi.is_bytes = fti.is_bytes;
                fi.is_optional = c.bit.has_value() || c.present_when != nullptr || in_fx;
                if (!fi.default_value) fi.default_value = c.default_value;
                // Warn when both default and constraint equals are specified
                if (fi.default_value && c.constraint && c.constraint->equals) {
                    Logger::warn(c.loc.to_string() + ": field '" + c.name +
                        "': both 'default' and 'constraint equals' specified; "
                        "constraint value (" + *c.constraint->equals + ") wins, "
                        "default value (" + *fi.default_value + ") is ignored");
                    fi.default_value = *c.constraint->equals;
                }
                // constraint equals="X" implies default="X"
                if (!fi.default_value && c.constraint && c.constraint->equals) {
                    fi.default_value = *c.constraint->equals;
                }
                // Bool fields: normalize "0"/"1" to "false"/"true"
                if (fi.default_value && fti.is_bool) {
                    if (*fi.default_value == "0") fi.default_value = "false";
                    else if (*fi.default_value == "1") fi.default_value = "true";
                }
                // Enum fields: qualify default value with type prefix
                if (fi.default_value && fti.is_enum && !c.type_ref.empty()) {
                    fi.default_value = fti.cpp_type + "::" +
                        to_enum_value_name(*fi.default_value);
                } else if (!fi.default_value && fti.is_enum && !c.type_ref.empty()) {
                    // No default — initialize to first enum value
                    auto resolved = index_.find(c.type_ref);
                    if (resolved) {
                        std::visit([&](const auto* def) {
                            using DT = std::decay_t<decltype(*def)>;
                            if constexpr (std::is_same_v<DT, model::TypeDef>) {
                                if (!def->enum_values.empty()) {
                                    fi.default_value = fti.cpp_type + "::" +
                                        to_enum_value_name(def->enum_values[0].name);
                                }
                            }
                        }, *resolved);
                    }
                }
                fi.constraint = c.constraint ? &*c.constraint : nullptr;
                // Thread scale info for raw accessor generation
                fi.has_field_scale = fti.has_field_scale;
                fi.field_scale = fti.field_scale;
                fi.field_offset = fti.field_offset;
                fi.raw_bits = fti.raw_bits;
                fi.raw_signed = fti.raw_signed;
                // Warn: equals constraint on float-generated fields is unreliable
                bool is_float_type = fti.is_float || fti.has_field_scale;
                if (fi.constraint && fi.constraint->equals && is_float_type) {
                    Logger::warn(c.loc.to_string() + ": field '" + c.name +
                        "': 'equals' constraint on floating-point field is unreliable "
                        "due to precision loss; consider using min/max with tolerance instead");
                }
                // Warn and suppress numeric constraints for byte arrays > 8 bytes
                if (fi.is_bytes && c.bytes_attr && *c.bytes_attr > 8) {
                    if (c.scale || c.offset) {
                        Logger::warn(c.loc.to_string() + ": field '" + c.name +
                            "': scale/offset ignored for byte array field (bytes=" +
                            std::to_string(*c.bytes_attr) + " exceeds native integer size)");
                    }
                    if (fi.constraint &&
                        (fi.constraint->min || fi.constraint->max || fi.constraint->equals)) {
                        Logger::warn(c.loc.to_string() + ": field '" + c.name +
                            "': numeric constraints ignored for byte array field (bytes=" +
                            std::to_string(*c.bytes_attr) + " exceeds native integer size)");
                        fi.constraint = nullptr;
                    }
                }
                fi.max_length = c.max_length;
                if (c.is_inline) {
                    auto resolved = index_.find(c.type_ref);
                    if (resolved) {
                        std::visit([&](const auto* def) {
                            using DT = std::decay_t<decltype(*def)>;
                            if constexpr (std::is_same_v<DT, model::StructDef> ||
                                          std::is_same_v<DT, model::MessageDef>) {
                                collect_fields(def->children, fields, has_fx, in_fx, fx_depth);
                            }
                        }, *resolved);
                    }
                    return;
                }
                fields.push_back(fi);
            } else if constexpr (std::is_same_v<T, model::StructDef>) {
                if (!c.name.empty()) {
                    fi.name = c.name;
                    fi.cpp_type = get_child_class_name(c.name);
                    fi.is_optional = c.bit.has_value() || c.present_when != nullptr || in_fx;
                    fields.push_back(fi);
                }
            } else if constexpr (std::is_same_v<T, model::ArrayDef>) {
                fi.name = c.name;
                std::string elem_type;
                if (!c.type_ref.empty()) {
                    bool elem_primitive = false;
                    auto resolved = index_.find(c.type_ref);
                    if (resolved) {
                        std::visit([&](const auto* def) {
                            using DT = std::decay_t<decltype(*def)>;
                            if constexpr (std::is_same_v<DT, model::TypeDef>) {
                                bool has_wrapper = !def->enum_values.empty() || !def->flags.empty() ||
                                                  def->scale.has_value() || def->offset.has_value() ||
                                                  (def->base == model::PrimitiveBase::String) ||
                                                  def->constraint.has_value();
                                if (!has_wrapper && def->bits > 0 &&
                                    def->base != model::PrimitiveBase::Bool &&
                                    def->base != model::PrimitiveBase::Float) {
                                    elem_primitive = true;
                                    elem_type = storage_type_for_bits(def->bits, def->base == model::PrimitiveBase::Int);
                                }
                            }
                        }, *resolved);
                    }
                    if (!elem_primitive) {
                        elem_type = to_cpp_type_name(c.type_ref);
                    }
                } else {
                    elem_type = get_child_class_name(c.name + "Element");
                }
                fi.cpp_type = "std::vector<" + elem_type + ">";
                fi.is_optional = c.bit.has_value() || c.present_when != nullptr || in_fx;
                fields.push_back(fi);
            } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                fi.name = c.name;
                fi.cpp_type = get_variant_alias_name(c.name);
                fi.is_variant = true;
                fi.is_optional = c.bit.has_value() || c.present_when != nullptr || in_fx;
                fields.push_back(fi);
            } else if constexpr (std::is_same_v<T, model::FxBlock>) {
                has_fx = true;
                collect_fields(c.children, fields, has_fx, true, fx_depth + 1);
            }
        }, child);
    }
}

// ============================================================================
// Accessors
// ============================================================================

void StructEmitter::emit_setter_constraint_checks(const std::string& name, const std::string& qual_type,
                                                    const model::Constraint* constraint, bool is_signed,
                                                    std::optional<int> max_length,
                                                    bool is_bytes) {
    std::string qualified = current_bmdl_name_.empty() ? name : (current_bmdl_name_ + "." + name);

    if (constraint && is_bytes) {
        // Byte-array fields: convert to numeric value before checking constraints
        bool need_check = constraint->equals || constraint->max ||
            (constraint->min && (*constraint->min != "0" || is_signed));
        if (need_check) {
            ctx_.line("{");
            ctx_.indent();
            ctx_.line("uint64_t _raw = 0;");
            ctx_.line("for (size_t i = 0; i < v.size(); ++i) _raw = (_raw << 8) | v[i];");
            if (constraint->equals) {
                ctx_.line("if (_raw != static_cast<uint64_t>(" + *constraint->equals + "))");
                ctx_.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
                ctx_.line("        \"set " + qualified + ": constraint violation: expected " + *constraint->equals + "\"));");
            }
            if (constraint->max) {
                ctx_.line("if (_raw > " + *constraint->max + ")");
                ctx_.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
                ctx_.line("        \"set " + qualified + ": value \" + std::to_string(_raw) + \" exceeds max " + *constraint->max + "\"));");
            }
            if (constraint->min && (*constraint->min != "0" || is_signed)) {
                ctx_.line("if (_raw < " + *constraint->min + ")");
                ctx_.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
                ctx_.line("        \"set " + qualified + ": value \" + std::to_string(_raw) + \" below min " + *constraint->min + "\"));");
            }
            ctx_.dedent();
            ctx_.line("}");
        }
    } else if (constraint) {
        if (constraint->equals) {
            ctx_.line("if (v != static_cast<" + qual_type + ">(" + *constraint->equals + "))");
            ctx_.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
            ctx_.line("        \"set " + qualified + ": constraint violation: expected " + *constraint->equals + "\"));");
        }
        if (constraint->max) {
            ctx_.line("if (v > " + *constraint->max + ")");
            ctx_.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
            ctx_.line("        \"set " + qualified + ": value \" + std::to_string(static_cast<int64_t>(v)) + \" exceeds max " + *constraint->max + "\"));");
        }
        if (constraint->min && (*constraint->min != "0" || is_signed)) {
            ctx_.line("if (v < " + *constraint->min + ")");
            ctx_.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::EncodeConstraintViolation,");
            ctx_.line("        \"set " + qualified + ": value \" + std::to_string(static_cast<int64_t>(v)) + \" below min " + *constraint->min + "\"));");
        }
    }
    if (max_length) {
        ctx_.line("if (v.size() > " + std::to_string(*max_length) + ")");
        ctx_.line("    return std::unexpected(conduit::Error(conduit::ErrorCode::StringTooLong,");
        ctx_.line("        \"set " + qualified + ": length \" + std::to_string(v.size()) + \" exceeds max length " + std::to_string(*max_length) + "\"));");
    }
}

void StructEmitter::emit_plain_accessors(const FieldInfo& fi) {
    const auto& name = fi.name;
    const auto& cpp_type = fi.cpp_type;
    const auto* constraint = fi.constraint;
    bool is_signed = fi.is_signed;
    auto max_length = fi.max_length;
    bool is_enum = fi.is_enum;
    bool is_bytes = fi.is_bytes;

    std::string acc = to_accessor_name(name);
    std::string member = to_member_name(name);
    std::string qual_type = (acc == cpp_type && !ns_.empty())
                            ? "::" + ns_ + "::" + cpp_type : cpp_type;
    bool by_value = (cpp_type == "bool") || is_enum;
    if (by_value) {
        ctx_.line(qual_type + " " + acc + "() const { return " + member + "; }");
    } else {
        ctx_.line("const " + qual_type + "& " + acc + "() const { return " + member + "; }");
    }
    ctx_.line(qual_type + "& mutable_" + acc + "() { return " + member + "; }");
    // Only validate immediate constraints in setters; deferred constraints are checked at validate()
    bool has_constraint = constraint
        && constraint->validate != model::ValidateTiming::Deferred
        && (constraint->equals || constraint->min || constraint->max);
    if (has_constraint || max_length) {
        if (by_value) {
            ctx_.line("[[nodiscard]] conduit::VoidResult set_" + acc + "(" + qual_type + " v) {");
        } else {
            ctx_.line("[[nodiscard]] conduit::VoidResult set_" + acc + "(const " + qual_type + "& v) {");
        }
        ctx_.indent();
        emit_setter_constraint_checks(name, qual_type,
            has_constraint ? constraint : nullptr, is_signed, max_length, is_bytes);
        ctx_.line(member + " = v;");
        ctx_.line("return {};");
        ctx_.dedent();
        ctx_.line("}");
    } else {
        if (by_value) {
            ctx_.line("void set_" + acc + "(" + qual_type + " v) { " + member + " = v; }");
        } else {
            ctx_.line("void set_" + acc + "(const " + qual_type + "& v) { " + member + " = v; }");
        }
    }
    // Raw accessors for scaled fields (expose underlying integer)
    if (fi.has_field_scale) {
        std::string raw_type = storage_type_for_bits(fi.raw_bits, fi.raw_signed);
        std::string scale_str = double_literal(fi.field_scale);
        std::string offset_str = double_literal(fi.field_offset);
        if (fi.field_offset != 0.0) {
            ctx_.line(raw_type + " " + acc + "_raw() const { return static_cast<" + raw_type +
                     ">((" + member + " - " + offset_str + ") / " + scale_str + "); }");
        } else {
            ctx_.line(raw_type + " " + acc + "_raw() const { return static_cast<" + raw_type +
                     ">(" + member + " / " + scale_str + "); }");
        }
        ctx_.line("void set_" + acc + "_raw(" + raw_type + " v) { " + member +
                 " = static_cast<double>(v) * " + scale_str + " + " + offset_str + "; }");
    }
    ctx_.line();
}

void StructEmitter::emit_optional_accessors(const FieldInfo& fi) {
    const auto& name = fi.name;
    const auto& cpp_type = fi.cpp_type;
    const auto& default_value = fi.default_value;
    const auto* constraint = fi.constraint;
    bool is_signed = fi.is_signed;
    auto max_length = fi.max_length;
    bool is_enum = fi.is_enum;
    bool is_bytes = fi.is_bytes;

    std::string acc = to_accessor_name(name);
    std::string member = to_member_name(name);
    std::string qual_type = (acc == cpp_type && !ns_.empty())
                            ? "::" + ns_ + "::" + cpp_type : cpp_type;
    bool by_value = (cpp_type == "bool") || is_enum;
    ctx_.line("bool has_" + acc + "() const { return " + member + ".has_value(); }");
    if (default_value) {
        ctx_.line(qual_type + " " + acc + "() const { return " + member +
                 ".value_or(" + qual_type + "{" + *default_value + "}); }");
    } else if (by_value) {
        ctx_.line(qual_type + " " + acc + "() const { return " + member + ".value(); }");
    } else {
        ctx_.line("const " + qual_type + "& " + acc + "() const { return " + member + ".value(); }");
    }
    ctx_.line(qual_type + "& mutable_" + acc + "() { if (!" + member + ") " + member + ".emplace(); return *" + member + "; }");
    bool has_constraint = constraint
        && constraint->validate != model::ValidateTiming::Deferred
        && (constraint->equals || constraint->min || constraint->max);
    if (has_constraint || max_length) {
        if (by_value) {
            ctx_.line("[[nodiscard]] conduit::VoidResult set_" + acc + "(" + qual_type + " v) {");
        } else {
            ctx_.line("[[nodiscard]] conduit::VoidResult set_" + acc + "(const " + qual_type + "& v) {");
        }
        ctx_.indent();
        emit_setter_constraint_checks(name, qual_type,
            has_constraint ? constraint : nullptr, is_signed, max_length, is_bytes);
        ctx_.line(member + " = v;");
        ctx_.line("return {};");
        ctx_.dedent();
        ctx_.line("}");
    } else {
        if (by_value) {
            ctx_.line("void set_" + acc + "(" + qual_type + " v) { " + member + " = v; }");
        } else {
            ctx_.line("void set_" + acc + "(const " + qual_type + "& v) { " + member + " = v; }");
        }
    }
    // Raw accessors for scaled fields (expose underlying integer)
    if (fi.has_field_scale) {
        std::string raw_type = storage_type_for_bits(fi.raw_bits, fi.raw_signed);
        std::string scale_str = double_literal(fi.field_scale);
        std::string offset_str = double_literal(fi.field_offset);
        if (fi.field_offset != 0.0) {
            ctx_.line(raw_type + " " + acc + "_raw() const { return " + member +
                     ".has_value() ? static_cast<" + raw_type +
                     ">((*" + member + " - " + offset_str + ") / " + scale_str + ") : " +
                     raw_type + "{0}; }");
        } else {
            ctx_.line(raw_type + " " + acc + "_raw() const { return " + member +
                     ".has_value() ? static_cast<" + raw_type +
                     ">(*" + member + " / " + scale_str + ") : " +
                     raw_type + "{0}; }");
        }
        ctx_.line("void set_" + acc + "_raw(" + raw_type + " v) { " + member +
                 " = static_cast<double>(v) * " + scale_str + " + " + offset_str + "; }");
    }
    ctx_.line("void clear_" + acc + "() { " + member + ".reset(); }");
    ctx_.line();
}

// ============================================================================
// Bitmap struct
// ============================================================================

void StructEmitter::emit_bitmap_struct(const model::StructDef& sd, const std::string& class_name) {
    // Collect bitmap-controlled fields
    std::vector<BitmapField> bfields;

    for (const auto& child : sd.children) {
        std::visit([&](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, model::Field>) {
                if (c.bit) {
                    BitmapField bf;
                    bf.name = c.name;
                    auto fti = resolve_field_type(c, index_);
                    // Override for inline enums
                    if (!c.enum_values.empty() && c.type_ref.empty()) {
                        std::string enum_name = to_pascal_case(current_parent_) + "_" + to_pascal_case(c.name);
                        fti.cpp_type = enum_name;
                        fti.is_enum = true;
                    }
                    bf.cpp_type = fti.cpp_type;
                    bf.bit = *c.bit;
                    bf.is_struct = fti.is_struct;
                    bf.is_enum = fti.is_enum;
                    bf.is_string = fti.is_string;
                    bf.is_bytes = fti.is_bytes;
                    bf.type_bits = fti.bits;
                    bf.is_signed = fti.is_signed;
                    bf.is_float = fti.is_float;
                    bf.endian = c.endian;
                    bf.wire_encoding = fti.wire_encoding;
                    bf.has_field_scale = fti.has_field_scale;
                    bf.field_scale = fti.field_scale;
                    bf.field_offset = fti.field_offset;
                    bf.raw_bits = fti.raw_bits;
                    bf.raw_signed = fti.raw_signed;
                    bf.raw_endian = fti.raw_endian;
                    bf.length = c.length;
                    bf.bytes_attr = c.bytes_attr;
                    bf.source_field = &c;
                    bfields.push_back(bf);
                }
            } else if constexpr (std::is_same_v<T, model::StructDef>) {
                if (c.bit) {
                    BitmapField bf;
                    bf.name = c.name;
                    bf.cpp_type = get_child_class_name(c.name);
                    bf.bit = *c.bit;
                    bf.is_struct = true;
                    bfields.push_back(bf);
                }
            } else if constexpr (std::is_same_v<T, model::ChoiceDef>) {
                if (c.bit) {
                    BitmapField bf;
                    bf.name = c.name;
                    bf.cpp_type = get_variant_alias_name(c.name);
                    bf.bit = *c.bit;
                    bf.is_struct = true;
                    bf.is_choice = true;
                    bf.choice_def = &c;
                    bfields.push_back(bf);
                }
            }
        }, child);
    }

    int max_bit = 0;
    for (const auto& bf : bfields) {
        max_bit = std::max(max_bit, bf.bit);
    }
    bool has_ext = sd.bitmap_ext.has_value();

    // Accessors
    for (const auto& bf : bfields) {
        std::string acc = to_accessor_name(bf.name);
        std::string member = to_member_name(bf.name);
        std::string qual_type = (acc == bf.cpp_type && !ns_.empty())
                                ? "::" + ns_ + "::" + bf.cpp_type : bf.cpp_type;

        bool bm_by_value = bf.is_enum || (bf.cpp_type == "bool");
        // Compute effective default: explicit default or constraint equals
        std::optional<std::string> bm_default;
        if (bf.source_field) {
            if (bf.source_field->default_value)
                bm_default = bf.source_field->default_value;
            else if (bf.source_field->constraint && bf.source_field->constraint->equals)
                bm_default = bf.source_field->constraint->equals;
        }
        ctx_.line("bool has_" + acc + "() const { return " + member + ".has_value(); }");
        if (bm_default) {
            ctx_.line(qual_type + " " + acc + "() const { return " + member +
                     ".value_or(" + qual_type + "{" + *bm_default + "}); }");
        } else if (bm_by_value) {
            ctx_.line(qual_type + " " + acc + "() const { return " + member + ".value(); }");
        } else {
            ctx_.line("const " + qual_type + "& " + acc + "() const { return " + member + ".value(); }");
        }
        ctx_.line(qual_type + "& mutable_" + acc + "() { if (!" + member + ") " + member + ".emplace(); return *" + member + "; }");
        const model::Constraint* bm_constraint = (bf.source_field && bf.source_field->constraint)
                                                  ? &*bf.source_field->constraint : nullptr;
        // Warn: equals constraint on float-generated bitmap fields is unreliable
        bool bm_is_float_type = bf.is_float || bf.has_field_scale;
        if (bm_constraint && bm_constraint->equals && bm_is_float_type && bf.source_field) {
            Logger::warn(bf.source_field->loc.to_string() + ": field '" + bf.name +
                "': 'equals' constraint on floating-point field is unreliable "
                "due to precision loss; consider using min/max with tolerance instead");
        }
        // Suppress numeric constraints for byte arrays > 8 bytes
        if (bf.is_bytes && bm_constraint &&
            (bm_constraint->min || bm_constraint->max || bm_constraint->equals)) {
            bm_constraint = nullptr;
        }
        bool bm_has_constraint = bm_constraint && (bm_constraint->equals || bm_constraint->min || bm_constraint->max);
        std::optional<int> bm_max_length = bf.source_field ? bf.source_field->max_length : std::nullopt;
        if (bm_has_constraint || bm_max_length) {
            if (bm_by_value) {
                ctx_.line("[[nodiscard]] conduit::VoidResult set_" + acc + "(" + qual_type + " v) {");
            } else {
                ctx_.line("[[nodiscard]] conduit::VoidResult set_" + acc + "(const " + qual_type + "& v) {");
            }
            ctx_.indent();
            emit_setter_constraint_checks(bf.name, qual_type,
                bm_has_constraint ? bm_constraint : nullptr, bf.is_signed, bm_max_length, bf.is_bytes);
            ctx_.line(member + " = v;");
            ctx_.line("return {};");
            ctx_.dedent();
            ctx_.line("}");
        } else {
            if (bm_by_value) {
                ctx_.line("void set_" + acc + "(" + qual_type + " v) { " + member + " = v; }");
            } else {
                ctx_.line("void set_" + acc + "(const " + qual_type + "& v) { " + member + " = v; }");
            }
        }
        // Raw accessors for scaled bitmap fields (expose underlying integer)
        if (bf.has_field_scale) {
            std::string raw_type = storage_type_for_bits(bf.raw_bits, bf.raw_signed);
            std::string scale_str = double_literal(bf.field_scale);
            std::string offset_str = double_literal(bf.field_offset);
            if (bf.field_offset != 0.0) {
                ctx_.line(raw_type + " " + acc + "_raw() const { return " + member +
                         ".has_value() ? static_cast<" + raw_type +
                         ">((*" + member + " - " + offset_str + ") / " + scale_str + ") : " +
                         raw_type + "{0}; }");
            } else {
                ctx_.line(raw_type + " " + acc + "_raw() const { return " + member +
                         ".has_value() ? static_cast<" + raw_type +
                         ">(*" + member + " / " + scale_str + ") : " +
                         raw_type + "{0}; }");
            }
            ctx_.line("void set_" + acc + "_raw(" + raw_type + " v) { " + member +
                     " = static_cast<double>(v) * " + scale_str + " + " + offset_str + "; }");
        }
        ctx_.line("void clear_" + acc + "() { " + member + ".reset(); }");
        ctx_.line();
    }

    ctx_.line("bool operator==(const " + class_name + "&) const = default;");
    ctx_.line();

    // Determine FSPEC endianness from first bitmap field
    bool fspec_le = !bfields.empty() && bfields[0].endian == model::Endian::Little;

    // Encode
    ctx_.line("conduit::VoidResult encode(conduit::io::BitWriter& w) const {");
    ctx_.indent();

    if (has_ext) {
        int max_octet = max_bit / BITS_PER_BYTE;
        int num_octets = max_octet + 1;

        ctx_.line("int last_octet = 0;");
        for (const auto& bf : bfields) {
            ctx_.line("if (" + to_member_name(bf.name) + ".has_value() && " +
                      std::to_string(bf.bit / BITS_PER_BYTE) + " > last_octet) last_octet = " +
                      std::to_string(bf.bit / BITS_PER_BYTE) + ";");
        }

        ctx_.line("std::array<uint8_t, " + std::to_string(num_octets) + "> fspec{};");
        for (const auto& bf : bfields) {
            int byte_idx = bf.bit / BITS_PER_BYTE;
            int bit_in_byte = bf.bit % BITS_PER_BYTE;
            ctx_.line("if (" + to_member_name(bf.name) + ".has_value()) fspec[" +
                      std::to_string(byte_idx) + "] |= (1 << " +
                      std::to_string(bit_in_byte) + ");");
        }
        ctx_.line("for (size_t i = 0; i < std::min(static_cast<size_t>(last_octet), fspec.size()); i++) fspec[i] |= (1 << " +
                  std::to_string(*sd.bitmap_ext) + ");");
        if (fspec_le) {
            ctx_.line("{ auto n = std::min(static_cast<size_t>(last_octet) + 1, fspec.size()); std::reverse(fspec.begin(), fspec.begin() + static_cast<ptrdiff_t>(n)); }");
        }
        ctx_.line("w.write_bytes(std::span<const uint8_t>(fspec.data(), std::min(static_cast<size_t>(last_octet) + 1, fspec.size())));");

        emit_bitmap_encode_fields(bfields, 0, max_octet);
    } else {
        int num_octets = (max_bit / BITS_PER_BYTE) + 1;
        ctx_.line("std::array<uint8_t, " + std::to_string(num_octets) + "> fspec{};");
        for (const auto& bf : bfields) {
            int byte_idx = bf.bit / BITS_PER_BYTE;
            int bit_in_byte = bf.bit % BITS_PER_BYTE;
            ctx_.line("if (" + to_member_name(bf.name) + ".has_value()) fspec[" +
                      std::to_string(byte_idx) + "] |= (1 << " +
                      std::to_string(bit_in_byte) + ");");
        }
        if (fspec_le) {
            ctx_.line("std::reverse(fspec.begin(), fspec.begin() + " + std::to_string(num_octets) + ");");
        }
        ctx_.line("w.write_bytes(std::span<const uint8_t>(fspec.data(), " +
                  std::to_string(num_octets) + "));");
        emit_bitmap_encode_fields(bfields, 0, (max_bit / BITS_PER_BYTE));
    }

    ctx_.line("if (w.has_error()) return std::unexpected(w.error());");
    ctx_.line("return {};");
    ctx_.dedent();
    ctx_.line("}");
    ctx_.line();

    // Decode
    ctx_.line("static conduit::Result<" + class_name + "> decode(conduit::io::BitReader& r) {");
    ctx_.indent();
    ctx_.line(class_name + " result;");
    ctx_.line();
    ctx_.line("// Read FSPEC bitmap");
    {
        int num_octets = (max_bit / BITS_PER_BYTE) + 1;
        ctx_.line("std::array<uint8_t, " + std::to_string(num_octets) + "> fspec{};");
        ctx_.line("size_t fspec_len = 0;");
        if (has_ext) {
            ctx_.line("while (true) {");
            ctx_.indent();
            ctx_.line("auto byte = r.read_u8();");
            ctx_.line("if (!byte) return std::unexpected(byte.error());");
            ctx_.line("if (fspec_len < " + std::to_string(num_octets) + ") fspec[fspec_len] = *byte;");
            ctx_.line("fspec_len++;");
            ctx_.line("if (!(*byte & (1 << " + std::to_string(*sd.bitmap_ext) + "))) break;");
            ctx_.dedent();
            ctx_.line("}");
        } else {
            // Fixed-size FSPEC: read exactly num_octets bytes
            ctx_.line("for (size_t i = 0; i < " + std::to_string(num_octets) + "; i++) {");
            ctx_.indent();
            ctx_.line("auto byte = r.read_u8();");
            ctx_.line("if (!byte) return std::unexpected(byte.error());");
            ctx_.line("fspec[i] = *byte;");
            ctx_.dedent();
            ctx_.line("}");
            ctx_.line("fspec_len = " + std::to_string(num_octets) + ";");
        }
        if (fspec_le) {
            ctx_.line("std::reverse(fspec.begin(), fspec.begin() + static_cast<ptrdiff_t>(fspec_len));");
        }
    }
    ctx_.line();

    auto sorted_fields = bfields;
    std::sort(sorted_fields.begin(), sorted_fields.end(), [](const auto& a, const auto& b) {
        int a_oct = a.bit / BITS_PER_BYTE;
        int b_oct = b.bit / BITS_PER_BYTE;
        if (a_oct != b_oct) return a_oct < b_oct;
        return a.bit > b.bit;
    });

    optional_field_names_.clear();
    for (const auto& bf : bfields) {
        optional_field_names_.insert(to_member_name(bf.name));
    }

    for (const auto& bf : sorted_fields) {
        int byte_idx = bf.bit / BITS_PER_BYTE;
        int bit_in_byte = bf.bit % BITS_PER_BYTE;
        std::string member = "result." + to_member_name(bf.name);

        ctx_.line("if (fspec_len > " + std::to_string(byte_idx) +
                 " && (fspec[" + std::to_string(byte_idx) +
                 "] & (1 << " + std::to_string(bit_in_byte) + "))) {");
        ctx_.indent();

        std::string qual_type = qualify_type_if_shadowed(bf.name, bf.cpp_type);
        if (bf.is_choice && bf.choice_def) {
            emit_decode_choice(*bf.choice_def, "result");
        } else if (bf.is_enum) {
            ctx_.line("{");
            ctx_.indent();
            ctx_.line("auto val = decode_" + bf.cpp_type + "(r);");
            ctx_.line("if (!val) return std::unexpected(val.error());");
            ctx_.line(member + " = std::move(*val);");
            ctx_.dedent();
            ctx_.line("}");
        } else if (bf.is_string) {
            ctx_.line("{");
            ctx_.indent();
            if (bf.length) {
                ctx_.line("auto val = r.read_string(" + std::to_string(*bf.length) + ");");
            } else {
                ctx_.line("auto val = r.read_string(r.remaining_bytes());");
            }
            ctx_.line("if (!val) return std::unexpected(val.error());");
            if (bf.source_field && field_needs_encoding(*bf.source_field)) {
                ctx_.line(member + " = conduit::string::to_ascii(*val, " + field_encoding_enum(*bf.source_field) + ");");
            } else {
                ctx_.line(member + " = std::move(*val);");
            }
            if (bf.source_field) {
                emit_field_trim(ctx_, "(*" + member + ")", *bf.source_field, index_);
            }
            ctx_.dedent();
            ctx_.line("}");
        } else if (bf.is_bytes) {
            ctx_.line("{");
            ctx_.indent();
            if (bf.length) {
                ctx_.line("auto val = r.read_bytes(" + std::to_string(*bf.length) + ");");
                ctx_.line("if (!val) return std::unexpected(val.error());");
                ctx_.line(member + ".emplace();");
                ctx_.line("std::copy(val->begin(), val->end(), " + member + "->begin());");
            } else if (bf.bytes_attr) {
                ctx_.line("auto val = r.read_bytes(" + std::to_string(*bf.bytes_attr) + ");");
                ctx_.line("if (!val) return std::unexpected(val.error());");
                ctx_.line(member + ".emplace();");
                ctx_.line("std::copy(val->begin(), val->end(), " + member + "->begin());");
            } else {
                ctx_.line("auto val = r.read_bytes(r.remaining_bytes());");
                ctx_.line("if (!val) return std::unexpected(val.error());");
                ctx_.line(member + " = " + qual_type + "(val->begin(), val->end());");
            }
            ctx_.dedent();
            ctx_.line("}");
        } else if (bf.is_struct) {
            // Check for outer-scope params to pass to child struct decode
            auto osp_it = struct_decode_params_.find(bf.name);
            if (osp_it != struct_decode_params_.end()) {
                // Verify required outer-scope fields are present (bitmap fields are optional)
                for (const auto& p : osp_it->second) {
                    std::string dep_member = "result." + to_member_name(p.bmdl_name);
                    ctx_.line("if (!" + dep_member + ") return std::unexpected(conduit::Error(conduit::ErrorCode::InvalidArgument,");
                    ctx_.line("    \"" + bf.name + " requires " + p.bmdl_name + "\"));");
                }
                std::string decode_call = qual_type + "::decode(r";
                for (const auto& p : osp_it->second) {
                    decode_call += ", (*result." + to_member_name(p.bmdl_name) + ")";
                }
                decode_call += ")";
                ctx_.line("auto val = " + decode_call + ";");
            } else {
                ctx_.line("auto val = " + qual_type + "::decode(r);");
            }
            ctx_.line("if (!val) return std::unexpected(val.error());");
            ctx_.line(member + " = std::move(*val);");
        } else if (bf.has_field_scale) {
            // Scaled bitmap field: read raw bits, apply scale/offset
            FieldTypeInfo raw_fti;
            raw_fti.bits = bf.raw_bits;
            raw_fti.is_signed = bf.raw_signed;
            raw_fti.wire_encoding = bf.wire_encoding;
            std::string read = emit_read_expr(raw_fti, bf.raw_endian, "r", false);
            std::string scale_str = double_literal(bf.field_scale);
            std::string offset_str = double_literal(bf.field_offset);
            ctx_.line("auto raw_val = " + read + ";");
            ctx_.line("if (!raw_val) return std::unexpected(raw_val.error());");
            ctx_.line(member + " = static_cast<double>(*raw_val) * " + scale_str + " + " + offset_str + ";");
        } else {
            FieldTypeInfo bfti;
            bfti.bits = bf.type_bits;
            bfti.is_signed = bf.is_signed;
            bfti.is_float = bf.is_float;
            bfti.cpp_type = bf.cpp_type;
            bfti.wire_encoding = bf.wire_encoding;
            std::string read = emit_read_expr(bfti, bf.endian, "r", false);
            ctx_.line("auto val = " + read + ";");
            ctx_.line("if (!val) return std::unexpected(val.error());");
            ctx_.line(member + " = static_cast<" + qual_type + ">(*val);");
        }

        ctx_.dedent();
        ctx_.line("}");
    }

    ctx_.line();
    ctx_.line("return result;");
    ctx_.dedent();
    ctx_.line("}");

    emit_bitmap_to_string(bfields, class_name);

    // Private section
    ctx_.dedent();
    ctx_.line("private:");
    ctx_.indent();
    for (const auto& bf : bfields) {
        std::string qual_type = qualify_type_if_shadowed(bf.name, bf.cpp_type);
        ctx_.line("std::optional<" + qual_type + "> " + to_member_name(bf.name) + ";");
    }
}

void StructEmitter::emit_bitmap_encode_fields(const std::vector<BitmapField>& bfields,
                                               int from_oct, int to_oct) {
    auto sorted = bfields;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        int a_oct = a.bit / BITS_PER_BYTE;
        int b_oct = b.bit / BITS_PER_BYTE;
        if (a_oct != b_oct) return a_oct < b_oct;
        return a.bit > b.bit;
    });

    for (const auto& bf : sorted) {
        int byte_idx = bf.bit / BITS_PER_BYTE;
        if (byte_idx < from_oct || byte_idx > to_oct) continue;

        std::string member = to_member_name(bf.name);
        ctx_.line("if (" + member + ".has_value()) {");
        ctx_.indent();

        if (bf.is_choice && bf.choice_def) {
            ctx_.line("{");
            ctx_.indent();
            ctx_.line("auto _enc_r = std::visit([&w](const auto& v) -> conduit::VoidResult { return v.encode(w); }, *" + member + ");");
            ctx_.line("if (!_enc_r) return std::unexpected(_enc_r.error());");
            ctx_.dedent();
            ctx_.line("}");
        } else if (bf.is_enum) {
            ctx_.line("CONDUIT_TRY(encode_" + bf.cpp_type + "(*" + member + ", w));");
        } else if (bf.is_string) {
            if (bf.source_field && field_needs_encoding(*bf.source_field)) {
                ctx_.line("{");
                ctx_.indent();
                ctx_.line("auto wire = conduit::string::from_ascii(*" + member + ", " + field_encoding_enum(*bf.source_field) + ");");
                if (bf.length) {
                    std::string pad = resolve_padding_char(*bf.source_field);
                    ctx_.line("w.write_string(wire, " + std::to_string(*bf.length) + ", " + pad + ");");
                } else {
                    ctx_.line("w.write_string(wire, wire.size());");
                }
                ctx_.dedent();
                ctx_.line("}");
            } else {
                if (bf.length) {
                    ctx_.line("w.write_string(*" + member + ", " + std::to_string(*bf.length) + ");");
                } else {
                    ctx_.line("w.write_string(*" + member + ", " + member + "->size());");
                }
            }
        } else if (bf.is_bytes) {
            if (bf.length) {
                ctx_.line("w.write_bytes(std::span<const uint8_t>(" + member + "->data(), " + std::to_string(*bf.length) + "));");
            } else if (bf.bytes_attr) {
                ctx_.line("w.write_bytes(std::span<const uint8_t>(" + member + "->data(), " + std::to_string(*bf.bytes_attr) + "));");
            } else {
                ctx_.line("w.write_bytes(std::span<const uint8_t>(" + member + "->data(), " + member + "->size()));");
            }
        } else if (bf.is_struct) {
            ctx_.line("CONDUIT_TRY(" + member + "->encode(w));");
        } else if (bf.has_field_scale) {
            // Scaled bitmap field: reverse scale/offset and write raw bits
            FieldTypeInfo raw_fti;
            raw_fti.bits = bf.raw_bits;
            raw_fti.is_signed = bf.raw_signed;
            raw_fti.wire_encoding = bf.wire_encoding;
            std::string storage = storage_type_for_bits(bf.raw_bits, bf.raw_signed);
            std::string scale_str = double_literal(bf.field_scale);
            std::string offset_str = double_literal(bf.field_offset);
            std::string raw_expr = "static_cast<" + storage + ">((*" + member + " - " + offset_str + ") / " + scale_str + ")";
            emit_write_stmt(ctx_, raw_expr, raw_fti, bf.raw_endian, false);
        } else {
            FieldTypeInfo bfti;
            bfti.bits = bf.type_bits;
            bfti.is_signed = bf.is_signed;
            bfti.is_float = bf.is_float;
            bfti.cpp_type = bf.cpp_type;
            bfti.wire_encoding = bf.wire_encoding;
            emit_write_stmt(ctx_, "*" + member, bfti, bf.endian, false);
        }

        ctx_.dedent();
        ctx_.line("}");
    }
}

// ============================================================================
// Check if any field in the protocol uses a non-ASCII encoding
// ============================================================================

namespace {

bool protocol_needs_encoding(const model::Protocol& protocol) {
    std::function<bool(const std::vector<model::StructChild>&)> check_fields;
    check_fields = [&check_fields](const std::vector<model::StructChild>& children) -> bool {
        for (const auto& child : children) {
            if (auto* f = std::get_if<model::Field>(&child)) {
                if (f->encoding && (*f->encoding == model::StringEncoding::Ia5 ||
                                     *f->encoding == model::StringEncoding::Ebcdic)) {
                    return true;
                }
            } else if (auto* s = std::get_if<model::StructDef>(&child)) {
                if (check_fields(s->children)) return true;
            } else if (auto* a = std::get_if<model::ArrayDef>(&child)) {
                if (check_fields(a->children)) return true;
            } else if (auto* c = std::get_if<model::ChoiceDef>(&child)) {
                for (const auto& cs : c->cases) {
                    if (check_fields(cs.children)) return true;
                }
                if (c->otherwise && check_fields(c->otherwise->children)) return true;
            } else if (auto* fx = std::get_if<model::FxBlock>(&child)) {
                if (check_fields(fx->children)) return true;
            }
        }
        return false;
    };
    for (const auto& s : protocol.structs) {
        if (check_fields(s.children)) return true;
    }
    for (const auto& m : protocol.messages) {
        if (check_fields(m.children)) return true;
    }
    return false;
}

} // anonymous namespace

// ============================================================================
// Public entry points: generate_structs, generate_messages
// ============================================================================

std::string generate_structs(const model::Protocol& protocol,
                             const analyzer::TypeIndex& index,
                             const analyzer::WireSizeInfo& sizes,
                             const std::vector<analyzer::SessionInfo>& sessions,
                             const std::string& ns) {
    EmitContext ctx;

    ctx.line("// Generated by bgen - DO NOT EDIT");
    ctx.line("#pragma once");
    ctx.line();
    ctx.line("#include \"types.hpp\"");
    ctx.line("#include \"constants.hpp\"");
    ctx.line("#include <conduit/core/error.hpp>");
    ctx.line("#include <conduit/io/bit_reader.hpp>");
    ctx.line("#include <conduit/io/bit_writer.hpp>");
    ctx.line("#include <conduit/logging/logger.hpp>");
    if (protocol_needs_encoding(protocol)) {
        ctx.line("#include <conduit/string/encoding.hpp>");
    }
    ctx.line("#include <algorithm>");
    ctx.line("#include <array>");
    ctx.line("#include <bitset>");
    ctx.line("#include <cstdint>");
    ctx.line("#include <optional>");
    ctx.line("#include <span>");
    ctx.line("#include <sstream>");
    ctx.line("#include <string>");
    ctx.line("#include <utility>");
    ctx.line("#include <variant>");
    ctx.line("#include <vector>");
    ctx.line();
    ctx.line("namespace " + ns + " {");
    ctx.line();

    // Forward declarations
    for (const auto& s : protocol.structs) {
        if (!s.name.empty()) {
            ctx.line("class " + to_cpp_type_name(s.name) + ";");
        }
    }
    ctx.line();

    // Build leaf type_id map from ALL sessions
    std::unordered_map<std::string, uint64_t> leaf_type_ids;
    for (const auto& si : sessions) {
        for (const auto& lt : si.leaf_types) {
            leaf_type_ids[lt.name] = lt.type_id;
        }
    }

    StructEmitter emitter(ctx, index, sizes, ns);
    emitter.set_leaf_type_ids(leaf_type_ids);

    for (const auto& s : protocol.structs) {
        if (!s.name.empty()) {
            const analyzer::SessionInfo* session = nullptr;
            for (const auto& si : sessions) {
                for (const auto& lt : si.leaf_types) {
                    if (lt.name == s.name) {
                        session = &si;
                        break;
                    }
                }
                if (session) break;
            }
            if (session) {
                emitter.set_current_session(session);
            }
            emitter.emit_struct(s);
            if (session) {
                emitter.set_current_session(nullptr);
            }
        }
    }

    ctx.line("} // namespace " + ns);
    ctx.line();

    return ctx.str();
}

std::string generate_messages(const model::Protocol& protocol,
                              const analyzer::TypeIndex& index,
                              const analyzer::WireSizeInfo& sizes,
                              const std::vector<analyzer::SessionInfo>& sessions,
                              const std::string& ns) {
    EmitContext ctx;

    ctx.line("// Generated by bgen - DO NOT EDIT");
    ctx.line("#pragma once");
    ctx.line();
    ctx.line("#include \"structs.hpp\"");
    ctx.line("#include \"types.hpp\"");
    ctx.line("#include \"constants.hpp\"");
    ctx.line("#include <conduit/core/error.hpp>");
    ctx.line("#include <conduit/io/bit_reader.hpp>");
    ctx.line("#include <conduit/io/bit_writer.hpp>");
    ctx.line("#include <conduit/logging/logger.hpp>");
    if (protocol_needs_encoding(protocol)) {
        ctx.line("#include <conduit/string/encoding.hpp>");
    }
    ctx.line("#include <algorithm>");
    ctx.line("#include <any>");
    ctx.line("#include <array>");
    ctx.line("#include <bitset>");
    ctx.line("#include <cstdint>");
    ctx.line("#include <optional>");
    ctx.line("#include <span>");
    ctx.line("#include <sstream>");
    ctx.line("#include <string>");
    ctx.line("#include <string_view>");
    ctx.line("#include <utility>");
    ctx.line("#include <variant>");
    ctx.line("#include <vector>");
    ctx.line();
    ctx.line("namespace " + ns + " {");
    ctx.line();

    // Forward declarations
    for (const auto& m : protocol.messages) {
        ctx.line("class " + to_cpp_type_name(m.name) + ";");
    }
    ctx.line();

    // Build leaf type_id map from ALL sessions
    std::unordered_map<std::string, uint64_t> leaf_type_ids;
    for (const auto& si : sessions) {
        for (const auto& lt : si.leaf_types) {
            leaf_type_ids[lt.name] = lt.type_id;
        }
    }

    StructEmitter emitter(ctx, index, sizes, ns);
    emitter.set_leaf_type_ids(leaf_type_ids);

    // Find the frame-based session (if any) — applies to ALL messages
    const analyzer::SessionInfo* frame_session = nullptr;
    for (const auto& si : sessions) {
        if (si.is_frame_based) { frame_session = &si; break; }
    }

    for (const auto& m : protocol.messages) {
        emitter.emit_message(m, frame_session);
    }

    // Emit frame classes (v2 frame-based protocols)
    for (const auto& si : sessions) {
        if (si.is_frame_based && si.frame) {
            emit_frame_class(ctx, *si.frame, index, si, ns);
        }
    }

    ctx.line("} // namespace " + ns);
    ctx.line();

    return ctx.str();
}

} // namespace bgen::codegen
