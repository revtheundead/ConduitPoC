#ifndef COMPAT11_SPAN_HPP
#define COMPAT11_SPAN_HPP

#include <cstddef>
#include <array>
#include <vector>
#include <type_traits>
#include <stdexcept>

namespace cpp11 {

template <typename T>
class span {
public:
    typedef T element_type;
    typedef typename std::remove_cv<T>::type value_type;
    typedef std::size_t size_type;
    typedef T* pointer;
    typedef T& reference;
    typedef T* iterator;
    typedef const T* const_iterator;

    span() : data_(0), size_(0) {}
    span(T* data, std::size_t size) : data_(data), size_(size) {}
    span(T* first, T* last) : data_(first), size_(static_cast<std::size_t>(last - first)) {}

    template <std::size_t N>
    span(T (&arr)[N]) : data_(arr), size_(N) {}

    template <std::size_t N>
    span(std::array<typename std::remove_cv<T>::type, N>& arr)
        : data_(arr.data()), size_(N) {}

    template <std::size_t N>
    span(const std::array<typename std::remove_cv<T>::type, N>& arr)
        : data_(arr.data()), size_(N) {}

    template <typename U>
    span(std::vector<U>& v,
         typename std::enable_if<
             std::is_convertible<U*, T*>::value, int>::type = 0)
        : data_(v.empty() ? 0 : v.data()), size_(v.size()) {}

    template <typename U>
    span(const std::vector<U>& v,
         typename std::enable_if<
             std::is_convertible<const U*, T*>::value, int>::type = 0)
        : data_(v.empty() ? 0 : v.data()), size_(v.size()) {}

    template <typename U>
    span(const span<U>& other,
         typename std::enable_if<
             std::is_convertible<U*, T*>::value, int>::type = 0)
        : data_(other.data()), size_(other.size()) {}

    T* data() const { return data_; }
    std::size_t size() const { return size_; }
    std::size_t size_bytes() const { return size_ * sizeof(T); }
    bool empty() const { return size_ == 0; }

    T& operator[](std::size_t i) const { return data_[i]; }
    T& front() const { return data_[0]; }
    T& back() const { return data_[size_ - 1]; }

    iterator begin() const { return data_; }
    iterator end() const { return data_ + size_; }

    span<T> subspan(std::size_t offset) const {
        return span<T>(data_ + offset, size_ - offset);
    }
    span<T> subspan(std::size_t offset, std::size_t count) const {
        return span<T>(data_ + offset, count);
    }
    span<T> first(std::size_t count) const { return span<T>(data_, count); }
    span<T> last(std::size_t count) const { return span<T>(data_ + size_ - count, count); }

private:
    T* data_;
    std::size_t size_;
};

}

#endif
