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
