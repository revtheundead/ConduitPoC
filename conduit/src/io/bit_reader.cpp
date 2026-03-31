// SPDX-License-Identifier: MIT
// Conduit - BitReader Implementation

#include <conduit/io/bit_reader.hpp>
#include <algorithm>
#include <cstring>

namespace conduit::io {

// ============================================================================
// Bit-level reading
// ============================================================================

Result<uint64_t> BitReader::read_bits(size_t count) {
    if (count == 0) return uint64_t{0};
    if (count > 64) {
        return std::unexpected(CONDUIT_ERROR(
            ErrorCode::InvalidArgument, "read_bits: count exceeds 64"));
    }
    if (remaining_bits() < count) {
        return std::unexpected(CONDUIT_ERROR(
            ErrorCode::BufferUnderrun, "read_bits: not enough bits remaining"));
    }

    uint64_t result = 0;
    size_t remaining = count;

    while (remaining > 0) {
        size_t bits_in_byte = 8 - bit_pos_;
        size_t bits_to_read = std::min(remaining, bits_in_byte);

        uint8_t mask = static_cast<uint8_t>((bits_to_read >= 8) ? 0xFF
                       : ((1U << bits_to_read) - 1));
        uint8_t shift = static_cast<uint8_t>(bits_in_byte - bits_to_read);
        uint8_t bits = static_cast<uint8_t>((data_[byte_pos_] >> shift) & mask);

        result = (result << bits_to_read) | bits;
        remaining -= bits_to_read;
        advance(bits_to_read);
    }

    return result;
}

Result<int64_t> BitReader::read_signed_bits(size_t count) {
    if (count == 0) return int64_t{0};

    CONDUIT_TRY_ASSIGN(uint64_t, val, read_bits(count));

    if (count >= 64) {
        return static_cast<int64_t>(val);
    }

    // Sign extend
    uint64_t sign_bit = 1ULL << (count - 1);
    if (val & sign_bit) {
        uint64_t extension_mask = ~((1ULL << count) - 1);
        val |= extension_mask;
    }
    return static_cast<int64_t>(val);
}

// ============================================================================
// Aviation wire encoding reads
// ============================================================================

Result<uint64_t> BitReader::read_bcd(size_t bits) {
    if (bits == 0) return uint64_t{0};
    if (bits % 4 != 0) {
        return std::unexpected(CONDUIT_ERROR(
            ErrorCode::InvalidArgument, "read_bcd: bits must be a multiple of 4"));
    }
    // Max 16 nibbles (64 bits) — avoids shift-past-width UB and fits in uint64_t
    if (bits / 4 > 16) {
        return std::unexpected(CONDUIT_ERROR(
            ErrorCode::InvalidArgument, "read_bcd: max 64 bits (16 BCD digits) supported"));
    }

    CONDUIT_TRY_ASSIGN(uint64_t, raw, read_bits(bits));

    uint64_t result = 0;
    uint64_t multiplier = 1;
    size_t nibbles = bits / 4;
    for (size_t i = 0; i < nibbles; i++) {
        uint8_t digit = static_cast<uint8_t>((raw >> (i * 4)) & 0xF);
        if (digit > 9) {
            return std::unexpected(CONDUIT_ERROR(
                ErrorCode::InvalidArgument, "read_bcd: invalid BCD digit"));
        }
        result += digit * multiplier;
        multiplier *= 10;
    }
    return result;
}

Result<int64_t> BitReader::read_bcd_signed(size_t bits) {
    if (bits < 5) {
        return std::unexpected(CONDUIT_ERROR(
            ErrorCode::InvalidArgument, "read_bcd_signed: bits must be >= 5"));
    }
    if ((bits - 1) % 4 != 0) {
        return std::unexpected(CONDUIT_ERROR(
            ErrorCode::InvalidArgument, "read_bcd_signed: (bits - 1) must be a multiple of 4"));
    }
    // Max 15 BCD nibbles + 1 sign bit = 61 bits (avoids shift-past-width UB)
    if ((bits - 1) / 4 > 15) {
        return std::unexpected(CONDUIT_ERROR(
            ErrorCode::InvalidArgument, "read_bcd_signed: max 61 bits (15 BCD digits + sign) supported"));
    }

    CONDUIT_TRY_ASSIGN(uint64_t, raw, read_bits(bits));

    bool negative = (raw >> (bits - 1)) & 1;
    uint64_t bcd_part = raw & ((1ULL << (bits - 1)) - 1);

    int64_t result = 0;
    int64_t multiplier = 1;
    size_t nibbles = (bits - 1) / 4;
    for (size_t i = 0; i < nibbles; i++) {
        uint8_t digit = static_cast<uint8_t>((bcd_part >> (i * 4)) & 0xF);
        if (digit > 9) {
            return std::unexpected(CONDUIT_ERROR(
                ErrorCode::InvalidArgument, "read_bcd_signed: invalid BCD digit"));
        }
        result += digit * multiplier;
        multiplier *= 10;
    }

    return negative ? -result : result;
}

Result<int64_t> BitReader::read_sign_magnitude(size_t bits) {
    if (bits < 2) {
        return std::unexpected(CONDUIT_ERROR(
            ErrorCode::InvalidArgument, "read_sign_magnitude: bits must be >= 2"));
    }

    CONDUIT_TRY_ASSIGN(uint64_t, raw, read_bits(bits));

    bool negative = (raw >> (bits - 1)) & 1;
    uint64_t magnitude = raw & ((1ULL << (bits - 1)) - 1);

    return negative ? -static_cast<int64_t>(magnitude) : static_cast<int64_t>(magnitude);
}

// ============================================================================
// Byte-level reading
// ============================================================================

Result<uint8_t> BitReader::read_u8() {
    align_to_byte();
    if (byte_pos_ >= data_.size()) {
        return std::unexpected(CONDUIT_ERROR(
            ErrorCode::BufferUnderrun, "read_u8: not enough data"));
    }
    uint8_t val = data_[byte_pos_++];
    return val;
}

Result<uint16_t> BitReader::read_u16(Endian e) {
    align_to_byte();
    if (byte_pos_ + 2 > data_.size()) {
        return std::unexpected(CONDUIT_ERROR(
            ErrorCode::BufferUnderrun, "read_u16: not enough data"));
    }
    uint16_t val = io::read_u16(data_, byte_pos_, e);
    byte_pos_ += 2;
    return val;
}

Result<uint32_t> BitReader::read_u32(Endian e) {
    align_to_byte();
    if (byte_pos_ + 4 > data_.size()) {
        return std::unexpected(CONDUIT_ERROR(
            ErrorCode::BufferUnderrun, "read_u32: not enough data"));
    }
    uint32_t val = io::read_u32(data_, byte_pos_, e);
    byte_pos_ += 4;
    return val;
}

Result<uint64_t> BitReader::read_u64(Endian e) {
    align_to_byte();
    if (byte_pos_ + 8 > data_.size()) {
        return std::unexpected(CONDUIT_ERROR(
            ErrorCode::BufferUnderrun, "read_u64: not enough data"));
    }
    uint64_t val = io::read_u64(data_, byte_pos_, e);
    byte_pos_ += 8;
    return val;
}

Result<float> BitReader::read_f16(Endian e) {
    align_to_byte();
    if (byte_pos_ + 2 > data_.size()) {
        return std::unexpected(CONDUIT_ERROR(
            ErrorCode::BufferUnderrun, "read_f16: not enough data"));
    }
    float val = io::read_f16(data_, byte_pos_, e);
    byte_pos_ += 2;
    return val;
}

Result<float> BitReader::read_f32(Endian e) {
    align_to_byte();
    if (byte_pos_ + 4 > data_.size()) {
        return std::unexpected(CONDUIT_ERROR(
            ErrorCode::BufferUnderrun, "read_f32: not enough data"));
    }
    float val = io::read_f32(data_, byte_pos_, e);
    byte_pos_ += 4;
    return val;
}

Result<double> BitReader::read_f48(Endian e) {
    align_to_byte();
    if (byte_pos_ + 6 > data_.size()) {
        return std::unexpected(CONDUIT_ERROR(
            ErrorCode::BufferUnderrun, "read_f48: not enough data"));
    }
    double val = io::read_f48(data_, byte_pos_, e);
    byte_pos_ += 6;
    return val;
}

Result<double> BitReader::read_f64(Endian e) {
    align_to_byte();
    if (byte_pos_ + 8 > data_.size()) {
        return std::unexpected(CONDUIT_ERROR(
            ErrorCode::BufferUnderrun, "read_f64: not enough data"));
    }
    double val = io::read_f64(data_, byte_pos_, e);
    byte_pos_ += 8;
    return val;
}

// ============================================================================
// Bulk reading
// ============================================================================

Result<std::span<const uint8_t>> BitReader::read_bytes(size_t count) {
    align_to_byte();
    if (byte_pos_ + count > data_.size()) {
        return std::unexpected(CONDUIT_ERROR(
            ErrorCode::BufferUnderrun, "read_bytes: not enough data"));
    }
    auto result = data_.subspan(byte_pos_, count);
    byte_pos_ += count;
    return result;
}

Result<std::string> BitReader::read_string(size_t byte_length) {
    CONDUIT_TRY_ASSIGN(auto, bytes, read_bytes(byte_length));
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

// ============================================================================
// Sub-reader
// ============================================================================

Result<BitReader> BitReader::sub_reader(size_t byte_count) {
    align_to_byte();
    if (byte_pos_ + byte_count > data_.size()) {
        return std::unexpected(CONDUIT_ERROR(
            ErrorCode::BufferUnderrun, "sub_reader: not enough data"));
    }
    auto sub_span = data_.subspan(byte_pos_, byte_count);
    byte_pos_ += byte_count;
    return BitReader(sub_span);
}

// ============================================================================
// Position and state
// ============================================================================

size_t BitReader::remaining_bytes() const noexcept {
    if (byte_pos_ >= data_.size()) return 0;
    return data_.size() - byte_pos_ - (bit_pos_ > 0 ? 1 : 0);
}

size_t BitReader::remaining_bits() const noexcept {
    if (byte_pos_ >= data_.size()) return 0;
    return (data_.size() - byte_pos_) * 8 - bit_pos_;
}

size_t BitReader::bit_position() const noexcept {
    return byte_pos_ * 8 + bit_pos_;
}

bool BitReader::at_end() const noexcept {
    return byte_pos_ >= data_.size();
}

VoidResult BitReader::skip_bits(size_t count) {
    if (count > remaining_bits()) {
        return std::unexpected(Error(ErrorCode::BufferUnderrun,
            "skip_bits: need " + std::to_string(count) + " bits, only " + std::to_string(remaining_bits()) + " remain"));
    }
    advance(count);
    return {};
}

void BitReader::align_to_byte() noexcept {
    if (bit_pos_ > 0) {
        byte_pos_++;
        bit_pos_ = 0;
    }
}

void BitReader::align_to(size_t byte_boundary) noexcept {
    if (byte_boundary == 0) return;
    align_to_byte();
    if (byte_boundary > 1 && byte_pos_ % byte_boundary != 0) {
        byte_pos_ += byte_boundary - (byte_pos_ % byte_boundary);
    }
    // Clamp to data size to prevent unsigned underflow in remaining_bytes()
    if (byte_pos_ > data_.size()) {
        byte_pos_ = data_.size();
    }
}

} // namespace conduit::io
