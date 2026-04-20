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

// ---------------------------------------------------------------------------
// JSON field dispatch: for each alternative in a variant, delegate to a
// user-supplied ADL-found `to_json(JsonT&, const T&)` and assign the
// result to `j[key]`.
//
// Templated on the JSON type so the helper does not pull in nlohmann
// headers.  Instantiated at call sites in the translated json.hpp.
// ---------------------------------------------------------------------------

template <typename JsonT>
struct VariantToJsonFieldVisitor {
    JsonT* j;
    const char* key;

    template <typename T>
    void operator()(const T& inner) {
        JsonT vj;
        using std::move;
        to_json(vj, inner);
        (*j)[key] = std::move(vj);
    }
};

template <typename JsonT, typename Variant>
void variant_to_json_field(JsonT& j, const char* key, const Variant& v) {
    VariantToJsonFieldVisitor<JsonT> vis;
    vis.j = &j;
    vis.key = key;
    cpp11::visit(vis, v);
}

// ---------------------------------------------------------------------------
// JSON helpers: assign-to-fixed-size-array OR move-assign.
//
// bgen's json.hpp emits:
//   if constexpr (requires { dest_.size(); dest_.begin();
//                            std::tuple_size<std::decay_t<decltype(dest_)>>::value; })
//     { std::copy_n(arr_.begin(), std::min(arr_.size(), dest_.size()), dest_.begin()); }
//   else { dest_ = std::move(arr_); }
//
// The C++23 `if constexpr requires` path is replaced by this SFINAE
// overload set in C++11.  The first overload is selected when
// `std::tuple_size<T>::value` is well-formed (i.e. T is a std::array or
// tuple-like type with a known compile-time size), otherwise the
// fallback overload runs the move-assignment.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <tuple>

template <typename Dest, typename Src>
auto assign_fixed_or_move_impl(Dest& dest, Src&& src, int)
    -> decltype((void)std::tuple_size<typename std::decay<Dest>::type>::value) {
    typedef typename std::decay<Src>::type SrcDecay;
    SrcDecay tmp(std::forward<Src>(src));
    std::size_t n = tmp.size() < dest.size() ? tmp.size() : dest.size();
    std::copy_n(tmp.begin(), n, dest.begin());
}

template <typename Dest, typename Src>
void assign_fixed_or_move_impl(Dest& dest, Src&& src, long) {
    dest = std::forward<Src>(src);
}

template <typename Dest, typename Src>
void assign_fixed_or_move(Dest& dest, Src&& src) {
    assign_fixed_or_move_impl(dest, std::forward<Src>(src), 0);
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
