#ifndef COMPAT11_STRING_VIEW_HPP
#define COMPAT11_STRING_VIEW_HPP

#include <cstddef>
#include <cstring>
#include <string>
#include <ostream>
#include <stdexcept>

namespace cpp11 {

class string_view {
public:
    typedef const char* const_iterator;
    typedef const char* iterator;
    static const std::size_t npos = static_cast<std::size_t>(-1);

    string_view() : data_(""), size_(0) {}
    string_view(const char* s) : data_(s ? s : ""), size_(s ? std::strlen(s) : 0) {}
    string_view(const char* s, std::size_t n) : data_(s), size_(n) {}
    string_view(const std::string& s) : data_(s.data()), size_(s.size()) {}
    template <std::size_t N>
    string_view(const char (&arr)[N]) : data_(arr), size_(N > 0 && arr[N-1] == '\0' ? N - 1 : N) {}

    const char* data() const { return data_; }
    std::size_t size() const { return size_; }
    std::size_t length() const { return size_; }
    bool empty() const { return size_ == 0; }

    const char& operator[](std::size_t i) const { return data_[i]; }
    const char& at(std::size_t i) const {
        if (i >= size_) throw std::out_of_range("string_view::at");
        return data_[i];
    }
    const char& front() const { return data_[0]; }
    const char& back() const { return data_[size_ - 1]; }

    const_iterator begin() const { return data_; }
    const_iterator end() const { return data_ + size_; }
    const_iterator cbegin() const { return data_; }
    const_iterator cend() const { return data_ + size_; }

    void remove_prefix(std::size_t n) { data_ += n; size_ -= n; }
    void remove_suffix(std::size_t n) { size_ -= n; }

    string_view substr(std::size_t pos = 0, std::size_t count = npos) const {
        if (pos > size_) throw std::out_of_range("string_view::substr");
        std::size_t rcount = (count < size_ - pos) ? count : size_ - pos;
        return string_view(data_ + pos, rcount);
    }

    int compare(string_view other) const {
        std::size_t rlen = size_ < other.size_ ? size_ : other.size_;
        int r = std::memcmp(data_, other.data_, rlen);
        if (r != 0) return r;
        if (size_ < other.size_) return -1;
        if (size_ > other.size_) return 1;
        return 0;
    }

    std::string to_string() const { return std::string(data_, size_); }
    operator std::string() const { return std::string(data_, size_); }

private:
    const char* data_;
    std::size_t size_;
};

inline bool operator==(string_view a, string_view b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size()) == 0;
}
inline bool operator!=(string_view a, string_view b) { return !(a == b); }
inline bool operator<(string_view a, string_view b) { return a.compare(b) < 0; }
inline bool operator<=(string_view a, string_view b) { return a.compare(b) <= 0; }
inline bool operator>(string_view a, string_view b) { return a.compare(b) > 0; }
inline bool operator>=(string_view a, string_view b) { return a.compare(b) >= 0; }

inline std::ostream& operator<<(std::ostream& os, string_view sv) {
    return os.write(sv.data(), static_cast<std::streamsize>(sv.size()));
}

inline std::string operator+(const std::string& lhs, string_view rhs) {
    std::string r(lhs);
    r.append(rhs.data(), rhs.size());
    return r;
}

}

#endif
