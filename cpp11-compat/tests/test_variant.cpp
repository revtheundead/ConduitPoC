#include "../include/compat11/variant.hpp"
#include "../include/compat11/visit.hpp"
#include <string>

extern int total;
extern int failed;
#define TINY_CHECK(expr) do { \
    ++total; \
    if (!(expr)) { ++failed; } \
} while (0)

namespace {
struct Visitor {
    int result;
    Visitor() : result(0) {}
    void operator()(int x) { result = x; }
    void operator()(const std::string& s) { result = int(s.size()); }
};
}

void run_variant_tests() {
    cpp11::variant<int, std::string> v(42);
    TINY_CHECK(v.index() == 0);
    TINY_CHECK(cpp11::get<int>(v) == 42);

    v = std::string("hello");
    TINY_CHECK(v.index() == 1);
    TINY_CHECK(cpp11::get<std::string>(v) == "hello");

    TINY_CHECK(cpp11::holds_alternative<std::string>(v));
    TINY_CHECK(!cpp11::holds_alternative<int>(v));

    Visitor viz;
    cpp11::visit(viz, v);
    TINY_CHECK(viz.result == 5);
}
