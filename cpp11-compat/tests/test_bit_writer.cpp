#include "../include/bgen11/bit_writer.hpp"
#include "../include/bgen11/bit_reader.hpp"

extern int total;
extern int failed;
#define TINY_CHECK(expr) do { \
    ++total; \
    if (!(expr)) { ++failed; } \
} while (0)

void run_bit_writer_tests() {
    bgen11::io::BitWriter w;
    w.write_u8(0xAB);
    w.write_u16(0x1234, bgen11::io::Endian_Big);
    w.write_u32(0xDEADBEEFu, bgen11::io::Endian_Big);

    std::vector<uint8_t> out = w.take();
    TINY_CHECK(out.size() == 7);
    TINY_CHECK(out[0] == 0xAB);
    TINY_CHECK(out[1] == 0x12);
    TINY_CHECK(out[2] == 0x34);
    TINY_CHECK(out[3] == 0xDE);

    bgen11::io::BitReader r(cpp11::span<const uint8_t>(out.data(), out.size()));
    bgen11::Result<uint8_t> b = r.read_u8();
    TINY_CHECK(b.has_value() && *b == 0xAB);
}
