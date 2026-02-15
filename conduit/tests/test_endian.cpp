// SPDX-License-Identifier: MIT
// Conduit - Endian Utility Tests

#include <catch2/catch_test_macros.hpp>
#include <conduit/io/endian.hpp>
#include <array>
#include <cstdint>
#include <vector>

using namespace conduit::io;

// ============================================================================
// Byte swap
// ============================================================================

TEST_CASE("endian: byte_swap u16", "[endian]") {
    CHECK(byte_swap(uint16_t{0x0102}) == 0x0201);
    CHECK(byte_swap(uint16_t{0}) == 0);
    CHECK(byte_swap(uint16_t{0xFFFF}) == 0xFFFF);
}

TEST_CASE("endian: byte_swap u32", "[endian]") {
    CHECK(byte_swap(uint32_t{0x01020304}) == 0x04030201);
    CHECK(byte_swap(uint32_t{0}) == 0);
}

TEST_CASE("endian: byte_swap u64", "[endian]") {
    CHECK(byte_swap(uint64_t{0x0102030405060708ULL}) == 0x0807060504030201ULL);
    CHECK(byte_swap(uint64_t{0}) == 0);
}

// ============================================================================
// Read operations — normal
// ============================================================================

TEST_CASE("endian: read_u16 big-endian", "[endian]") {
    std::array<uint8_t, 4> data = {0x01, 0x02, 0x03, 0x04};
    CHECK(read_u16(data, 0, Endian::Big) == 0x0102);
    CHECK(read_u16(data, 2, Endian::Big) == 0x0304);
}

TEST_CASE("endian: read_u16 little-endian", "[endian]") {
    std::array<uint8_t, 4> data = {0x01, 0x02, 0x03, 0x04};
    CHECK(read_u16(data, 0, Endian::Little) == 0x0201);
    CHECK(read_u16(data, 2, Endian::Little) == 0x0403);
}

TEST_CASE("endian: read_u32 big-endian", "[endian]") {
    std::array<uint8_t, 4> data = {0xDE, 0xAD, 0xBE, 0xEF};
    CHECK(read_u32(data, 0, Endian::Big) == 0xDEADBEEF);
}

TEST_CASE("endian: read_u32 little-endian", "[endian]") {
    std::array<uint8_t, 4> data = {0xEF, 0xBE, 0xAD, 0xDE};
    CHECK(read_u32(data, 0, Endian::Little) == 0xDEADBEEF);
}

TEST_CASE("endian: read_u64 big-endian", "[endian]") {
    std::array<uint8_t, 8> data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    CHECK(read_u64(data, 0, Endian::Big) == 0x0102030405060708ULL);
}

TEST_CASE("endian: read_u64 little-endian", "[endian]") {
    std::array<uint8_t, 8> data = {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
    CHECK(read_u64(data, 0, Endian::Little) == 0x0102030405060708ULL);
}

// ============================================================================
// Read operations — out-of-bounds (B3: returns 0 instead of UB)
// ============================================================================

TEST_CASE("endian: read_u16 out-of-bounds returns 0", "[endian][bounds]") {
    std::array<uint8_t, 1> data = {0xFF};
    CHECK(read_u16(data, 0, Endian::Big) == 0);
    CHECK(read_u16(data, 1, Endian::Big) == 0);

    std::span<const uint8_t> empty;
    CHECK(read_u16(empty, 0, Endian::Big) == 0);
}

TEST_CASE("endian: read_u32 out-of-bounds returns 0", "[endian][bounds]") {
    std::array<uint8_t, 3> data = {0xFF, 0xFF, 0xFF};
    CHECK(read_u32(data, 0, Endian::Big) == 0);
    CHECK(read_u32(data, 2, Endian::Big) == 0);

    std::span<const uint8_t> empty;
    CHECK(read_u32(empty, 0, Endian::Big) == 0);
}

TEST_CASE("endian: read_u64 out-of-bounds returns 0", "[endian][bounds]") {
    std::array<uint8_t, 7> data = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    CHECK(read_u64(data, 0, Endian::Big) == 0);

    std::span<const uint8_t> empty;
    CHECK(read_u64(empty, 0, Endian::Big) == 0);
}

// ============================================================================
// Write operations — normal
// ============================================================================

TEST_CASE("endian: write_u16 big-endian", "[endian]") {
    std::array<uint8_t, 4> data = {};
    write_u16(data, 0, 0x0102, Endian::Big);
    CHECK(data[0] == 0x01);
    CHECK(data[1] == 0x02);
}

TEST_CASE("endian: write_u16 little-endian", "[endian]") {
    std::array<uint8_t, 4> data = {};
    write_u16(data, 0, 0x0102, Endian::Little);
    CHECK(data[0] == 0x02);
    CHECK(data[1] == 0x01);
}

TEST_CASE("endian: write_u32 big-endian", "[endian]") {
    std::array<uint8_t, 4> data = {};
    write_u32(data, 0, 0xDEADBEEF, Endian::Big);
    CHECK(data[0] == 0xDE);
    CHECK(data[1] == 0xAD);
    CHECK(data[2] == 0xBE);
    CHECK(data[3] == 0xEF);
}

TEST_CASE("endian: write_u64 big-endian", "[endian]") {
    std::array<uint8_t, 8> data = {};
    write_u64(data, 0, 0x0102030405060708ULL, Endian::Big);
    CHECK(data[0] == 0x01);
    CHECK(data[7] == 0x08);
}

// ============================================================================
// Write operations — out-of-bounds (B3: no-op instead of UB)
// ============================================================================

TEST_CASE("endian: write_u16 out-of-bounds is no-op", "[endian][bounds]") {
    std::array<uint8_t, 1> data = {0x42};
    write_u16(data, 0, 0xFFFF, Endian::Big);
    CHECK(data[0] == 0x42);  // Unchanged

    write_u16(data, 1, 0xFFFF, Endian::Big);
    CHECK(data[0] == 0x42);  // Still unchanged
}

TEST_CASE("endian: write_u32 out-of-bounds is no-op", "[endian][bounds]") {
    std::array<uint8_t, 3> data = {0x01, 0x02, 0x03};
    write_u32(data, 0, 0xFFFFFFFF, Endian::Big);
    CHECK(data[0] == 0x01);  // Unchanged
    CHECK(data[1] == 0x02);
    CHECK(data[2] == 0x03);
}

TEST_CASE("endian: write_u64 out-of-bounds is no-op", "[endian][bounds]") {
    std::array<uint8_t, 4> data = {0x01, 0x02, 0x03, 0x04};
    write_u64(data, 0, 0xFFFFFFFFFFFFFFFFULL, Endian::Big);
    CHECK(data[0] == 0x01);  // Unchanged
    CHECK(data[3] == 0x04);
}

// ============================================================================
// Float round-trips
// ============================================================================

TEST_CASE("endian: float round-trip", "[endian]") {
    std::array<uint8_t, 4> buf = {};
    write_f32(buf, 0, 3.14f, Endian::Big);
    float val = read_f32(buf, 0, Endian::Big);
    CHECK(val == 3.14f);
}

TEST_CASE("endian: double round-trip", "[endian]") {
    std::array<uint8_t, 8> buf = {};
    write_f64(buf, 0, 2.71828, Endian::Big);
    double val = read_f64(buf, 0, Endian::Big);
    CHECK(val == 2.71828);
}

// ============================================================================
// Write/Read round-trips with offset
// ============================================================================

TEST_CASE("endian: write then read with offset", "[endian]") {
    std::array<uint8_t, 8> buf = {};
    write_u16(buf, 2, 0xABCD, Endian::Big);
    CHECK(read_u16(buf, 2, Endian::Big) == 0xABCD);

    write_u32(buf, 4, 0x12345678, Endian::Little);
    CHECK(read_u32(buf, 4, Endian::Little) == 0x12345678);
}
