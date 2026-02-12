// SPDX-License-Identifier: MIT
// Conduit - Bit-level Writer

#pragma once

#include <conduit/core/error.hpp>
#include <conduit/io/endian.hpp>
#include <cassert>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace conduit::io {

// ============================================================================
// BitWriter: Protocol-generic bit-level encoder
//
// Redesigned from SENTRIX BitWriter:
// - Endianness parameter on multi-byte writes
// - Alignment to arbitrary byte boundaries
// - Patch support for writing values at earlier offsets
// - Dynamic buffer only (no external buffer mode)
// ============================================================================

class BitWriter {
public:
    explicit BitWriter(size_t initial_capacity = 256);

    // ========================================================================
    // Bit-level writing
    // ========================================================================

    void write_bits(uint64_t value, size_t count);
    void write_signed_bits(int64_t value, size_t count);

    // ========================================================================
    // Aviation wire encoding writes
    // ========================================================================

    /// BCD: Write unsigned value as BCD nibbles into N bits
    void write_bcd(uint64_t value, size_t bits);

    /// BCD_S: Write signed value as MSB-sign + BCD nibbles into N bits
    void write_bcd_signed(int64_t value, size_t bits);

    /// BNR_S: Write signed value as MSB-sign + magnitude into N bits
    void write_sign_magnitude(int64_t value, size_t bits);

    // ========================================================================
    // Byte-level writing (with endianness)
    // ========================================================================

    void write_u8(uint8_t value);
    void write_u16(uint16_t value, Endian e = Endian::Big);
    void write_u32(uint32_t value, Endian e = Endian::Big);
    void write_u64(uint64_t value, Endian e = Endian::Big);
    void write_f32(float value, Endian e = Endian::Big);
    void write_f64(double value, Endian e = Endian::Big);

    // ========================================================================
    // Bulk writing
    // ========================================================================

    void write_bytes(std::span<const uint8_t> data);

    // Writes exactly padded_length bytes. If str.size() > padded_length,
    // the string is truncated to padded_length bytes and false is returned.
    bool write_string(std::string_view str, size_t padded_length, char padding = '\0');

    // ========================================================================
    // Alignment and patching
    // ========================================================================

    void align_to_byte();
    void align_to(size_t byte_boundary);

    // Patch a previously written value at the given byte offset.
    // Writer must be byte-aligned and offset must be within already-written data.
    // Returns false if the byte offset is out of range.
    [[nodiscard]] bool patch_u8(size_t byte_offset, uint8_t value);
    [[nodiscard]] bool patch_u16(size_t byte_offset, uint16_t value, Endian e = Endian::Big);
    [[nodiscard]] bool patch_u32(size_t byte_offset, uint32_t value, Endian e = Endian::Big);

    // ========================================================================
    // Output
    // ========================================================================

    [[nodiscard]] conduit::Result<std::vector<uint8_t>> finish();

    [[nodiscard]] size_t size_bytes() const noexcept {
        return (bit_pos_ + 7) / 8;
    }

    [[nodiscard]] size_t size_bits() const noexcept {
        return bit_pos_;
    }

    [[nodiscard]] bool is_byte_aligned() const noexcept {
        return bit_pos_ % 8 == 0;
    }

    void clear();

    // ========================================================================
    // Error state
    // ========================================================================

    [[nodiscard]] bool has_error() const noexcept { return error_.has_value(); }
    [[nodiscard]] const conduit::Error& error() const noexcept { assert(error_.has_value()); return *error_; }
    void clear_error() noexcept { error_.reset(); }

private:
    void set_error(conduit::ErrorCode code, std::string msg);
    void write_bit(bool bit);
    void ensure_capacity(size_t additional_bits);

    [[nodiscard]] size_t current_byte_index() const noexcept {
        return bit_pos_ / 8;
    }

    std::vector<uint8_t> data_;
    size_t bit_pos_;
    std::optional<conduit::Error> error_;
};

} // namespace conduit::io
