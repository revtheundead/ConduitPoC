#include "../include/compat11/optional.hpp"
#include <string>

extern int total;
extern int failed;
#define TINY_CHECK(expr) do { \
    ++total; \
    if (!(expr)) { ++failed; } \
} while (0)

void run_optional_tests() {
    cpp11::optional<int> a;
    TINY_CHECK(!a.has_value());

    cpp11::optional<int> b(42);
    TINY_CHECK(b.has_value());
    TINY_CHECK(*b == 42);

    cpp11::optional<std::string> s(std::string("hello"));
    TINY_CHECK(s->size() == 5);

    a = 10;
    TINY_CHECK(a.has_value());
    TINY_CHECK(*a == 10);

    a.reset();
    TINY_CHECK(!a.has_value());

    TINY_CHECK(a.value_or(99) == 99);
}
