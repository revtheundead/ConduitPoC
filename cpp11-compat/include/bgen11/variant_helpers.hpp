#ifndef BGEN11_VARIANT_HELPERS_HPP
#define BGEN11_VARIANT_HELPERS_HPP

#include <string>
#include <utility>
#include <vector>
#include "error.hpp"
#include "bit_writer.hpp"
#include "session_traits.hpp"
#include "../compat11/variant.hpp"
#include "../compat11/visit.hpp"
#include "../compat11/any.hpp"

namespace bgen11 {
namespace detail {

struct VariantEncodeVisitor {
    io::BitWriter* w;
    VoidResult result;
    VariantEncodeVisitor(io::BitWriter& writer) : w(&writer) {}

    template <typename T>
    void operator()(const T& m) {
        result = m.encode(*w);
    }
};

template <typename Variant>
VoidResult variant_encode(const Variant& v, io::BitWriter& w) {
    VariantEncodeVisitor vis(w);
    cpp11::visit(vis, v);
    return vis.result;
}

struct VariantToStringVisitor {
    std::string result;

    template <typename T>
    void operator()(const T& m) {
        result = m.to_string();
    }
};

template <typename Variant>
std::string variant_to_string(const Variant& v) {
    VariantToStringVisitor vis;
    cpp11::visit(vis, v);
    return vis.result;
}

template <typename Overrides>
struct VariantToStringWithVisitor {
    const Overrides* overrides;
    std::string result;

    template <typename T>
    void operator()(const T& m) {
        result = m.to_string(*overrides);
    }
};

template <typename Variant, typename Overrides>
std::string variant_to_string_with(const Variant& v, const Overrides& overrides) {
    VariantToStringWithVisitor<Overrides> vis;
    vis.overrides = &overrides;
    cpp11::visit(vis, v);
    return vis.result;
}

struct VariantDecodeAppendVisitor {
    std::vector<traits::DecodedMessage>* messages;
    const std::vector<uint8_t>* raw;

    VariantDecodeAppendVisitor(std::vector<traits::DecodedMessage>& m,
                                const std::vector<uint8_t>& r)
        : messages(&m), raw(&r) {}

    template <typename T>
    void operator()(const T& m) {
        traits::DecodedMessage dm;
        dm.type_id = T::TYPE_ID;
        dm.type_name = std::string(T::TYPE_NAME);
        dm.payload = cpp11::any(m);
        dm.raw = *raw;
        messages->push_back(std::move(dm));
    }
};

template <typename Variant>
void variant_decode_append(const Variant& v,
                            std::vector<traits::DecodedMessage>& messages,
                            const std::vector<uint8_t>& raw) {
    VariantDecodeAppendVisitor vis(messages, raw);
    cpp11::visit(vis, v);
}

}
}

#endif
