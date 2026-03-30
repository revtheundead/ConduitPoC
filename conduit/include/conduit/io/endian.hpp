// SPDX-License-Identifier: MIT
// Conduit - Endianness Utilities

#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <span>

namespace conduit::io {

// ============================================================================
// Endian Enum
// ============================================================================

enum class Endian {
    Big,
    Little
};

// ============================================================================
// Byte Order Detection
// ============================================================================

[[nodiscard]] constexpr bool is_little_endian() noexcept {
    return std::endian::native == std::endian::little;
}

[[nodiscard]] constexpr bool is_big_endian() noexcept {
    return std::endian::native == std::endian::big;
}

// ============================================================================
// Byte Swap Operations
// ============================================================================

[[nodiscard]] constexpr uint16_t byte_swap(uint16_t value) noexcept {
    return static_cast<uint16_t>((value << 8) | (value >> 8));
}

[[nodiscard]] constexpr uint32_t byte_swap(uint32_t value) noexcept {
    return ((value & 0x000000FFU) << 24) |
           ((value & 0x0000FF00U) << 8)  |
           ((value & 0x00FF0000U) >> 8)  |
           ((value & 0xFF000000U) >> 24);
}

[[nodiscard]] constexpr uint64_t byte_swap(uint64_t value) noexcept {
    return ((value & 0x00000000000000FFULL) << 56) |
           ((value & 0x000000000000FF00ULL) << 40) |
           ((value & 0x0000000000FF0000ULL) << 24) |
           ((value & 0x00000000FF000000ULL) << 8)  |
           ((value & 0x000000FF00000000ULL) >> 8)  |
           ((value & 0x0000FF0000000000ULL) >> 24) |
           ((value & 0x00FF000000000000ULL) >> 40) |
           ((value & 0xFF00000000000000ULL) >> 56);
}

// ============================================================================
// Endian-Aware Read from Byte Buffer
// ============================================================================

[[nodiscard]] inline uint16_t read_u16(std::span<const uint8_t> data, size_t offset, Endian e) noexcept {
    if (offset + 2 > data.size()) return 0;
    if (e == Endian::Big) {
        return static_cast<uint16_t>(
            (static_cast<uint16_t>(data[offset]) << 8) |
             static_cast<uint16_t>(data[offset + 1]));
    } else {
        return static_cast<uint16_t>(
            (static_cast<uint16_t>(data[offset + 1]) << 8) |
             static_cast<uint16_t>(data[offset]));
    }
}

[[nodiscard]] inline uint32_t read_u32(std::span<const uint8_t> data, size_t offset, Endian e) noexcept {
    if (offset + 4 > data.size()) return 0;
    if (e == Endian::Big) {
        return (static_cast<uint32_t>(data[offset])     << 24) |
               (static_cast<uint32_t>(data[offset + 1]) << 16) |
               (static_cast<uint32_t>(data[offset + 2]) << 8)  |
                static_cast<uint32_t>(data[offset + 3]);
    } else {
        return (static_cast<uint32_t>(data[offset + 3]) << 24) |
               (static_cast<uint32_t>(data[offset + 2]) << 16) |
               (static_cast<uint32_t>(data[offset + 1]) << 8)  |
                static_cast<uint32_t>(data[offset]);
    }
}

[[nodiscard]] inline uint64_t read_u64(std::span<const uint8_t> data, size_t offset, Endian e) noexcept {
    if (offset + 8 > data.size()) return 0;
    if (e == Endian::Big) {
        return (static_cast<uint64_t>(data[offset])     << 56) |
               (static_cast<uint64_t>(data[offset + 1]) << 48) |
               (static_cast<uint64_t>(data[offset + 2]) << 40) |
               (static_cast<uint64_t>(data[offset + 3]) << 32) |
               (static_cast<uint64_t>(data[offset + 4]) << 24) |
               (static_cast<uint64_t>(data[offset + 5]) << 16) |
               (static_cast<uint64_t>(data[offset + 6]) << 8)  |
                static_cast<uint64_t>(data[offset + 7]);
    } else {
        return (static_cast<uint64_t>(data[offset + 7]) << 56) |
               (static_cast<uint64_t>(data[offset + 6]) << 48) |
               (static_cast<uint64_t>(data[offset + 5]) << 40) |
               (static_cast<uint64_t>(data[offset + 4]) << 32) |
               (static_cast<uint64_t>(data[offset + 3]) << 24) |
               (static_cast<uint64_t>(data[offset + 2]) << 16) |
               (static_cast<uint64_t>(data[offset + 1]) << 8)  |
                static_cast<uint64_t>(data[offset]);
    }
}

// ============================================================================
// Endian-Aware Write to Byte Buffer
// ============================================================================

inline void write_u16(std::span<uint8_t> data, size_t offset, uint16_t value, Endian e) noexcept {
    if (offset + 2 > data.size()) return;
    if (e == Endian::Big) {
        data[offset]     = static_cast<uint8_t>(value >> 8);
        data[offset + 1] = static_cast<uint8_t>(value);
    } else {
        data[offset]     = static_cast<uint8_t>(value);
        data[offset + 1] = static_cast<uint8_t>(value >> 8);
    }
}

inline void write_u32(std::span<uint8_t> data, size_t offset, uint32_t value, Endian e) noexcept {
    if (offset + 4 > data.size()) return;
    if (e == Endian::Big) {
        data[offset]     = static_cast<uint8_t>(value >> 24);
        data[offset + 1] = static_cast<uint8_t>(value >> 16);
        data[offset + 2] = static_cast<uint8_t>(value >> 8);
        data[offset + 3] = static_cast<uint8_t>(value);
    } else {
        data[offset]     = static_cast<uint8_t>(value);
        data[offset + 1] = static_cast<uint8_t>(value >> 8);
        data[offset + 2] = static_cast<uint8_t>(value >> 16);
        data[offset + 3] = static_cast<uint8_t>(value >> 24);
    }
}

inline void write_u64(std::span<uint8_t> data, size_t offset, uint64_t value, Endian e) noexcept {
    if (offset + 8 > data.size()) return;
    if (e == Endian::Big) {
        data[offset]     = static_cast<uint8_t>(value >> 56);
        data[offset + 1] = static_cast<uint8_t>(value >> 48);
        data[offset + 2] = static_cast<uint8_t>(value >> 40);
        data[offset + 3] = static_cast<uint8_t>(value >> 32);
        data[offset + 4] = static_cast<uint8_t>(value >> 24);
        data[offset + 5] = static_cast<uint8_t>(value >> 16);
        data[offset + 6] = static_cast<uint8_t>(value >> 8);
        data[offset + 7] = static_cast<uint8_t>(value);
    } else {
        data[offset]     = static_cast<uint8_t>(value);
        data[offset + 1] = static_cast<uint8_t>(value >> 8);
        data[offset + 2] = static_cast<uint8_t>(value >> 16);
        data[offset + 3] = static_cast<uint8_t>(value >> 24);
        data[offset + 4] = static_cast<uint8_t>(value >> 32);
        data[offset + 5] = static_cast<uint8_t>(value >> 40);
        data[offset + 6] = static_cast<uint8_t>(value >> 48);
        data[offset + 7] = static_cast<uint8_t>(value >> 56);
    }
}

// ============================================================================
// Float/Double Conversions via memcpy (type-punning safe)
// ============================================================================

// ============================================================================
// IEEE 754 half-precision (float16) conversion helpers
// ============================================================================

[[nodiscard]] inline float f16_to_f32(uint16_t h) noexcept {
    uint32_t sign = (static_cast<uint32_t>(h) & 0x8000u) << 16;
    uint32_t exp  = (h >> 10) & 0x1Fu;
    uint32_t mant = h & 0x03FFu;

    if (exp == 0) {
        if (mant == 0) {
            // +-zero
            float result;
            std::memcpy(&result, &sign, sizeof(float));
            return result;
        }
        // Subnormal: normalize
        while (!(mant & 0x0400u)) {
            mant <<= 1;
            exp--;
        }
        exp++;
        mant &= ~0x0400u;
        exp += (127 - 15);
        uint32_t f = sign | (exp << 23) | (mant << 13);
        float result;
        std::memcpy(&result, &f, sizeof(float));
        return result;
    }
    if (exp == 31) {
        // Inf or NaN
        uint32_t f = sign | 0x7F800000u | (mant << 13);
        float result;
        std::memcpy(&result, &f, sizeof(float));
        return result;
    }
    // Normal
    exp += (127 - 15);
    uint32_t f = sign | (exp << 23) | (mant << 13);
    float result;
    std::memcpy(&result, &f, sizeof(float));
    return result;
}

[[nodiscard]] inline uint16_t f32_to_f16(float value) noexcept {
    uint32_t f;
    std::memcpy(&f, &value, sizeof(float));

    uint16_t sign = static_cast<uint16_t>((f >> 16) & 0x8000u);
    int32_t exp   = static_cast<int32_t>((f >> 23) & 0xFFu) - 127 + 15;
    uint32_t mant = f & 0x007FFFFFu;

    if (((f >> 23) & 0xFFu) == 255) {
        // Inf or NaN
        return static_cast<uint16_t>(sign | 0x7C00u | (mant ? 0x0200u : 0));
    }
    if (exp >= 31) {
        // Overflow -> Inf
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    if (exp <= 0) {
        if (exp < -10) {
            // Too small -> zero
            return sign;
        }
        // Subnormal
        mant |= 0x00800000u;
        uint32_t shift = static_cast<uint32_t>(1 - exp);
        uint16_t h = static_cast<uint16_t>(sign | (mant >> (13 + shift)));
        return h;
    }
    return static_cast<uint16_t>(sign | (static_cast<uint16_t>(exp) << 10) | static_cast<uint16_t>(mant >> 13));
}

// ============================================================================
// IEEE 754 48-bit (truncated double) conversion helpers
//
// 48-bit floats use the same format as 64-bit doubles but truncate the
// mantissa: sign(1) + exponent(11) + mantissa(36) = 48 bits.
// The bottom 16 bits of the 52-bit mantissa are dropped on write and
// zero-filled on read.
// ============================================================================

[[nodiscard]] inline double f48_to_f64(uint64_t h48) noexcept {
    // Shift the 48-bit value into the top 48 bits of a 64-bit double,
    // zero-filling the bottom 16 bits.
    uint64_t d = h48 << 16;
    double result;
    std::memcpy(&result, &d, sizeof(double));
    return result;
}

[[nodiscard]] inline uint64_t f64_to_f48(double value) noexcept {
    uint64_t d;
    std::memcpy(&d, &value, sizeof(double));
    // Take the top 48 bits (sign + exponent + top 36 mantissa bits)
    return d >> 16;
}

inline void write_u48(std::span<uint8_t> data, size_t offset, uint64_t value, Endian e) noexcept {
    if (offset + 6 > data.size()) return;
    if (e == Endian::Big) {
        data[offset]     = static_cast<uint8_t>((value >> 40) & 0xFF);
        data[offset + 1] = static_cast<uint8_t>((value >> 32) & 0xFF);
        data[offset + 2] = static_cast<uint8_t>((value >> 24) & 0xFF);
        data[offset + 3] = static_cast<uint8_t>((value >> 16) & 0xFF);
        data[offset + 4] = static_cast<uint8_t>((value >> 8) & 0xFF);
        data[offset + 5] = static_cast<uint8_t>(value & 0xFF);
    } else {
        data[offset]     = static_cast<uint8_t>(value & 0xFF);
        data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        data[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
        data[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
        data[offset + 4] = static_cast<uint8_t>((value >> 32) & 0xFF);
        data[offset + 5] = static_cast<uint8_t>((value >> 40) & 0xFF);
    }
}

[[nodiscard]] inline uint64_t read_u48(std::span<const uint8_t> data, size_t offset, Endian e) noexcept {
    if (offset + 6 > data.size()) return 0;
    if (e == Endian::Big) {
        return (static_cast<uint64_t>(data[offset])     << 40) |
               (static_cast<uint64_t>(data[offset + 1]) << 32) |
               (static_cast<uint64_t>(data[offset + 2]) << 24) |
               (static_cast<uint64_t>(data[offset + 3]) << 16) |
               (static_cast<uint64_t>(data[offset + 4]) << 8)  |
                static_cast<uint64_t>(data[offset + 5]);
    } else {
        return (static_cast<uint64_t>(data[offset + 5]) << 40) |
               (static_cast<uint64_t>(data[offset + 4]) << 32) |
               (static_cast<uint64_t>(data[offset + 3]) << 24) |
               (static_cast<uint64_t>(data[offset + 2]) << 16) |
               (static_cast<uint64_t>(data[offset + 1]) << 8)  |
                static_cast<uint64_t>(data[offset]);
    }
}

[[nodiscard]] inline double read_f48(std::span<const uint8_t> data, size_t offset, Endian e) noexcept {
    uint64_t raw = read_u48(data, offset, e);
    return f48_to_f64(raw);
}

inline void write_f48(std::span<uint8_t> data, size_t offset, double value, Endian e) noexcept {
    uint64_t raw = f64_to_f48(value);
    write_u48(data, offset, raw, e);
}

[[nodiscard]] inline float read_f16(std::span<const uint8_t> data, size_t offset, Endian e) noexcept {
    uint16_t raw = read_u16(data, offset, e);
    return f16_to_f32(raw);
}

inline void write_f16(std::span<uint8_t> data, size_t offset, float value, Endian e) noexcept {
    uint16_t raw = f32_to_f16(value);
    write_u16(data, offset, raw, e);
}

[[nodiscard]] inline float read_f32(std::span<const uint8_t> data, size_t offset, Endian e) noexcept {
    uint32_t raw = read_u32(data, offset, e);
    float result;
    std::memcpy(&result, &raw, sizeof(float));
    return result;
}

[[nodiscard]] inline double read_f64(std::span<const uint8_t> data, size_t offset, Endian e) noexcept {
    uint64_t raw = read_u64(data, offset, e);
    double result;
    std::memcpy(&result, &raw, sizeof(double));
    return result;
}

inline void write_f32(std::span<uint8_t> data, size_t offset, float value, Endian e) noexcept {
    uint32_t raw;
    std::memcpy(&raw, &value, sizeof(float));
    write_u32(data, offset, raw, e);
}

inline void write_f64(std::span<uint8_t> data, size_t offset, double value, Endian e) noexcept {
    uint64_t raw;
    std::memcpy(&raw, &value, sizeof(double));
    write_u64(data, offset, raw, e);
}

} // namespace conduit::io
