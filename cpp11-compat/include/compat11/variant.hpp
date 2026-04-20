#ifndef COMPAT11_VARIANT_HPP
#define COMPAT11_VARIANT_HPP

#include <new>
#include <type_traits>
#include <stdexcept>
#include <utility>
#include <cstddef>

namespace cpp11 {

class bad_variant_access : public std::exception {
public:
    const char* what() const throw() { return "bad_variant_access"; }
};

namespace detail {

template <std::size_t I, typename... Ts> struct type_at;
template <typename H, typename... R> struct type_at<0, H, R...> { typedef H type; };
template <std::size_t I, typename H, typename... R> struct type_at<I, H, R...> {
    typedef typename type_at<I-1, R...>::type type;
};

template <typename T, typename... Ts> struct index_of;
template <typename T, typename H, typename... R>
struct index_of<T, H, R...> {
    static const std::size_t value = std::is_same<T, H>::value
        ? 0 : 1 + index_of<T, R...>::value;
};
template <typename T> struct index_of<T> { static const std::size_t value = 0; };

template <typename T, typename... Ts> struct contains;
template <typename T> struct contains<T> { static const bool value = false; };
template <typename T, typename H, typename... R>
struct contains<T, H, R...> {
    static const bool value = std::is_same<T, H>::value || contains<T, R...>::value;
};

template <typename... Ts> struct max_size;
template <> struct max_size<> {
    static const std::size_t value = 1;
    static const std::size_t align = 1;
};
template <typename H, typename... R> struct max_size<H, R...> {
    static const std::size_t rv = max_size<R...>::value;
    static const std::size_t ra = max_size<R...>::align;
    static const std::size_t value = sizeof(H) > rv ? sizeof(H) : rv;
    static const std::size_t align = alignof(H) > ra ? alignof(H) : ra;
};

template <std::size_t I, typename... Ts>
struct helper {
    typedef typename type_at<I-1, Ts...>::type T;
    static void destroy(std::size_t idx, void* p) {
        if (idx == I-1) static_cast<T*>(p)->~T();
        else helper<I-1, Ts...>::destroy(idx, p);
    }
    static void copy(std::size_t idx, const void* src, void* dst) {
        if (idx == I-1) new (dst) T(*static_cast<const T*>(src));
        else helper<I-1, Ts...>::copy(idx, src, dst);
    }
    static void move(std::size_t idx, void* src, void* dst) {
        if (idx == I-1) new (dst) T(std::move(*static_cast<T*>(src)));
        else helper<I-1, Ts...>::move(idx, src, dst);
    }
    static bool eq(std::size_t idx, const void* a, const void* b) {
        if (idx == I-1) return *static_cast<const T*>(a) == *static_cast<const T*>(b);
        return helper<I-1, Ts...>::eq(idx, a, b);
    }
};
template <typename... Ts>
struct helper<0, Ts...> {
    static void destroy(std::size_t, void*) {}
    static void copy(std::size_t, const void*, void*) {}
    static void move(std::size_t, void*, void*) {}
    static bool eq(std::size_t, const void*, const void*) { return false; }
};

}

template <typename... Ts>
class variant {
public:
    static const std::size_t npos = static_cast<std::size_t>(-1);

    variant() : index_(0) {
        typedef typename detail::type_at<0, Ts...>::type T0;
        new (storage()) T0();
    }

    template <typename T,
              typename = typename std::enable_if<detail::contains<typename std::decay<T>::type, Ts...>::value>::type>
    variant(T&& v) : index_(detail::index_of<typename std::decay<T>::type, Ts...>::value) {
        typedef typename std::decay<T>::type D;
        new (storage()) D(std::forward<T>(v));
    }

    variant(const variant& o) : index_(o.index_) {
        detail::helper<sizeof...(Ts), Ts...>::copy(index_, o.storage(), storage());
    }
    variant(variant&& o) : index_(o.index_) {
        detail::helper<sizeof...(Ts), Ts...>::move(index_, o.storage(), storage());
    }

    ~variant() {
        detail::helper<sizeof...(Ts), Ts...>::destroy(index_, storage());
    }

    variant& operator=(const variant& o) {
        if (this == &o) return *this;
        detail::helper<sizeof...(Ts), Ts...>::destroy(index_, storage());
        index_ = o.index_;
        detail::helper<sizeof...(Ts), Ts...>::copy(index_, o.storage(), storage());
        return *this;
    }
    variant& operator=(variant&& o) {
        detail::helper<sizeof...(Ts), Ts...>::destroy(index_, storage());
        index_ = o.index_;
        detail::helper<sizeof...(Ts), Ts...>::move(index_, o.storage(), storage());
        return *this;
    }
    template <typename T>
    typename std::enable_if<detail::contains<typename std::decay<T>::type, Ts...>::value, variant&>::type
    operator=(T&& v) {
        typedef typename std::decay<T>::type D;
        detail::helper<sizeof...(Ts), Ts...>::destroy(index_, storage());
        index_ = detail::index_of<D, Ts...>::value;
        new (storage()) D(std::forward<T>(v));
        return *this;
    }

    std::size_t index() const { return index_; }

    template <typename T>
    T& get() {
        if (detail::index_of<T, Ts...>::value != index_) throw bad_variant_access();
        return *static_cast<T*>(storage());
    }
    template <typename T>
    const T& get() const {
        if (detail::index_of<T, Ts...>::value != index_) throw bad_variant_access();
        return *static_cast<const T*>(storage());
    }

    void* storage() { return static_cast<void*>(&storage_); }
    const void* storage() const { return static_cast<const void*>(&storage_); }

    bool operator==(const variant& o) const {
        if (index_ != o.index_) return false;
        return detail::helper<sizeof...(Ts), Ts...>::eq(index_, storage(), o.storage());
    }
    bool operator!=(const variant& o) const { return !(*this == o); }

private:
    typename std::aligned_storage<
        detail::max_size<Ts...>::value,
        detail::max_size<Ts...>::align>::type storage_;
    std::size_t index_;
};

template <typename T, typename... Ts>
bool holds_alternative(const variant<Ts...>& v) {
    return detail::index_of<T, Ts...>::value == v.index();
}

template <typename T, typename... Ts>
T& get(variant<Ts...>& v) { return v.template get<T>(); }

template <typename T, typename... Ts>
const T& get(const variant<Ts...>& v) { return v.template get<T>(); }

template <typename T, typename... Ts>
T* get_if(variant<Ts...>* v) {
    if (!v) return 0;
    if (detail::index_of<T, Ts...>::value != v->index()) return 0;
    return static_cast<T*>(v->storage());
}
template <typename T, typename... Ts>
const T* get_if(const variant<Ts...>* v) {
    if (!v) return 0;
    if (detail::index_of<T, Ts...>::value != v->index()) return 0;
    return static_cast<const T*>(v->storage());
}

}

#endif
