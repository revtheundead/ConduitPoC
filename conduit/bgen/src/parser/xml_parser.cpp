// SPDX-License-Identifier: MIT
// Bgen - XML Parser Implementation

#include "xml_parser.hpp"
#include "expression_parser.hpp"
#include "auto_expr_parser.hpp"
#include <pugixml.hpp>
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace bgen::parser {

namespace {

// ============================================================================
// Helper: Parse context that accumulates errors
// ============================================================================

class XmlParseContext {
public:
    explicit XmlParseContext(const std::string& file_path,
                             std::vector<size_t> line_offsets = {})
        : file_path_(file_path), line_offsets_(std::move(line_offsets)) {}

    model::SourceLoc loc(const pugi::xml_node& node) const {
        model::SourceLoc sl;
        sl.file = file_path_;
        sl.offset = static_cast<int>(node.offset_debug());
        if (!line_offsets_.empty()) {
            auto off = static_cast<size_t>(sl.offset);
            // Binary search for the line containing this offset
            auto it = std::upper_bound(line_offsets_.begin(), line_offsets_.end(), off);
            sl.line = static_cast<int>(std::distance(line_offsets_.begin(), it));
            size_t line_start = (it != line_offsets_.begin()) ? *std::prev(it) : 0;
            sl.column = static_cast<int>(off - line_start) + 1;
        }
        return sl;
    }

    void error(const pugi::xml_node& node, const std::string& msg) {
        errors_.push_back(XmlParseError{msg, loc(node)});
    }

    void error(const model::SourceLoc& sl, const std::string& msg) {
        errors_.push_back(XmlParseError{msg, sl});
    }

    void warn(const pugi::xml_node& node, const std::string& msg) {
        warnings_.push_back({msg, loc(node)});
    }

    // Check for unrecognized attributes on an XML element
    void check_unknown_attrs(const pugi::xml_node& node,
                             std::initializer_list<const char*> known) {
        for (auto attr : node.attributes()) {
            bool found = false;
            for (auto k : known) {
                if (std::string_view(attr.name()) == k) { found = true; break; }
            }
            if (!found) {
                error(node, std::string("unrecognized attribute '") + attr.name() +
                     "' on <" + node.name() + "> element");
            }
        }
    }

    bool has_errors() const { return !errors_.empty(); }
    const std::vector<XmlParseError>& errors() const { return errors_; }
    const std::vector<std::pair<std::string, model::SourceLoc>>& warnings() const { return warnings_; }

    // Parse optional expression attribute
    std::unique_ptr<model::Expr> parse_expr_attr(const pugi::xml_node& node, const char* attr) {
        auto val = node.attribute(attr).value();
        if (!val || val[0] == '\0') return nullptr;
        auto result = parse_expression(val, loc(node));
        if (!result) {
            error(node, std::string("invalid expression in '") + attr + "': " + result.error().message);
            return nullptr;
        }
        return std::move(*result);
    }

    // Parse optional integer attribute, returns nullopt if not present
    std::optional<int> parse_int_attr(const pugi::xml_node& node, const char* attr) {
        auto a = node.attribute(attr);
        if (!a) return std::nullopt;
        std::string_view sv = a.value();
        int val = 0;
        std::errc ec{};
        const char* end = nullptr;
        if (sv.size() > 2 && sv[0] == '0' && (sv[1] == 'x' || sv[1] == 'X')) {
            auto hex = sv.substr(2);
            auto [ptr, e] = std::from_chars(hex.data(), hex.data() + hex.size(), val, 16);
            ec = e;
            end = hex.data() + hex.size();
            if (ec == std::errc{} && ptr != end) ec = std::errc::invalid_argument;
        } else {
            auto [ptr, e] = std::from_chars(sv.data(), sv.data() + sv.size(), val, 10);
            ec = e;
            end = sv.data() + sv.size();
            if (ec == std::errc{} && ptr != end) ec = std::errc::invalid_argument;
        }
        if (ec != std::errc{}) {
            error(node, std::string("invalid integer value for '") + attr + "': " + std::string(sv));
            return std::nullopt;
        }
        return val;
    }

    // Parse non-negative integer attribute (for bits, bytes, length)
    std::optional<int> parse_nonneg_int_attr(const pugi::xml_node& node, const char* attr) {
        auto val = parse_int_attr(node, attr);
        if (val && *val < 0) {
            error(node, std::string("'") + attr + "' must be non-negative, got " + std::to_string(*val));
            return std::nullopt;
        }
        return val;
    }

    std::optional<int64_t> parse_int64_attr(const pugi::xml_node& node, const char* attr) {
        auto a = node.attribute(attr);
        if (!a) return std::nullopt;
        std::string_view sv = a.value();
        int64_t val = 0;
        std::errc ec{};
        const char* end = nullptr;
        if (sv.size() > 2 && sv[0] == '0' && (sv[1] == 'x' || sv[1] == 'X')) {
            auto hex = sv.substr(2);
            auto [ptr, e] = std::from_chars(hex.data(), hex.data() + hex.size(), val, 16);
            ec = e;
            end = hex.data() + hex.size();
            if (ec == std::errc{} && ptr != end) ec = std::errc::invalid_argument;
        } else {
            auto [ptr, e] = std::from_chars(sv.data(), sv.data() + sv.size(), val, 10);
            ec = e;
            end = sv.data() + sv.size();
            if (ec == std::errc{} && ptr != end) ec = std::errc::invalid_argument;
        }
        if (ec != std::errc{}) {
            error(node, std::string("invalid integer value for '") + attr + "': " + std::string(sv));
            return std::nullopt;
        }
        return val;
    }

    // Get doc text from child <doc> element
    std::string get_doc(const pugi::xml_node& node) {
        auto doc_node = node.child("doc");
        if (doc_node) {
            return doc_node.text().get();
        }
        return {};
    }

    // Parse annotation children
    std::vector<model::Annotation> parse_annotations(const pugi::xml_node& node) {
        std::vector<model::Annotation> result;
        for (auto ann : node.children("annotation")) {
            model::Annotation a;
            a.name = ann.attribute("name").value();
            if (a.name.empty()) {
                error(ann, "<annotation> element missing 'name' attribute");
            }
            a.value = ann.attribute("value").value();
            if (a.value.empty()) a.value = ann.text().get();
            result.push_back(std::move(a));
        }
        return result;
    }

    // Parse enum values
    std::vector<model::EnumValue> parse_enum(const pugi::xml_node& enum_node) {
        std::vector<model::EnumValue> result;
        int64_t next_id = 0;
        for (auto val : enum_node.children("value")) {
            model::EnumValue ev;
            ev.name = val.attribute("name").value();
            if (ev.name.empty()) {
                error(val, "enum <value> element missing required 'name' attribute");
            }
            auto id_val = parse_int64_attr(val, "id");
            if (id_val) {
                ev.id = *id_val;
                next_id = (*id_val < INT64_MAX) ? *id_val + 1 : *id_val;
            } else {
                ev.id = next_id;
                if (next_id < INT64_MAX) next_id++;
            }
            ev.loc = loc(val);
            result.push_back(std::move(ev));
        }
        return result;
    }

    // Parse flag definitions
    std::vector<model::FlagDef> parse_flags(const pugi::xml_node& flags_node) {
        std::vector<model::FlagDef> result;
        for (auto flag : flags_node.children("flag")) {
            model::FlagDef fd;
            fd.name = flag.attribute("name").value();
            if (fd.name.empty()) {
                error(flag, "<flag> element missing required 'name' attribute");
            }
            auto b = parse_int_attr(flag, "bit");
            if (b) {
                fd.bit = *b;
            } else {
                error(flag, "<flag> '" + fd.name + "' missing required 'bit' attribute");
            }
            fd.loc = loc(flag);
            result.push_back(std::move(fd));
        }
        return result;
    }

    // Parse constraint child
    std::optional<model::Constraint> parse_constraint(const pugi::xml_node& parent) {
        auto node = parent.child("constraint");
        if (!node) return std::nullopt;

        model::Constraint c;
        auto eq = node.attribute("equals");
        if (eq) c.equals = eq.value();
        auto mn = node.attribute("min");
        if (mn) c.min = mn.value();
        auto mx = node.attribute("max");
        if (mx) c.max = mx.value();

        auto validate_attr = node.attribute("validate");
        if (validate_attr) {
            std::string_view v = validate_attr.value();
            if (v == "deferred") c.validate = model::ValidateTiming::Deferred;
            else if (v == "immediate") c.validate = model::ValidateTiming::Immediate;
            else {
                error(node, "unknown validate value '" + std::string(v) + "'; expected 'immediate' or 'deferred'");
                c.validate = model::ValidateTiming::Immediate;
            }
        }

        c.loc = loc(node);
        return c;
    }

    // Parse endian from attribute or child element
    std::pair<model::Endian, bool> parse_endian(const pugi::xml_node& node) {
        auto attr = node.attribute("endian");
        if (attr) {
            std::string_view v = attr.value();
            if (v == "little") return {model::Endian::Little, true};
            if (v == "big") return {model::Endian::Big, true};
            error(node, "unknown endian value '" + std::string(v) + "'; expected 'little' or 'big'");
            return {model::Endian::Big, true};
        }
        return {model::Endian::Big, false};
    }

    // Parse format attribute
    std::pair<model::DisplayFormat, bool> parse_format(const pugi::xml_node& node) {
        auto attr = node.attribute("format");
        if (attr) {
            std::string_view v = attr.value();
            if (v == "hex") return {model::DisplayFormat::Hex, true};
            if (v == "octal") return {model::DisplayFormat::Octal, true};
            if (v == "binary") return {model::DisplayFormat::Binary, true};
            if (v == "decimal") return {model::DisplayFormat::Decimal, true};
            error(node, "unknown format value '" + std::string(v) + "'; expected 'hex', 'octal', 'binary', or 'decimal'");
            return {model::DisplayFormat::Decimal, true};
        }
        return {model::DisplayFormat::Decimal, false};
    }

    // Parse string encoding
    model::StringEncoding parse_encoding(const pugi::xml_node& node, std::string_view v) {
        if (v == "ascii") return model::StringEncoding::Ascii;
        if (v == "utf8") return model::StringEncoding::Utf8;
        if (v == "ia5") return model::StringEncoding::Ia5;
        if (v == "ebcdic") return model::StringEncoding::Ebcdic;
        error(node, "unknown encoding value '" + std::string(v) + "'; expected 'ascii', 'utf8', 'ia5', or 'ebcdic'");
        return model::StringEncoding::Ascii;
    }

    model::StringPadding parse_padding(const pugi::xml_node& node, std::string_view v) {
        if (v == "null") return model::StringPadding::Null;
        if (v == "space") return model::StringPadding::Space;
        if (v == "none") return model::StringPadding::None;
        error(node, "unknown padding value '" + std::string(v) + "'; expected 'null', 'space', or 'none'");
        return model::StringPadding::Null;
    }

    model::StringTrim parse_trim_val(const pugi::xml_node& node, std::string_view v) {
        if (v == "right") return model::StringTrim::Right;
        if (v == "left") return model::StringTrim::Left;
        if (v == "both") return model::StringTrim::Both;
        if (v == "none") return model::StringTrim::None;
        error(node, "unknown trim value '" + std::string(v) + "'; expected 'right', 'left', 'both', or 'none'");
        return model::StringTrim::Right;
    }

    model::WireEncoding parse_wire_encoding(const pugi::xml_node& node, std::string_view v) {
        if (v == "cb2") return model::WireEncoding::CB2;
        if (v == "bnr") return model::WireEncoding::BNR;
        if (v == "bnr-s" || v == "bnr_s") return model::WireEncoding::BNR_S;
        if (v == "bcd") return model::WireEncoding::BCD;
        if (v == "bcd-s" || v == "bcd_s") return model::WireEncoding::BCD_S;
        error(node, "unknown wire-encoding '" + std::string(v) + "'; expected 'cb2', 'bnr', 'bnr-s', 'bcd', or 'bcd-s'");
        return model::WireEncoding::Default;
    }

    model::PrimitiveBase parse_base(const pugi::xml_node& node, std::string_view v) {
        if (v == "uint") return model::PrimitiveBase::Uint;
        if (v == "int") return model::PrimitiveBase::Int;
        if (v == "float") return model::PrimitiveBase::Float;
        if (v == "bool") return model::PrimitiveBase::Bool;
        if (v == "bytes") return model::PrimitiveBase::Bytes;
        if (v == "string") return model::PrimitiveBase::String;
        error(node, "unknown base value '" + std::string(v) + "'; expected 'uint', 'int', 'float', 'bool', 'bytes', or 'string'");
        return model::PrimitiveBase::Uint;
    }

    // Parse direction attribute
    model::Direction parse_direction(const pugi::xml_node& node) {
        auto attr = node.attribute("direction");
        if (attr) {
            std::string_view v = attr.value();
            if (v == "send") return model::Direction::Send;
            if (v == "receive") return model::Direction::Receive;
            if (v == "both") return model::Direction::Both;
            error(node, "unknown direction value '" + std::string(v) + "'; expected 'send', 'receive', or 'both'");
        }
        return model::Direction::Both;
    }

    // ========================================================================
    // Parse type definition
    // ========================================================================

    model::TypeDef parse_type(const pugi::xml_node& node) {
        model::TypeDef td;
        td.name = node.attribute("name").value();
        td.loc = loc(node);
        if (td.name.empty()) {
            error(node, "<type> element missing required 'name' attribute");
        }

        auto base_attr = node.attribute("base");
        if (base_attr) td.base = parse_base(node, base_attr.value());

        auto bits = parse_nonneg_int_attr(node, "bits");
        if (bits) td.bits = *bits;

        auto type_bytes = parse_nonneg_int_attr(node, "bytes");
        if (type_bytes) {
            if (*type_bytes > 0x0FFF'FFFF) {
                error(node, "<type> bytes attribute too large");
            } else if (bits) {
                // Combine bytes + bits
                td.bits = *type_bytes * 8 + *bits;
            } else {
                td.bits = *type_bytes * 8;
            }
        }

        auto [endian, endian_explicit] = parse_endian(node);
        td.endian = endian;
        td.endian_explicit = endian_explicit;

        auto [fmt, fmt_explicit] = parse_format(node);
        td.format = fmt;
        td.format_explicit = fmt_explicit;

        auto we_attr = node.attribute("wire-encoding");
        if (we_attr) {
            td.wire_encoding = parse_wire_encoding(node, we_attr.value());
            td.wire_encoding_explicit = true;
        }

        // Scale/offset/unit from child elements
        auto scale_node = node.child("scale");
        if (scale_node) td.scale = scale_node.text().as_double();
        auto offset_node = node.child("offset");
        if (offset_node) td.offset = offset_node.text().as_double();
        auto unit_node = node.child("unit");
        if (unit_node) td.unit = unit_node.text().get();

        // String attributes
        td.length = parse_nonneg_int_attr(node, "length");
        auto enc_attr = node.attribute("encoding");
        if (enc_attr) { td.encoding = parse_encoding(node, enc_attr.value()); td.encoding_explicit = true; }
        auto pad_attr = node.attribute("padding");
        if (pad_attr) { td.padding = parse_padding(node, pad_attr.value()); td.padding_explicit = true; }
        auto trim_attr = node.attribute("trim");
        if (trim_attr) { td.trim = parse_trim_val(node, trim_attr.value()); td.trim_explicit = true; }
        auto term_attr = node.attribute("terminated");
        if (term_attr) td.terminated = term_attr.value();
        td.max_length = parse_int_attr(node, "max-length");
        td.char_bits = parse_int_attr(node, "char-bits");

        // Enum
        auto enum_node = node.child("enum");
        if (enum_node) td.enum_values = parse_enum(enum_node);

        // Flags
        auto flags_node = node.child("flags");
        if (flags_node) td.flags = parse_flags(flags_node);

        // Constraint
        td.constraint = parse_constraint(node);

        td.doc = get_doc(node);
        td.annotations = parse_annotations(node);

        check_unknown_attrs(node, {
            "name", "base", "bits", "bytes", "endian", "format", "wire-encoding",
            "length", "encoding", "padding", "trim", "terminated",
            "max-length", "char-bits"
        });

        return td;
    }

    // ========================================================================
    // Parse struct children (recursive)
    // ========================================================================

    std::vector<model::StructChild> parse_struct_children(const pugi::xml_node& parent);

    model::Field parse_field(const pugi::xml_node& node) {
        model::Field f;
        f.name = node.attribute("name").value();
        f.loc = loc(node);
        if (f.name.empty()) {
            error(node, "<field> element missing required 'name' attribute");
        }

        auto type_attr = node.attribute("type");
        if (type_attr) f.type_ref = type_attr.value();

        auto bits = parse_nonneg_int_attr(node, "bits");
        if (bits) f.bits = *bits;

        auto bytes = parse_nonneg_int_attr(node, "bytes");
        if (bytes) f.bytes_attr = *bytes;

        auto signed_attr = node.attribute("signed");
        if (signed_attr) f.is_signed = std::string_view(signed_attr.value()) == "true";

        // Parse base attribute for inline field type
        auto base_attr = node.attribute("base");
        if (base_attr) {
            f.base = parse_base(node, base_attr.value());
            if (*f.base == model::PrimitiveBase::Int) f.is_signed = true;
        }

        // Combine bytes + bits: spec says bytes="2" bits="3" = 19 bits
        if (bits && bytes) {
            if (*bytes > 0x0FFF'FFFF) { // Guard against int overflow in *bytes * 8
                error(node, "bytes attribute too large");
            } else {
                f.bits = *bytes * 8 + *bits;
            }
            f.bytes_attr = std::nullopt;
        }

        // If no type but has bits, bytes, or base, it's inline
        if (f.type_ref.empty() && (f.bits || f.bytes_attr || f.base)) {
            f.type_is_inline = true;
        }

        // Scale/offset/unit from child elements
        auto scale_node = node.child("scale");
        if (scale_node) f.scale = scale_node.text().as_double();
        auto offset_node = node.child("offset");
        if (offset_node) f.offset = offset_node.text().as_double();
        auto unit_node = node.child("unit");
        if (unit_node) f.unit = unit_node.text().get();

        // Enum/flags inline
        auto enum_node = node.child("enum");
        if (enum_node) f.enum_values = parse_enum(enum_node);
        auto flags_node = node.child("flags");
        if (flags_node) f.flags = parse_flags(flags_node);

        // Constraint
        f.constraint = parse_constraint(node);

        // Presence
        auto bit_attr = parse_int_attr(node, "bit");
        if (bit_attr) f.bit = *bit_attr;

        f.present_when = parse_expr_attr(node, "present-when");

        // Length determination
        auto length_attr = node.attribute("length");
        if (length_attr) {
            std::string_view lv = length_attr.value();
            if (lv == "*") {
                f.length_star = true;
            } else {
                f.length = parse_nonneg_int_attr(node, "length");
            }
        }

        f.length_from = parse_expr_attr(node, "length-from");

        auto lp_attr = node.attribute("length-prefix");
        if (lp_attr) f.length_prefix = lp_attr.value();
        auto lip_attr = node.attribute("length-includes-prefix");
        if (lip_attr) {
            f.length_includes_prefix = std::string_view(lip_attr.value()) == "true";
            if (f.length_includes_prefix && !f.length_prefix.has_value()) {
                error(node, "field '" + f.name + "' has length-includes-prefix without length-prefix");
            }
        }

        // String attributes
        auto enc_attr = node.attribute("encoding");
        if (enc_attr) f.encoding = parse_encoding(node, enc_attr.value());
        auto pad_attr = node.attribute("padding");
        if (pad_attr) f.padding = parse_padding(node, pad_attr.value());
        auto trim_attr = node.attribute("trim");
        if (trim_attr) f.trim = parse_trim_val(node, trim_attr.value());
        auto term_attr = node.attribute("terminated");
        if (term_attr) f.terminated = term_attr.value();
        f.max_length = parse_int_attr(node, "max-length");
        f.char_bits = parse_int_attr(node, "char-bits");

        // Validate mutual exclusivity of length specifications
        // (must be after terminated is parsed)
        // Note: length + terminated is valid (fixed-size read with terminator trim).
        // Only dynamic-length specs conflict with each other and with terminated.
        {
            int length_specs = 0;
            if (f.length || f.length_star) ++length_specs;
            if (f.length_from) ++length_specs;
            if (f.length_prefix.has_value()) ++length_specs;
            if (length_specs > 1) {
                error(node, "field '" + f.name + "' has conflicting length specifications "
                      "(use only one of: length, length-from, length-prefix)");
            }
            if (f.terminated && (f.length_from || f.length_prefix.has_value() || f.length_star)) {
                error(node, "field '" + f.name + "' has conflicting length specifications "
                      "(terminated cannot be combined with length-from, length-prefix, or open-ended length)");
            }
        }

        // Endian/format
        auto [endian, endian_explicit] = parse_endian(node);
        f.endian = endian;
        f.endian_explicit = endian_explicit;
        auto [fmt, fmt_explicit] = parse_format(node);
        f.format = fmt;
        f.format_explicit = fmt_explicit;

        auto we_attr = node.attribute("wire-encoding");
        if (we_attr) f.wire_encoding = parse_wire_encoding(node, we_attr.value());

        // Inline
        auto inline_attr = node.attribute("inline");
        if (inline_attr) f.is_inline = std::string_view(inline_attr.value()) == "true";

        // Default/auto
        auto def_attr = node.attribute("default");
        if (def_attr) f.default_value = def_attr.value();
        auto init_attr = node.attribute("initial");
        if (init_attr) {
            error(node, "attribute 'initial' is not supported; use 'default' instead");
        }
        // Reject <initial> child element
        auto init_node = node.child("initial");
        if (init_node) {
            error(node, "element <initial> is not supported; use the 'default' attribute instead");
        }

        auto auto_attr = node.attribute("auto");
        if (auto_attr) {
            f.auto_attr = auto_attr.value();
            // Parse into structured form; if it fails, leave auto_expr as nullopt
            // and let the validator catch unsupported auto values.
            auto expr_result = parse_auto_expr(f.auto_attr.value());
            if (expr_result) {
                f.auto_expr = std::move(*expr_result);
            }
        }

        f.doc = get_doc(node);
        f.annotations = parse_annotations(node);

        check_unknown_attrs(node, {
            "name", "type", "bits", "bytes", "signed", "base", "bit", "present-when",
            "length", "length-from", "length-prefix", "length-includes-prefix",
            "encoding", "padding", "trim", "terminated", "max-length", "char-bits",
            "endian", "format", "wire-encoding", "inline", "default", "auto"
        });

        return f;
    }

    model::StructDef parse_struct(const pugi::xml_node& node) {
        model::StructDef sd;
        sd.name = node.attribute("name").value();
        sd.loc = loc(node);
        if (sd.name.empty()) {
            error(node, "<struct> element missing required 'name' attribute");
        }

        // Reject 'role' attribute on <struct> (only valid on <message>)
        if (node.attribute("role")) {
            error(node, "'role' attribute is not valid on <struct> element '" + sd.name + "'");
        }

        // Presence
        auto presence_attr = node.attribute("presence");
        if (presence_attr && std::string_view(presence_attr.value()) == "bitmap") {
            sd.is_bitmap = true;
        }

        // Bitmap child
        auto bitmap_node = node.child("bitmap");
        if (bitmap_node) {
            auto bits = parse_int_attr(bitmap_node, "bits");
            if (bits) sd.bitmap_bits = *bits;
            auto ext = bitmap_node.attribute("ext");
            if (ext) {
                std::string_view v = ext.value();
                if (v == "none") {
                    // Explicitly no extension bit
                } else {
                    auto ext_val = parse_nonneg_int_attr(bitmap_node, "ext");
                    if (ext_val) {
                        sd.bitmap_ext = *ext_val;
                    } else {
                        error(bitmap_node, "<bitmap> ext attribute must be 'none' or a valid integer, got '" +
                             std::string(v) + "'");
                    }
                }
            }
        }

        auto bit_attr = parse_int_attr(node, "bit");
        if (bit_attr) sd.bit = *bit_attr;
        sd.present_when = parse_expr_attr(node, "present-when");

        auto type_name_attr = node.attribute("typeName");
        if (type_name_attr) sd.type_name = type_name_attr.value();

        sd.children = parse_struct_children(node);
        sd.doc = get_doc(node);
        sd.annotations = parse_annotations(node);

        check_unknown_attrs(node, {
            "name", "presence", "bit", "present-when", "typeName"
        });

        return sd;
    }

    model::ArrayDef parse_array(const pugi::xml_node& node) {
        model::ArrayDef ad;
        ad.name = node.attribute("name").value();
        ad.loc = loc(node);
        if (ad.name.empty()) {
            error(node, "<array> element missing required 'name' attribute");
        }

        auto type_attr = node.attribute("type");
        if (type_attr) ad.type_ref = type_attr.value();

        // Count
        auto count_attr = node.attribute("count");
        if (count_attr) {
            std::string_view cv = count_attr.value();
            if (cv == "*") {
                ad.count_star = true;
            } else {
                int val = 0;
                auto [ptr, ec] = std::from_chars(cv.data(), cv.data() + cv.size(), val, 10);
                if (ec != std::errc{}) {
                    error(node, "invalid integer value for 'count': " + std::string(cv));
                } else {
                    ad.fixed_count = val;
                }
            }
        }
        ad.count_from = parse_expr_attr(node, "count-from");

        // Validate mutual exclusivity of count specifications
        if ((ad.fixed_count || ad.count_star) && ad.count_from) {
            error(node, "array '" + ad.name + "' has conflicting count specifications "
                  "(use only one of: count, count-from)");
        }

        // Length
        ad.length = parse_nonneg_int_attr(node, "length");
        ad.length_from = parse_expr_attr(node, "length-from");

        if (ad.length && ad.length_from) {
            error(node, "array '" + ad.name + "' has conflicting length specifications "
                  "(use only one of: length, length-from)");
        }

        // Presence
        auto bit_attr = parse_int_attr(node, "bit");
        if (bit_attr) ad.bit = *bit_attr;
        ad.present_when = parse_expr_attr(node, "present-when");

        // Inline children (if no type attribute)
        if (!type_attr) {
            ad.children = parse_struct_children(node);
        }

        auto type_name_attr = node.attribute("typeName");
        if (type_name_attr) ad.type_name = type_name_attr.value();

        ad.doc = get_doc(node);
        ad.annotations = parse_annotations(node);

        check_unknown_attrs(node, {
            "name", "type", "count", "count-from", "length", "length-from",
            "bit", "present-when", "typeName"
        });

        return ad;
    }

    model::ChoiceDef parse_choice(const pugi::xml_node& node) {
        model::ChoiceDef cd;
        cd.name = node.attribute("name").value();
        cd.loc = loc(node);
        if (cd.name.empty()) {
            error(node, "<choice> element missing required 'name' attribute");
        }

        cd.switch_expr = parse_expr_attr(node, "switch");

        // Length
        cd.length = parse_nonneg_int_attr(node, "length");
        cd.length_from = parse_expr_attr(node, "length-from");

        if (cd.length && cd.length_from) {
            error(node, "choice '" + cd.name + "' has conflicting length specifications "
                  "(use only one of: length, length-from)");
        }

        // Presence
        auto bit_attr = parse_int_attr(node, "bit");
        if (bit_attr) cd.bit = *bit_attr;
        cd.present_when = parse_expr_attr(node, "present-when");

        // Cases
        for (auto case_node : node.children("case")) {
            model::CaseDef cdef;
            cdef.name = case_node.attribute("name").value();
            cdef.loc = loc(case_node);
            if (cdef.name.empty()) {
                error(case_node, "<case> element missing required 'name' attribute");
            }

            auto type_attr = case_node.attribute("type");
            if (type_attr) cdef.type_ref = type_attr.value();

            auto value_attr = case_node.attribute("value");
            if (value_attr) cdef.value = value_attr.value();

            auto range_attr = case_node.attribute("range");
            if (range_attr) cdef.range = range_attr.value();

            cdef.direction = parse_direction(case_node);

            auto type_name_attr = case_node.attribute("typeName");
            if (type_name_attr) cdef.type_name = type_name_attr.value();

            // Inline children
            if (!type_attr) {
                cdef.children = parse_struct_children(case_node);
            }

            cdef.doc = get_doc(case_node);
            cdef.annotations = parse_annotations(case_node);

            check_unknown_attrs(case_node, {
                "name", "type", "value", "range", "direction", "typeName"
            });

            cd.cases.push_back(std::move(cdef));
        }

        // Verify no <case> appears after <otherwise> in XML child order
        {
            bool seen_otherwise = false;
            for (auto child : node.children()) {
                std::string_view cname = child.name();
                if (cname == "otherwise") {
                    seen_otherwise = true;
                } else if (cname == "case" && seen_otherwise) {
                    error(child, "<case> must not appear after <otherwise> in choice '" + cd.name + "'");
                    break;
                }
            }
        }

        // Otherwise
        auto other = node.child("otherwise");
        if (other) {
            // Reject 'direction' attribute on <otherwise>
            if (other.attribute("direction")) {
                error(other, "'direction' attribute is not valid on <otherwise> in choice '" + cd.name + "'");
            }

            model::OtherwiseDef od;
            od.name = other.attribute("name").value();
            od.loc = loc(other);

            auto type_attr = other.attribute("type");
            if (type_attr) od.type_ref = type_attr.value();

            auto type_name_attr = other.attribute("typeName");
            if (type_name_attr) od.type_name = type_name_attr.value();

            if (!type_attr) {
                od.children = parse_struct_children(other);
            }

            od.doc = get_doc(other);
            od.annotations = parse_annotations(other);

            check_unknown_attrs(other, {"name", "type", "typeName"});

            cd.otherwise = std::move(od);
        }

        // Error on unrecognized children inside <choice>
        for (auto child : node.children()) {
            std::string_view cname = child.name();
            if (cname != "case" && cname != "otherwise" &&
                cname != "doc" && cname != "annotation") {
                error(child, "unrecognized element <" + std::string(cname) + "> inside <choice>");
            }
        }

        cd.doc = get_doc(node);
        cd.annotations = parse_annotations(node);

        check_unknown_attrs(node, {
            "name", "switch", "length", "length-from", "bit", "present-when"
        });

        return cd;
    }

    model::FxBlock parse_fx(const pugi::xml_node& node) {
        model::FxBlock fx;
        fx.loc = loc(node);
        fx.children = parse_struct_children(node);
        return fx;
    }

    model::Reserved parse_reserved(const pugi::xml_node& node) {
        model::Reserved r;
        r.loc = loc(node);
        int total = 0;
        auto bits = parse_nonneg_int_attr(node, "bits");
        if (bits) total += *bits;
        auto bytes = parse_nonneg_int_attr(node, "bytes");
        if (bytes) {
            if (*bytes > 0x0FFF'FFFF) { // Guard against int overflow in *bytes * 8
                error(node, "<reserved> bytes attribute too large");
                return r;
            }
            total += *bytes * 8;
        }
        if (total <= 0) {
            error(node, "<reserved> element must specify non-zero 'bits' or 'bytes'");
            return r;
        }
        r.bits = total;

        check_unknown_attrs(node, {"bits", "bytes"});

        return r;
    }

    model::Align parse_align(const pugi::xml_node& node) {
        model::Align a;
        a.loc = loc(node);
        auto to = parse_int_attr(node, "to");
        if (!to) {
            error(node, "<align> element missing required 'to' attribute");
        } else {
            a.to = *to;
        }

        check_unknown_attrs(node, {"to"});

        return a;
    }

    // ========================================================================
    // Parse import
    // ========================================================================

    model::ImportDef parse_import(const pugi::xml_node& node) {
        model::ImportDef imp;
        imp.href = node.attribute("href").value();
        auto ns_attr = node.attribute("ns");
        if (ns_attr) imp.ns = ns_attr.value();
        imp.loc = loc(node);
        if (imp.href.empty()) {
            error(node, "<import> element missing required 'href' attribute");
        }

        check_unknown_attrs(node, {"href", "ns"});

        return imp;
    }

    // ========================================================================
    // Parse constant
    // ========================================================================

    model::ConstDef parse_const(const pugi::xml_node& node) {
        model::ConstDef cd;
        cd.name = node.attribute("name").value();
        cd.type_ref = node.attribute("type").value();
        cd.value = node.attribute("value").value();
        cd.doc = get_doc(node);
        cd.annotations = parse_annotations(node);
        cd.loc = loc(node);
        if (cd.name.empty()) {
            error(node, "<const> element missing required 'name' attribute");
        }
        if (cd.type_ref.empty()) {
            error(node, "<const> element missing required 'type' attribute");
        }
        if (cd.value.empty()) {
            error(node, "<const> element missing required 'value' attribute");
        }

        check_unknown_attrs(node, {"name", "type", "value"});

        return cd;
    }

    // ========================================================================
    // Parse message
    // ========================================================================

    model::MessageDef parse_message(const pugi::xml_node& node) {
        model::MessageDef md;
        md.name = node.attribute("name").value();
        md.loc = loc(node);
        if (md.name.empty()) {
            error(node, "<message> element missing required 'name' attribute");
        }

        auto id_attr = node.attribute("id");
        if (id_attr) md.id = id_attr.value();

        md.direction = parse_direction(node);

        md.children = parse_struct_children(node);
        md.doc = get_doc(node);
        md.annotations = parse_annotations(node);

        check_unknown_attrs(node, {"name", "id", "direction"});

        return md;
    }

    // ========================================================================
    // Parse defaults
    // ========================================================================

    model::Defaults parse_defaults(const pugi::xml_node& node) {
        model::Defaults d;
        auto endian_node = node.child("endian");
        if (endian_node) {
            std::string_view v = endian_node.text().get();
            if (v == "little") d.endian = model::Endian::Little;
            else if (v == "big") d.endian = model::Endian::Big;
            else error(endian_node, "unknown endian value '" + std::string(v) + "'; expected 'little' or 'big'");
        }
        auto enc_node = node.child("string-encoding");
        if (enc_node) d.string_encoding = parse_encoding(enc_node, enc_node.text().get());
        auto pad_node = node.child("string-padding");
        if (pad_node) d.string_padding = parse_padding(pad_node, pad_node.text().get());
        auto trim_node = node.child("string-trim");
        if (trim_node) d.string_trim = parse_trim_val(trim_node, trim_node.text().get());
        auto ns_node = node.child("namespace");
        if (ns_node) d.namespace_ = ns_node.text().get();

        for (auto child : node.children()) {
            std::string_view cname = child.name();
            if (cname != "endian" && cname != "string-encoding" &&
                cname != "string-padding" && cname != "string-trim" &&
                cname != "namespace") {
                error(child, "unrecognized element <" + std::string(cname) + "> inside <defaults>");
            }
        }
        return d;
    }

    // ========================================================================
    // Parse frame
    // ========================================================================

    model::FrameDef parse_frame(const pugi::xml_node& node) {
        model::FrameDef fd;
        fd.name = node.attribute("name").value();
        fd.loc = loc(node);
        if (fd.name.empty()) {
            error(node, "<frame> element missing required 'name' attribute");
        }

        // Collect children before and after <payload/>
        bool seen_payload = false;
        for (auto child : node.children()) {
            std::string_view cname = child.name();

            if (cname == "payload") {
                if (seen_payload) {
                    error(child, "duplicate <payload/> in frame '" + fd.name + "'");
                    continue;
                }
                seen_payload = true;

                // Parse payload attributes
                auto count_attr = child.attribute("count");
                if (count_attr && std::string_view(count_attr.value()) == "*") {
                    fd.payload.is_array = true;
                }
                fd.payload.length_from = parse_expr_attr(child, "length-from");
                fd.payload.loc = loc(child);

                check_unknown_attrs(child, {"count", "length-from"});
            } else if (cname == "field" || cname == "struct" || cname == "array" ||
                       cname == "choice" || cname == "fx" || cname == "reserved" ||
                       cname == "align") {
                // Route to appropriate parser
                model::StructChild sc;
                if (cname == "field") sc = parse_field(child);
                else if (cname == "struct") sc = parse_struct(child);
                else if (cname == "array") sc = parse_array(child);
                else if (cname == "choice") sc = parse_choice(child);
                else if (cname == "fx") sc = parse_fx(child);
                else if (cname == "reserved") sc = parse_reserved(child);
                else sc = parse_align(child);

                if (!seen_payload) {
                    fd.header_fields.push_back(std::move(sc));
                } else {
                    fd.footer_fields.push_back(std::move(sc));
                }
            } else if (cname != "doc" && cname != "annotation" && !cname.empty()) {
                error(child, "unrecognized element <" + std::string(cname) + "> inside <frame>");
            }
        }

        if (!seen_payload) {
            error(node, "<frame> '" + fd.name + "' missing required <payload/> element");
        }

        fd.doc = get_doc(node);
        fd.annotations = parse_annotations(node);

        check_unknown_attrs(node, {"name"});

        return fd;
    }

private:
    std::string file_path_;
    std::vector<size_t> line_offsets_;
    std::vector<XmlParseError> errors_;
    std::vector<std::pair<std::string, model::SourceLoc>> warnings_;
};

// Parse struct children recursively
std::vector<model::StructChild> XmlParseContext::parse_struct_children(const pugi::xml_node& parent) {
    std::vector<model::StructChild> children;

    for (auto child : parent.children()) {
        std::string_view name = child.name();

        if (name == "field") {
            children.push_back(parse_field(child));
        } else if (name == "struct") {
            children.push_back(parse_struct(child));
        } else if (name == "array") {
            children.push_back(parse_array(child));
        } else if (name == "choice") {
            children.push_back(parse_choice(child));
        } else if (name == "fx") {
            children.push_back(parse_fx(child));
        } else if (name == "reserved") {
            children.push_back(parse_reserved(child));
        } else if (name == "align") {
            children.push_back(parse_align(child));
        } else if (name != "doc" && name != "annotation" && name != "bitmap" &&
                   !name.empty()) {
            error(child, "unrecognized child element <" + std::string(name) + ">");
        }
    }

    return children;
}

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

XmlParseResult parse_bmdl_file(const std::string& file_path) {
    // Read file into memory to build line offset index
    std::ifstream ifs(file_path, std::ios::binary);
    std::string file_content;
    std::vector<size_t> line_offsets;
    if (ifs) {
        file_content.assign(std::istreambuf_iterator<char>(ifs),
                            std::istreambuf_iterator<char>());
        ifs.close();
        // Build line start offsets: line 1 starts at 0
        line_offsets.push_back(0);
        for (size_t i = 0; i < file_content.size(); ++i) {
            if (file_content[i] == '\n') {
                line_offsets.push_back(i + 1);
            }
        }
    }

    pugi::xml_document doc;
    auto result = file_content.empty()
        ? doc.load_file(file_path.c_str())
        : doc.load_buffer(file_content.data(), file_content.size());
    if (!result) {
        model::SourceLoc sl;
        sl.file = file_path;
        sl.offset = static_cast<int>(result.offset);
        if (!line_offsets.empty()) {
            auto it = std::upper_bound(line_offsets.begin(), line_offsets.end(),
                                       static_cast<size_t>(sl.offset));
            sl.line = static_cast<int>(std::distance(line_offsets.begin(), it));
            size_t line_start = (it != line_offsets.begin()) ? *std::prev(it) : 0;
            sl.column = static_cast<int>(static_cast<size_t>(sl.offset) - line_start) + 1;
        }
        return std::unexpected(std::vector<XmlParseError>{
            XmlParseError{"XML parse error: " + std::string(result.description()), sl}
        });
    }

    XmlParseContext ctx(file_path, std::move(line_offsets));
    model::BmdlFile bmdl;
    bmdl.file_path = file_path;

    auto root = doc.child("bmdl");
    if (!root) {
        model::SourceLoc sl;
        sl.file = file_path;
        return std::unexpected(std::vector<XmlParseError>{
            XmlParseError{"missing <bmdl> root element", sl}
        });
    }

    auto version_attr = root.attribute("version");
    if (!version_attr) {
        ctx.error(root, "<bmdl> missing required 'version' attribute");
    } else {
        bmdl.bmdl_version = version_attr.value();
        if (bmdl.bmdl_version != "2.0") {
            ctx.warn(root, "unsupported BMDL version '" + bmdl.bmdl_version +
                     "'; only version '2.0' is supported");
        }
    }
    bmdl.loc = ctx.loc(root);
    ctx.check_unknown_attrs(root, {"version"});

    // Parse root-level imports (always, before protocol check)
    for (auto child : root.children("import")) {
        bmdl.imports.push_back(ctx.parse_import(child));
    }

    // Helper lambda: parse constants, types, structs, messages, frames from a container node.
    // NOTE: imports are NOT parsed here — they're handled separately since root-level
    // imports are always pre-parsed before the protocol/v2/library branch.
    auto parse_definitions = [&](const pugi::xml_node& container) {
        for (auto constants_block : container.children("constants")) {
            for (auto c : constants_block.children()) {
                std::string_view cname = c.name();
                if (cname == "const") {
                    bmdl.constants.push_back(ctx.parse_const(c));
                } else {
                    ctx.error(c, "unrecognized element <" + std::string(cname) + "> inside <constants>");
                }
            }
        }

        for (auto types_block : container.children("types")) {
            for (auto child : types_block.children()) {
                std::string_view cname = child.name();
                if (cname == "type") {
                    bmdl.types.push_back(ctx.parse_type(child));
                } else if (cname == "struct") {
                    bmdl.structs.push_back(ctx.parse_struct(child));
                } else {
                    ctx.error(child, "unrecognized element <" + std::string(cname) + "> inside <types>");
                }
            }
        }

        for (auto messages_block : container.children("messages")) {
            for (auto m : messages_block.children()) {
                std::string_view cname = m.name();
                if (cname == "message") {
                    bmdl.messages.push_back(ctx.parse_message(m));
                } else {
                    ctx.error(m, "unrecognized element <" + std::string(cname) + "> inside <messages>");
                }
            }
        }

        for (auto frame_node : container.children("frame")) {
            bmdl.frames.push_back(ctx.parse_frame(frame_node));
        }
    };

    // Check for <protocol>
    auto protocol = root.child("protocol");
    bool has_root_frame = root.child("frame") ? true : false;

    if (protocol) {
        // v1 path: <protocol> wrapper
        bmdl.has_protocol = true;
        auto name_attr = protocol.attribute("name");
        auto ver_attr = protocol.attribute("version");
        if (!name_attr) ctx.error(protocol, "<protocol> missing required 'name' attribute");
        if (!ver_attr) ctx.error(protocol, "<protocol> missing required 'version' attribute");
        if (name_attr) bmdl.protocol_name = name_attr.value();
        if (ver_attr) bmdl.protocol_version = ver_attr.value();
        ctx.check_unknown_attrs(protocol, {"name", "version"});

        // Parse defaults
        auto defaults_node = protocol.child("defaults");
        if (defaults_node) {
            bmdl.defaults = ctx.parse_defaults(defaults_node);
        }

        // Parse doc
        bmdl.doc = ctx.get_doc(protocol);

        // Parse protocol-level imports (root-level already parsed above)
        for (auto child : protocol.children("import")) {
            bmdl.imports.push_back(ctx.parse_import(child));
        }

        // Parse all definitions under <protocol>
        parse_definitions(protocol);

        // Error on unrecognized child elements inside <protocol>
        for (auto child : protocol.children()) {
            std::string_view cname = child.name();
            if (cname != "defaults" && cname != "import" && cname != "constants" &&
                cname != "types" && cname != "messages" && cname != "frame" && cname != "doc") {
                ctx.error(child, "unrecognized element <" + std::string(cname) + "> inside <protocol>");
            }
        }
    } else if (root.child("defaults")) {
        // v2 flat file: <defaults> at root level → this is a protocol file
        bmdl.has_protocol = true;

        // Parse defaults if present at root
        auto defaults_node = root.child("defaults");
        if (defaults_node) {
            bmdl.defaults = ctx.parse_defaults(defaults_node);
        }

        // Protocol name from namespace or frame name
        if (bmdl.defaults.namespace_) {
            bmdl.protocol_name = *bmdl.defaults.namespace_;
        } else if (has_root_frame) {
            // Use first frame name as fallback
            auto first_frame = root.child("frame");
            if (first_frame) {
                auto fname = first_frame.attribute("name");
                if (fname) bmdl.protocol_name = fname.value();
            }
        }

        // v2 protocol version is the bmdl version
        bmdl.protocol_version = bmdl.bmdl_version;

        bmdl.doc = ctx.get_doc(root);

        // Parse all definitions at root level
        parse_definitions(root);

        // Error on unrecognized root children
        for (auto child : root.children()) {
            std::string_view cname = child.name();
            if (cname != "import" && cname != "defaults" && cname != "constants" &&
                cname != "types" && cname != "messages" && cname != "frame" &&
                cname != "doc") {
                ctx.error(child, "unrecognized element <" + std::string(cname) + "> in v2 file");
            }
        }
    } else {
        // Library file: parse directly under <bmdl>
        bmdl.defaults = model::BUILTIN_DEFAULTS;

        // Root-level imports already parsed above — parse remaining definitions
        parse_definitions(root);

        // Error on unrecognized child elements at root level (library file)
        for (auto child : root.children()) {
            std::string_view cname = child.name();
            if (cname != "import" && cname != "constants" &&
                cname != "types" && cname != "messages" && cname != "frame" &&
                cname != "doc" && cname != "annotation") {
                ctx.error(child, "unrecognized element <" + std::string(cname) + "> in library file");
            }
        }
    }

    if (ctx.has_errors()) {
        return std::unexpected(ctx.errors());
    }

    bmdl.warnings = ctx.warnings();

    return bmdl;
}

} // namespace bgen::parser
