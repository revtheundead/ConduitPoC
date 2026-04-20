#include "../include/compat11/span.hpp"
#include <vector>

extern int total;
extern int failed;
#define TINY_CHECK(expr) do { \
    ++total; \
    if (!(expr)) { ++failed; } \
} while (0)

void run_span_tests() {
    int arr[5] = {1, 2, 3, 4, 5};
    cpp11::span<int> s(arr);
    TINY_CHECK(s.size() == 5);
    TINY_CHECK(s[0] == 1);
    TINY_CHECK(s[4] == 5);

    std::vector<int> v = {10, 20, 30};
    cpp11::span<const int> sv(v);
    TINY_CHECK(sv.size() == 3);
    TINY_CHECK(sv[1] == 20);

    cpp11::span<int> sub = s.subspan(1, 2);
    TINY_CHECK(sub.size() == 2);
    TINY_CHECK(sub[0] == 2);
}
