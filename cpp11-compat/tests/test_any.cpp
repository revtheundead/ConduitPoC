#include "../include/compat11/any.hpp"
#include <string>

extern int total;
extern int failed;
#define TINY_CHECK(expr) do { \
    ++total; \
    if (!(expr)) { ++failed; } \
} while (0)

void run_any_tests() {
    cpp11::any a(42);
    TINY_CHECK(a.has_value());
    int* p = cpp11::any_cast<int>(&a);
    TINY_CHECK(p != 0 && *p == 42);

    cpp11::any b(std::string("abc"));
    std::string* sp = cpp11::any_cast<std::string>(&b);
    TINY_CHECK(sp != 0 && *sp == "abc");

    cpp11::any c = a;
    TINY_CHECK(cpp11::any_cast<int>(c) == 42);

    cpp11::any empty;
    TINY_CHECK(!empty.has_value());
    TINY_CHECK(cpp11::any_cast<int>(&empty) == 0);
}
