#ifndef COMPAT11_EXPECTED_HPP
#define COMPAT11_EXPECTED_HPP

#include <new>
#include <type_traits>
#include <stdexcept>
#include <utility>

namespace cpp11 {

template <typename E>
class unexpected {
public:
    unexpected(const E& e) : err_(e) {}
    unexpected(E&& e) : err_(std::move(e)) {}
    const E& value() const { return err_; }
    E& value() { return err_; }
private:
    E err_;
};

template <typename E>
unexpected<typename std::decay<E>::type> make_unexpected(E&& e) {
    return unexpected<typename std::decay<E>::type>(std::forward<E>(e));
}

struct unexpect_t {};
static const unexpect_t unexpect;

class bad_expected_access : public std::exception {
public:
    const char* what() const throw() { return "bad_expected_access"; }
};

template <typename T, typename E>
class expected {
public:
    typedef T value_type;
    typedef E error_type;

    expected() : has_value_(true) { new (val_ptr()) T(); }

    expected(const T& v) : has_value_(true) { new (val_ptr()) T(v); }
    expected(T&& v) : has_value_(true) { new (val_ptr()) T(std::move(v)); }

    expected(const unexpected<E>& u) : has_value_(false) {
        new (err_ptr()) E(u.value());
    }
    expected(unexpected<E>&& u) : has_value_(false) {
        new (err_ptr()) E(std::move(u.value()));
    }

    expected(const expected& o) : has_value_(o.has_value_) {
        if (has_value_) new (val_ptr()) T(*o.val_ptr());
        else new (err_ptr()) E(*o.err_ptr());
    }
    expected(expected&& o) : has_value_(o.has_value_) {
        if (has_value_) new (val_ptr()) T(std::move(*o.val_ptr()));
        else new (err_ptr()) E(std::move(*o.err_ptr()));
    }

    ~expected() { destroy(); }

    expected& operator=(const expected& o) {
        if (this == &o) return *this;
        destroy();
        has_value_ = o.has_value_;
        if (has_value_) new (val_ptr()) T(*o.val_ptr());
        else new (err_ptr()) E(*o.err_ptr());
        return *this;
    }
    expected& operator=(expected&& o) {
        destroy();
        has_value_ = o.has_value_;
        if (has_value_) new (val_ptr()) T(std::move(*o.val_ptr()));
        else new (err_ptr()) E(std::move(*o.err_ptr()));
        return *this;
    }

    bool has_value() const { return has_value_; }
    explicit operator bool() const { return has_value_; }

    T& value() {
        if (!has_value_) throw bad_expected_access();
        return *val_ptr();
    }
    const T& value() const {
        if (!has_value_) throw bad_expected_access();
        return *val_ptr();
    }
    E& error() { return *err_ptr(); }
    const E& error() const { return *err_ptr(); }

    T& operator*() { return *val_ptr(); }
    const T& operator*() const { return *val_ptr(); }
    T* operator->() { return val_ptr(); }
    const T* operator->() const { return val_ptr(); }

private:
    union {
        typename std::aligned_storage<sizeof(T), alignof(T)>::type v_;
        typename std::aligned_storage<sizeof(E), alignof(E)>::type e_;
    };
    bool has_value_;

    T* val_ptr() { return reinterpret_cast<T*>(&v_); }
    const T* val_ptr() const { return reinterpret_cast<const T*>(&v_); }
    E* err_ptr() { return reinterpret_cast<E*>(&e_); }
    const E* err_ptr() const { return reinterpret_cast<const E*>(&e_); }

    void destroy() {
        if (has_value_) val_ptr()->~T();
        else err_ptr()->~E();
    }
};

template <typename E>
class expected<void, E> {
public:
    expected() : has_value_(true) {}
    expected(const unexpected<E>& u) : has_value_(false) {
        new (err_ptr()) E(u.value());
    }
    expected(unexpected<E>&& u) : has_value_(false) {
        new (err_ptr()) E(std::move(u.value()));
    }
    expected(const expected& o) : has_value_(o.has_value_) {
        if (!has_value_) new (err_ptr()) E(*o.err_ptr());
    }
    expected(expected&& o) : has_value_(o.has_value_) {
        if (!has_value_) new (err_ptr()) E(std::move(*o.err_ptr()));
    }
    ~expected() { if (!has_value_) err_ptr()->~E(); }

    expected& operator=(const expected& o) {
        if (this == &o) return *this;
        if (!has_value_) err_ptr()->~E();
        has_value_ = o.has_value_;
        if (!has_value_) new (err_ptr()) E(*o.err_ptr());
        return *this;
    }
    expected& operator=(expected&& o) {
        if (!has_value_) err_ptr()->~E();
        has_value_ = o.has_value_;
        if (!has_value_) new (err_ptr()) E(std::move(*o.err_ptr()));
        return *this;
    }

    bool has_value() const { return has_value_; }
    explicit operator bool() const { return has_value_; }
    E& error() { return *err_ptr(); }
    const E& error() const { return *err_ptr(); }

private:
    typename std::aligned_storage<sizeof(E), alignof(E)>::type e_;
    bool has_value_;
    E* err_ptr() { return reinterpret_cast<E*>(&e_); }
    const E* err_ptr() const { return reinterpret_cast<const E*>(&e_); }
};

}

#endif
