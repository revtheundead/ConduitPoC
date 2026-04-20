#ifndef COMPAT11_MEMORY_HPP
#define COMPAT11_MEMORY_HPP

#include <memory>
#include <utility>

namespace cpp11 {

template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

}

#endif
