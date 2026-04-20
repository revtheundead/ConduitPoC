#include "../include/bgen11/bit_reader.hpp"

extern int total;
extern int failed;
#define TINY_CHECK(expr) do { \
    ++total; \
    if (!(expr)) { ++failed; } \
} while (0)

void run_bit_reader_tests() {
    uint8_t buf[] = {0x12, 0x34, 0x56, 0x78};
    bgen11::io::BitReader r(cpp11::span<const uint8_t>(buf, 4));

    bgen11::Result<uint8_t> b = r.read_u8();
    TINY_CHECK(b.has_value() && *b == 0x12);

    bgen11::Result<uint16_t> w = r.read_u16(bgen11::io::Endian_Big);
    TINY_CHECK(w.has_value() && *w == 0x3456);

    bgen11::io::BitReader r2(cpp11::span<const uint8_t>(buf, 4));
    bgen11::Result<uint64_t> bits = r2.read_bits(4);
    TINY_CHECK(bits.has_value() && *bits == 0x1);
    bgen11::Result<uint64_t> more = r2.read_bits(12);
    TINY_CHECK(more.has_value() && *more == 0x234);
}
