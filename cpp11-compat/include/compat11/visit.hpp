#ifndef COMPAT11_VISIT_HPP
#define COMPAT11_VISIT_HPP

#include "variant.hpp"
#include <stdexcept>
#include <utility>

namespace cpp11 {

namespace detail {

template <std::size_t I, typename V, typename F, typename... Ts>
struct visitor_dispatch;

template <std::size_t I, typename... Ts, typename F>
struct visitor_dispatch<I, variant<Ts...>, F> {
    typedef typename type_at<I-1, Ts...>::type Alt;
    static auto call(F&& f, variant<Ts...>& v) -> decltype(f(std::declval<Alt&>())) {
        if (v.index() == I-1) return f(v.template get<Alt>());
        return visitor_dispatch<I-1, variant<Ts...>, F>::call(std::forward<F>(f), v);
    }
    static auto call(F&& f, const variant<Ts...>& v) -> decltype(f(std::declval<const Alt&>())) {
        if (v.index() == I-1) return f(v.template get<Alt>());
        return visitor_dispatch<I-1, variant<Ts...>, F>::call(std::forward<F>(f), v);
    }
};

template <typename... Ts, typename F>
struct visitor_dispatch<0, variant<Ts...>, F> {
    typedef typename type_at<0, Ts...>::type Alt;
    static auto call(F&&, variant<Ts...>&) -> decltype(std::declval<F>()(std::declval<Alt&>())) {
        throw bad_variant_access();
    }
    static auto call(F&&, const variant<Ts...>&) -> decltype(std::declval<F>()(std::declval<const Alt&>())) {
        throw bad_variant_access();
    }
};

}

template <typename F, typename... Ts>
auto visit(F&& f, variant<Ts...>& v)
    -> decltype(f(std::declval<typename detail::type_at<0, Ts...>::type&>())) {
    return detail::visitor_dispatch<sizeof...(Ts), variant<Ts...>, F>::call(std::forward<F>(f), v);
}

template <typename F, typename... Ts>
auto visit(F&& f, const variant<Ts...>& v)
    -> decltype(f(std::declval<const typename detail::type_at<0, Ts...>::type&>())) {
    return detail::visitor_dispatch<sizeof...(Ts), variant<Ts...>, F>::call(std::forward<F>(f), v);
}

}

#endif
