#ifndef COMPAT11_OPTIONAL_HPP
#define COMPAT11_OPTIONAL_HPP

#include <new>
#include <type_traits>
#include <stdexcept>
#include <utility>

namespace cpp11 {

struct nullopt_t { struct _tag {}; explicit nullopt_t(_tag) {} };
static const nullopt_t nullopt((nullopt_t::_tag()));

struct in_place_t {};
static const in_place_t in_place;

class bad_optional_access : public std::exception {
public:
    const char* what() const throw() { return "bad_optional_access"; }
};

template <typename T>
class optional {
public:
    typedef T value_type;

    optional() : engaged_(false) {}
    optional(nullopt_t) : engaged_(false) {}

    optional(const T& v) : engaged_(true) {
        new (storage()) T(v);
    }
    optional(T&& v) : engaged_(true) {
        new (storage()) T(std::move(v));
    }

    template <typename... Args>
    optional(in_place_t, Args&&... args) : engaged_(true) {
        new (storage()) T(std::forward<Args>(args)...);
    }

    optional(const optional& o) : engaged_(false) {
        if (o.engaged_) { new (storage()) T(*o.ptr()); engaged_ = true; }
    }
    optional(optional&& o) : engaged_(false) {
        if (o.engaged_) { new (storage()) T(std::move(*o.ptr())); engaged_ = true; }
    }

    ~optional() { reset(); }

    optional& operator=(nullopt_t) { reset(); return *this; }
    optional& operator=(const optional& o) {
        if (this == &o) return *this;
        if (engaged_ && o.engaged_) { *ptr() = *o.ptr(); }
        else if (engaged_) { reset(); }
        else if (o.engaged_) { new (storage()) T(*o.ptr()); engaged_ = true; }
        return *this;
    }
    optional& operator=(optional&& o) {
        if (engaged_ && o.engaged_) { *ptr() = std::move(*o.ptr()); }
        else if (engaged_) { reset(); }
        else if (o.engaged_) { new (storage()) T(std::move(*o.ptr())); engaged_ = true; }
        return *this;
    }
    template <typename U>
    optional& operator=(U&& v) {
        if (engaged_) { *ptr() = std::forward<U>(v); }
        else { new (storage()) T(std::forward<U>(v)); engaged_ = true; }
        return *this;
    }

    bool has_value() const { return engaged_; }
    explicit operator bool() const { return engaged_; }

    T& value() {
        if (!engaged_) throw bad_optional_access();
        return *ptr();
    }
    const T& value() const {
        if (!engaged_) throw bad_optional_access();
        return *ptr();
    }
    template <typename U>
    T value_or(U&& def) const {
        return engaged_ ? *ptr() : static_cast<T>(std::forward<U>(def));
    }

    T& operator*() { return *ptr(); }
    const T& operator*() const { return *ptr(); }
    T* operator->() { return ptr(); }
    const T* operator->() const { return ptr(); }

    void reset() {
        if (engaged_) { ptr()->~T(); engaged_ = false; }
    }

    template <typename... Args>
    T& emplace(Args&&... args) {
        reset();
        new (storage()) T(std::forward<Args>(args)...);
        engaged_ = true;
        return *ptr();
    }

private:
    typename std::aligned_storage<sizeof(T), alignof(T)>::type storage_;
    bool engaged_;

    void* storage() { return static_cast<void*>(&storage_); }
    T* ptr() { return static_cast<T*>(storage()); }
    const T* ptr() const { return reinterpret_cast<const T*>(&storage_); }
};

template <typename T>
bool operator==(const optional<T>& a, const optional<T>& b) {
    if (a.has_value() != b.has_value()) return false;
    if (!a.has_value()) return true;
    return *a == *b;
}
template <typename T>
bool operator!=(const optional<T>& a, const optional<T>& b) { return !(a == b); }
template <typename T>
bool operator==(const optional<T>& a, nullopt_t) { return !a.has_value(); }
template <typename T>
bool operator==(nullopt_t, const optional<T>& a) { return !a.has_value(); }
template <typename T>
bool operator!=(const optional<T>& a, nullopt_t) { return a.has_value(); }
template <typename T>
bool operator!=(nullopt_t, const optional<T>& a) { return a.has_value(); }

}

#endif
