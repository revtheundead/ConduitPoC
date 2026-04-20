#include "../include/compat11/string_view.hpp"
#include <string>

extern int total;
extern int failed;
#define TINY_CHECK(expr) do { \
    ++total; \
    if (!(expr)) { ++failed; } \
} while (0)

void run_string_view_tests() {
    cpp11::string_view sv("hello");
    TINY_CHECK(sv.size() == 5);
    TINY_CHECK(sv[0] == 'h');

    std::string s = "world";
    cpp11::string_view sv2(s);
    TINY_CHECK(sv2 == cpp11::string_view("world"));

    cpp11::string_view sub = sv.substr(1, 3);
    TINY_CHECK(sub == cpp11::string_view("ell"));

    TINY_CHECK(sv < cpp11::string_view("hi"));
}
