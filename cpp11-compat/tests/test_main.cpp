#include <cstdio>

int total = 0;
int failed = 0;

#define TINY_CHECK(expr) do { \
    ++total; \
    if (!(expr)) { ++failed; std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #expr); } \
} while (0)

void run_span_tests();
void run_optional_tests();
void run_variant_tests();
void run_any_tests();
void run_expected_tests();
void run_string_view_tests();
void run_bit_reader_tests();
void run_bit_writer_tests();

int main() {
    run_span_tests();
    run_optional_tests();
    run_variant_tests();
    run_any_tests();
    run_expected_tests();
    run_string_view_tests();
    run_bit_reader_tests();
    run_bit_writer_tests();
    std::printf("cpp11-compat: %d/%d tests passed\n", total - failed, total);
    return failed == 0 ? 0 : 1;
}
