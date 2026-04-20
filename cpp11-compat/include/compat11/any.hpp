#ifndef COMPAT11_ANY_HPP
#define COMPAT11_ANY_HPP

#include <typeinfo>
#include <type_traits>
#include <stdexcept>
#include <utility>

namespace cpp11 {

class bad_any_cast : public std::bad_cast {
public:
    const char* what() const throw() { return "bad_any_cast"; }
};

class any {
public:
    any() : holder_(0) {}
    any(const any& o) : holder_(o.holder_ ? o.holder_->clone() : 0) {}
    any(any&& o) : holder_(o.holder_) { o.holder_ = 0; }

    template <typename T>
    any(T&& v, typename std::enable_if<
            !std::is_same<typename std::decay<T>::type, any>::value, int>::type = 0)
        : holder_(new holder<typename std::decay<T>::type>(std::forward<T>(v))) {}

    ~any() { delete holder_; }

    any& operator=(const any& o) {
        if (this == &o) return *this;
        delete holder_;
        holder_ = o.holder_ ? o.holder_->clone() : 0;
        return *this;
    }
    any& operator=(any&& o) {
        delete holder_;
        holder_ = o.holder_;
        o.holder_ = 0;
        return *this;
    }
    template <typename T>
    typename std::enable_if<
        !std::is_same<typename std::decay<T>::type, any>::value, any&>::type
    operator=(T&& v) {
        delete holder_;
        holder_ = new holder<typename std::decay<T>::type>(std::forward<T>(v));
        return *this;
    }

    bool has_value() const { return holder_ != 0; }
    void reset() { delete holder_; holder_ = 0; }
    const std::type_info& type() const {
        return holder_ ? holder_->type() : typeid(void);
    }

    struct holder_base {
        virtual ~holder_base() {}
        virtual holder_base* clone() const = 0;
        virtual const std::type_info& type() const = 0;
    };
    template <typename T>
    struct holder : holder_base {
        T value;
        holder(const T& v) : value(v) {}
        holder(T&& v) : value(std::move(v)) {}
        holder_base* clone() const { return new holder<T>(value); }
        const std::type_info& type() const { return typeid(T); }
    };

    holder_base* holder_;
};

template <typename T>
T* any_cast(any* a) {
    if (!a || !a->holder_) return 0;
    if (a->holder_->type() != typeid(T)) return 0;
    return &static_cast<any::holder<T>*>(a->holder_)->value;
}

template <typename T>
const T* any_cast(const any* a) {
    if (!a || !a->holder_) return 0;
    if (a->holder_->type() != typeid(T)) return 0;
    return &static_cast<const any::holder<T>*>(a->holder_)->value;
}

template <typename T>
T any_cast(const any& a) {
    typedef typename std::remove_reference<T>::type U;
    const U* p = any_cast<typename std::remove_cv<U>::type>(&a);
    if (!p) throw bad_any_cast();
    return *p;
}

template <typename T>
T any_cast(any& a) {
    typedef typename std::remove_reference<T>::type U;
    U* p = any_cast<typename std::remove_cv<U>::type>(&a);
    if (!p) throw bad_any_cast();
    return *p;
}

}

#endif
