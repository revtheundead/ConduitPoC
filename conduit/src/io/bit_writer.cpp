// SPDX-License-Identifier: MIT
// Conduit - BitWriter Implementation

#include <conduit/io/bit_writer.hpp>
#include <algorithm>
#include <cstring>

namespace conduit::io {

BitWriter::BitWriter(size_t initial_capacity)
    : bit_pos_(0) {
    data_.reserve(initial_capacity);
}

// ============================================================================
// Bit-level writing
// ============================================================================

void BitWriter::write_bit(bool bit) {
    ensure_capacity(1);

    if (bit_pos_ % 8 == 0) {
        data_.push_back(0);
    }

    if (bit) {
        size_t byte_idx = current_byte_index();
        size_t bit_in_byte = 7 - (bit_pos_ % 8);
        data_[byte_idx] |= static_cast<uint8_t>(1U << bit_in_byte);
    }
    bit_pos_++;
}

void BitWriter::write_bits(uint64_t value, size_t count) {
    if (error_) return;
    if (count > 64) {
        set_error(conduit::ErrorCode::EncodingFailed, "write_bits: count exceeds 64");
        return;
    }
    if (count == 0) return;

    ensure_capacity(count);

    // Fast path: byte-aligned position — write whole bytes directly
    if (bit_pos_ % 8 == 0) {
        // Write complete bytes from MSB side of value
        size_t full_bytes = count / 8;
        size_t remaining_bits = count % 8;

        for (size_t i = 0; i < full_bytes; ++i) {
            size_t shift = (full_bytes - 1 - i) * 8 + remaining_bits;
            data_.push_back(static_cast<uint8_t>(value >> shift));
            bit_pos_ += 8;
        }

        // Write leftover bits (< 8) individually
        for (size_t i = remaining_bits; i > 0; --i) {
            write_bit((value >> (i - 1)) & 1);
        }
        return;
    }

    // Slow path: not byte-aligned
    size_t bits_to_align = 8 - (bit_pos_ % 8);
    if (count <= bits_to_align) {
        // All bits fit in current partial byte
        uint8_t mask = static_cast<uint8_t>(
            (value & ((1ULL << count) - 1)) << (bits_to_align - count));
        data_[current_byte_index()] |= mask;
        bit_pos_ += count;
    } else {
        // Fill current byte to reach alignment
        uint8_t top = static_cast<uint8_t>(
            (value >> (count - bits_to_align)) & ((1ULL << bits_to_align) - 1));
        data_[current_byte_index()] |= top;
        bit_pos_ += bits_to_align;
        count -= bits_to_align;

        // Now byte-aligned — write whole bytes
        size_t full_bytes = count / 8;
        size_t remaining = count % 8;
        for (size_t i = 0; i < full_bytes; ++i) {
            size_t shift = remaining + (full_bytes - 1 - i) * 8;
            data_.push_back(static_cast<uint8_t>(value >> shift));
            bit_pos_ += 8;
        }

        // Write leftover bits (< 8)
        if (remaining > 0) {
            data_.push_back(0);
            uint8_t rem = static_cast<uint8_t>(
                (value & ((1ULL << remaining) - 1)) << (8 - remaining));
            data_[current_byte_index()] |= rem;
            bit_pos_ += remaining;
        }
    }
}

void BitWriter::write_signed_bits(int64_t value, size_t count) {
    if (error_) return;
    write_bits(static_cast<uint64_t>(value), count);
}

// ============================================================================
// Aviation wire encoding writes
// ============================================================================

void BitWriter::write_bcd(uint64_t value, size_t bits) {
    if (error_) return;
    if (bits % 4 != 0) {
        set_error(conduit::ErrorCode::EncodingFailed, "write_bcd: bits not multiple of 4");
        return;
    }
    if (bits == 0) return;
    if (bits / 4 > 16) {
        set_error(conduit::ErrorCode::EncodingFailed, "write_bcd: max 64 bits (16 BCD digits) supported");
        return;
    }
    size_t nibbles = bits / 4;
    uint64_t raw = 0;
    uint64_t v = value;
    for (size_t i = 0; i < nibbles; i++) {
        uint8_t digit = static_cast<uint8_t>(v % 10);
        raw |= static_cast<uint64_t>(digit) << (i * 4);
        v /= 10;
    }
    if (v != 0) {
        set_error(conduit::ErrorCode::EncodeConstraintViolation, "write_bcd: value too large for given bits");
        return;
    }
    write_bits(raw, bits);
}

void BitWriter::write_bcd_signed(int64_t value, size_t bits) {
    if (error_) return;
    if (bits < 5) {
        set_error(conduit::ErrorCode::EncodingFailed, "write_bcd_signed: bits < 5");
        return;
    }
    if ((bits - 1) % 4 != 0) {
        set_error(conduit::ErrorCode::EncodingFailed, "write_bcd_signed: (bits-1) not multiple of 4");
        return;
    }
    if ((bits - 1) / 4 > 15) {
        set_error(conduit::ErrorCode::EncodingFailed, "write_bcd_signed: max 61 bits (15 BCD digits + sign) supported");
        return;
    }
    bool negative = value < 0;
    uint64_t abs_val = negative ? (static_cast<uint64_t>(~value) + 1u) : static_cast<uint64_t>(value);
    size_t nibbles = (bits - 1) / 4;
    uint64_t raw = 0;
    for (size_t i = 0; i < nibbles; i++) {
        uint8_t digit = static_cast<uint8_t>(abs_val % 10);
        raw |= static_cast<uint64_t>(digit) << (i * 4);
        abs_val /= 10;
    }
    if (abs_val != 0) {
        set_error(conduit::ErrorCode::EncodeConstraintViolation, "write_bcd_signed: value too large for given bits");
        return;
    }
    if (negative) {
        raw |= 1ULL << (bits - 1);
    }
    write_bits(raw, bits);
}

void BitWriter::write_sign_magnitude(int64_t value, size_t bits) {
    if (error_) return;
    if (bits < 2) {
        set_error(conduit::ErrorCode::EncodingFailed, "write_sign_magnitude: bits < 2");
        return;
    }
    bool negative = value < 0;
    uint64_t magnitude = negative ? (static_cast<uint64_t>(~value) + 1u) : static_cast<uint64_t>(value);
    uint64_t mag_mask = (1ULL << (bits - 1)) - 1;
    if (magnitude > mag_mask) {
        set_error(conduit::ErrorCode::EncodeConstraintViolation, "write_sign_magnitude: magnitude too large for given bits");
        return;
    }
    uint64_t raw = magnitude & mag_mask;
    if (negative) {
        raw |= 1ULL << (bits - 1);
    }
    write_bits(raw, bits);
}

// ============================================================================
// Byte-level writing
// ============================================================================

void BitWriter::write_u8(uint8_t value) {
    if (error_) return;
    align_to_byte();
    data_.push_back(value);
    bit_pos_ += 8;
}

void BitWriter::write_u16(uint16_t value, Endian e) {
    if (error_) return;
    align_to_byte();
    if (e == Endian::Big) {
        data_.push_back(static_cast<uint8_t>(value >> 8));
        data_.push_back(static_cast<uint8_t>(value));
    } else {
        data_.push_back(static_cast<uint8_t>(value));
        data_.push_back(static_cast<uint8_t>(value >> 8));
    }
    bit_pos_ += 16;
}

void BitWriter::write_u32(uint32_t value, Endian e) {
    if (error_) return;
    align_to_byte();
    if (e == Endian::Big) {
        data_.push_back(static_cast<uint8_t>(value >> 24));
        data_.push_back(static_cast<uint8_t>(value >> 16));
        data_.push_back(static_cast<uint8_t>(value >> 8));
        data_.push_back(static_cast<uint8_t>(value));
    } else {
        data_.push_back(static_cast<uint8_t>(value));
        data_.push_back(static_cast<uint8_t>(value >> 8));
        data_.push_back(static_cast<uint8_t>(value >> 16));
        data_.push_back(static_cast<uint8_t>(value >> 24));
    }
    bit_pos_ += 32;
}

void BitWriter::write_u64(uint64_t value, Endian e) {
    if (error_) return;
    align_to_byte();
    if (e == Endian::Big) {
        data_.push_back(static_cast<uint8_t>(value >> 56));
        data_.push_back(static_cast<uint8_t>(value >> 48));
        data_.push_back(static_cast<uint8_t>(value >> 40));
        data_.push_back(static_cast<uint8_t>(value >> 32));
        data_.push_back(static_cast<uint8_t>(value >> 24));
        data_.push_back(static_cast<uint8_t>(value >> 16));
        data_.push_back(static_cast<uint8_t>(value >> 8));
        data_.push_back(static_cast<uint8_t>(value));
    } else {
        data_.push_back(static_cast<uint8_t>(value));
        data_.push_back(static_cast<uint8_t>(value >> 8));
        data_.push_back(static_cast<uint8_t>(value >> 16));
        data_.push_back(static_cast<uint8_t>(value >> 24));
        data_.push_back(static_cast<uint8_t>(value >> 32));
        data_.push_back(static_cast<uint8_t>(value >> 40));
        data_.push_back(static_cast<uint8_t>(value >> 48));
        data_.push_back(static_cast<uint8_t>(value >> 56));
    }
    bit_pos_ += 64;
}

void BitWriter::write_f16(float value, Endian e) {
    if (error_) return;
    uint16_t raw = io::f32_to_f16(value);
    write_u16(raw, e);
}

void BitWriter::write_f32(float value, Endian e) {
    if (error_) return;
    uint32_t raw;
    std::memcpy(&raw, &value, sizeof(float));
    write_u32(raw, e);
}

void BitWriter::write_f48(double value, Endian e) {
    if (error_) return;
    align_to_byte();
    // Ensure space for 6 bytes
    data_.resize(data_.size() + 6);
    io::write_f48(std::span<uint8_t>(data_).subspan(data_.size() - 6), 0, value, e);
    bit_pos_ += 48;
}

void BitWriter::write_f64(double value, Endian e) {
    if (error_) return;
    uint64_t raw;
    std::memcpy(&raw, &value, sizeof(double));
    write_u64(raw, e);
}

// ============================================================================
// Bulk writing
// ============================================================================

void BitWriter::write_bytes(std::span<const uint8_t> data) {
    if (error_) return;
    align_to_byte();
    data_.reserve(data_.size() + data.size());
    data_.insert(data_.end(), data.begin(), data.end());
    bit_pos_ += data.size() * 8;
}

bool BitWriter::write_string(std::string_view str, size_t padded_length, char padding) {
    if (error_) return false;
    align_to_byte();
    data_.reserve(data_.size() + padded_length);
    bool truncated = str.size() > padded_length;
    size_t copy_len = std::min(str.size(), padded_length);
    auto str_begin = reinterpret_cast<const uint8_t*>(str.data());
    data_.insert(data_.end(), str_begin, str_begin + copy_len);
    data_.insert(data_.end(), padded_length - copy_len, static_cast<uint8_t>(padding));
    bit_pos_ += padded_length * 8;
    return !truncated;
}

// ============================================================================
// Alignment and patching
// ============================================================================

void BitWriter::align_to_byte() {
    size_t remainder = bit_pos_ % 8;
    if (remainder == 0) return;
    size_t pad_bits = 8 - remainder;
    // Current byte already exists in data_ (partially written);
    // the remaining bits in that byte are already zero from push_back(0).
    bit_pos_ += pad_bits;
}

void BitWriter::align_to(size_t byte_boundary) {
    if (byte_boundary == 0) return;
    align_to_byte();
    size_t current_bytes = size_bytes();
    size_t remainder = current_bytes % byte_boundary;
    if (remainder != 0) {
        size_t padding = byte_boundary - remainder;
        for (size_t i = 0; i < padding; ++i) {
            write_u8(0);
        }
    }
}

bool BitWriter::patch_u8(size_t byte_offset, uint8_t value) {
    if (error_) return false;
    if (byte_offset >= data_.size()) {
        set_error(conduit::ErrorCode::BufferOverrun, "patch_u8: byte_offset out of range");
        return false;
    }
    data_[byte_offset] = value;
    return true;
}

bool BitWriter::patch_u16(size_t byte_offset, uint16_t value, Endian e) {
    if (error_) return false;
    if (byte_offset + 2 > data_.size()) {
        set_error(conduit::ErrorCode::BufferOverrun, "patch_u16: byte_offset out of range");
        return false;
    }
    if (e == Endian::Big) {
        data_[byte_offset]     = static_cast<uint8_t>(value >> 8);
        data_[byte_offset + 1] = static_cast<uint8_t>(value);
    } else {
        data_[byte_offset]     = static_cast<uint8_t>(value);
        data_[byte_offset + 1] = static_cast<uint8_t>(value >> 8);
    }
    return true;
}

bool BitWriter::patch_u32(size_t byte_offset, uint32_t value, Endian e) {
    if (error_) return false;
    if (byte_offset + 4 > data_.size()) {
        set_error(conduit::ErrorCode::BufferOverrun, "patch_u32: byte_offset out of range");
        return false;
    }
    if (e == Endian::Big) {
        data_[byte_offset]     = static_cast<uint8_t>(value >> 24);
        data_[byte_offset + 1] = static_cast<uint8_t>(value >> 16);
        data_[byte_offset + 2] = static_cast<uint8_t>(value >> 8);
        data_[byte_offset + 3] = static_cast<uint8_t>(value);
    } else {
        data_[byte_offset]     = static_cast<uint8_t>(value);
        data_[byte_offset + 1] = static_cast<uint8_t>(value >> 8);
        data_[byte_offset + 2] = static_cast<uint8_t>(value >> 16);
        data_[byte_offset + 3] = static_cast<uint8_t>(value >> 24);
    }
    return true;
}

// ============================================================================
// Output
// ============================================================================

conduit::Result<std::vector<uint8_t>> BitWriter::finish() {
    if (error_) {
        auto err = std::move(*error_);
        error_.reset();
        data_.clear();
        bit_pos_ = 0;
        return std::unexpected(std::move(err));
    }
    align_to_byte();
    bit_pos_ = 0;
    return std::move(data_);
}

void BitWriter::clear() {
    data_.clear();
    bit_pos_ = 0;
    error_.reset();
}

void BitWriter::set_error(conduit::ErrorCode code, std::string msg) {
    if (!error_) {
        error_.emplace(code, std::move(msg));
    }
}

// ============================================================================
// Private helpers
// ============================================================================

void BitWriter::ensure_capacity(size_t additional_bits) {
    // Guard against overflow in bit arithmetic
    if (additional_bits > SIZE_MAX - bit_pos_ - 7) {
        set_error(conduit::ErrorCode::InvalidArgument, "ensure_capacity: bit count overflow");
        return;
    }
    size_t needed_bytes = (bit_pos_ + additional_bits + 7) / 8;
    if (data_.capacity() < needed_bytes) {
        data_.reserve(std::max(needed_bytes, data_.capacity() * 2));
    }
}

} // namespace conduit::io
