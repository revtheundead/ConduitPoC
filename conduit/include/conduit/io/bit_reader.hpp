// SPDX-License-Identifier: MIT
// Conduit - Bit-level Reader

#pragma once

#include <conduit/core/error.hpp>
#include <conduit/io/endian.hpp>
#include <cstdint>
#include <span>
#include <string>

namespace conduit::io {

// ============================================================================
// BitReader: Protocol-generic bit-level parser
//
// Redesigned from SENTRIX BitReader:
// - Returns Result<T> instead of std::optional
// - Endianness parameter on multi-byte reads
// - sub_reader() for bounded-scope parsing
// - No ASTERIX-specific helpers
// ============================================================================

class BitReader {
public:
    explicit BitReader(std::span<const uint8_t> data) noexcept
        : data_(data), byte_pos_(0), bit_pos_(0) {}

    // ========================================================================
    // Bit-level reading
    // ========================================================================

    [[nodiscard]] Result<uint64_t> read_bits(size_t count);
    [[nodiscard]] Result<int64_t> read_signed_bits(size_t count);

    // ========================================================================
    // Aviation wire encoding reads
    // ========================================================================

    /// BCD: Read N bits as unsigned BCD (N must be multiple of 4)
    [[nodiscard]] Result<uint64_t> read_bcd(size_t bits);

    /// BCD_S: Read N bits as signed BCD (MSB = sign, rest = BCD nibbles)
    [[nodiscard]] Result<int64_t> read_bcd_signed(size_t bits);

    /// BNR_S: Read N bits as sign-magnitude (MSB = sign, rest = magnitude)
    [[nodiscard]] Result<int64_t> read_sign_magnitude(size_t bits);

    // ========================================================================
    // Byte-level reading (with endianness)
    // ========================================================================

    [[nodiscard]] Result<uint8_t> read_u8();
    [[nodiscard]] Result<uint16_t> read_u16(Endian e = Endian::Big);
    [[nodiscard]] Result<uint32_t> read_u32(Endian e = Endian::Big);
    [[nodiscard]] Result<uint64_t> read_u64(Endian e = Endian::Big);
    [[nodiscard]] Result<float> read_f16(Endian e = Endian::Big);
    [[nodiscard]] Result<float> read_f32(Endian e = Endian::Big);
    [[nodiscard]] Result<double> read_f64(Endian e = Endian::Big);

    // ========================================================================
    // Bulk reading
    // ========================================================================

    [[nodiscard]] Result<std::span<const uint8_t>> read_bytes(size_t count);
    [[nodiscard]] Result<std::string> read_string(size_t byte_length);

    // ========================================================================
    // Sub-reader: creates a new BitReader bounded to the next N bytes
    // Advances this reader past those bytes.
    // ========================================================================

    [[nodiscard]] Result<BitReader> sub_reader(size_t byte_count);

    // ========================================================================
    // Position and state
    // ========================================================================

    [[nodiscard]] size_t remaining_bytes() const noexcept;
    [[nodiscard]] size_t remaining_bits() const noexcept;
    [[nodiscard]] size_t bit_position() const noexcept;
    [[nodiscard]] bool at_end() const noexcept;

    [[nodiscard]] VoidResult skip_bits(size_t count);
    void align_to_byte() noexcept;
    void align_to(size_t byte_boundary) noexcept;

    [[nodiscard]] bool is_byte_aligned() const noexcept {
        return bit_pos_ == 0;
    }

    void reset() noexcept {
        byte_pos_ = 0;
        bit_pos_ = 0;
    }

    [[nodiscard]] std::span<const uint8_t> underlying_data() const noexcept {
        return data_;
    }

private:
    void advance(size_t bits) noexcept {
        bit_pos_ += bits;
        byte_pos_ += bit_pos_ / 8;
        bit_pos_ %= 8;
    }

    std::span<const uint8_t> data_;
    size_t byte_pos_;
    size_t bit_pos_;  // 0-7, bit within current byte (0 = MSB)
};

} // namespace conduit::io
