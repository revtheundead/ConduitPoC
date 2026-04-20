#include "../include/compat11/expected.hpp"
#include <string>

extern int total;
extern int failed;
#define TINY_CHECK(expr) do { \
    ++total; \
    if (!(expr)) { ++failed; } \
} while (0)

void run_expected_tests() {
    cpp11::expected<int, std::string> ok(42);
    TINY_CHECK(ok.has_value());
    TINY_CHECK(*ok == 42);

    cpp11::expected<int, std::string> err =
        cpp11::make_unexpected(std::string("boom"));
    TINY_CHECK(!err.has_value());
    TINY_CHECK(err.error() == "boom");

    cpp11::expected<void, std::string> vo;
    TINY_CHECK(vo.has_value());
    cpp11::expected<void, std::string> ve =
        cpp11::make_unexpected(std::string("bad"));
    TINY_CHECK(!ve.has_value());
}
