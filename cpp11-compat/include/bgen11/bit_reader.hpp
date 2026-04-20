#ifndef BGEN11_BIT_READER_HPP
#define BGEN11_BIT_READER_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <algorithm>
#include "../compat11/span.hpp"
#include "error.hpp"
#include "endian.hpp"

namespace bgen11 {
namespace io {

class BitReader {
public:
    BitReader() : data_(), byte_pos_(0), bit_pos_(0) {}

    explicit BitReader(cpp11::span<const uint8_t> data)
        : data_(data), byte_pos_(0), bit_pos_(0) {}

    Result<uint64_t> read_bits(std::size_t count) {
        if (count == 0) return uint64_t(0);
        if (count > 64) {
            return cpp11::make_unexpected(Error(ErrorCode_InvalidArgument,
                "read_bits: count > 64"));
        }
        if (remaining_bits() < count) {
            return cpp11::make_unexpected(Error(ErrorCode_BufferUnderrun,
                "read_bits: underrun"));
        }
        uint64_t result = 0;
        std::size_t remaining = count;
        while (remaining > 0) {
            std::size_t bits_in_byte = 8 - bit_pos_;
            std::size_t bits_to_read = remaining < bits_in_byte ? remaining : bits_in_byte;
            uint8_t mask = (bits_to_read >= 8)
                ? uint8_t(0xFF)
                : uint8_t((1U << bits_to_read) - 1);
            uint8_t shift = uint8_t(bits_in_byte - bits_to_read);
            uint8_t bits = uint8_t((data_[byte_pos_] >> shift) & mask);
            result = (result << bits_to_read) | bits;
            remaining -= bits_to_read;
            advance(bits_to_read);
        }
        return result;
    }

    Result<int64_t> read_signed_bits(std::size_t count) {
        if (count == 0) return int64_t(0);
        BGEN11_TRY_ASSIGN(uint64_t, val, read_bits(count));
        if (count >= 64) return int64_t(val);
        uint64_t sign_bit = uint64_t(1) << (count - 1);
        if (val & sign_bit) {
            uint64_t mask = ~((uint64_t(1) << count) - 1);
            val |= mask;
        }
        return int64_t(val);
    }

    Result<uint8_t> read_u8() {
        align_to_byte();
        if (byte_pos_ >= data_.size())
            return cpp11::make_unexpected(Error(ErrorCode_BufferUnderrun, "read_u8"));
        return data_[byte_pos_++];
    }

    Result<uint16_t> read_u16(int e = Endian_Big) {
        align_to_byte();
        if (byte_pos_ + 2 > data_.size())
            return cpp11::make_unexpected(Error(ErrorCode_BufferUnderrun, "read_u16"));
        uint16_t v;
        if (e == Endian_Big) {
            v = uint16_t(data_[byte_pos_]) << 8 | uint16_t(data_[byte_pos_ + 1]);
        } else {
            v = uint16_t(data_[byte_pos_ + 1]) << 8 | uint16_t(data_[byte_pos_]);
        }
        byte_pos_ += 2;
        return v;
    }

    Result<uint32_t> read_u32(int e = Endian_Big) {
        align_to_byte();
        if (byte_pos_ + 4 > data_.size())
            return cpp11::make_unexpected(Error(ErrorCode_BufferUnderrun, "read_u32"));
        uint32_t v = 0;
        if (e == Endian_Big) {
            for (int i = 0; i < 4; ++i) v = (v << 8) | data_[byte_pos_ + i];
        } else {
            for (int i = 3; i >= 0; --i) v = (v << 8) | data_[byte_pos_ + i];
        }
        byte_pos_ += 4;
        return v;
    }

    Result<uint64_t> read_u64(int e = Endian_Big) {
        align_to_byte();
        if (byte_pos_ + 8 > data_.size())
            return cpp11::make_unexpected(Error(ErrorCode_BufferUnderrun, "read_u64"));
        uint64_t v = 0;
        if (e == Endian_Big) {
            for (int i = 0; i < 8; ++i) v = (v << 8) | data_[byte_pos_ + i];
        } else {
            for (int i = 7; i >= 0; --i) v = (v << 8) | data_[byte_pos_ + i];
        }
        byte_pos_ += 8;
        return v;
    }

    Result<float> read_f32(int e = Endian_Big) {
        BGEN11_TRY_ASSIGN(uint32_t, raw, read_u32(e));
        float f;
        std::memcpy(&f, &raw, sizeof(f));
        return f;
    }

    Result<double> read_f64(int e = Endian_Big) {
        BGEN11_TRY_ASSIGN(uint64_t, raw, read_u64(e));
        double d;
        std::memcpy(&d, &raw, sizeof(d));
        return d;
    }

    Result<cpp11::span<const uint8_t> > read_bytes(std::size_t count) {
        align_to_byte();
        if (byte_pos_ + count > data_.size())
            return cpp11::make_unexpected(Error(ErrorCode_BufferUnderrun, "read_bytes"));
        cpp11::span<const uint8_t> r = data_.subspan(byte_pos_, count);
        byte_pos_ += count;
        return r;
    }

    Result<std::string> read_string(std::size_t byte_length) {
        BGEN11_TRY_ASSIGN(cpp11::span<const uint8_t>, bytes, read_bytes(byte_length));
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    Result<BitReader> sub_reader(std::size_t byte_count) {
        align_to_byte();
        if (byte_pos_ + byte_count > data_.size())
            return cpp11::make_unexpected(Error(ErrorCode_BufferUnderrun, "sub_reader"));
        cpp11::span<const uint8_t> sub = data_.subspan(byte_pos_, byte_count);
        byte_pos_ += byte_count;
        return BitReader(sub);
    }

    std::size_t remaining_bytes() const {
        if (byte_pos_ >= data_.size()) return 0;
        return data_.size() - byte_pos_ - (bit_pos_ > 0 ? 1 : 0);
    }

    std::size_t remaining_bits() const {
        if (byte_pos_ >= data_.size()) return 0;
        return (data_.size() - byte_pos_) * 8 - bit_pos_;
    }

    std::size_t bit_position() const { return byte_pos_ * 8 + bit_pos_; }
    bool at_end() const { return byte_pos_ >= data_.size(); }
    bool is_byte_aligned() const { return bit_pos_ == 0; }

    VoidResult skip_bits(std::size_t count) {
        if (count > remaining_bits())
            return cpp11::make_unexpected(Error(ErrorCode_BufferUnderrun, "skip_bits"));
        advance(count);
        return VoidResult();
    }

    void align_to_byte() {
        if (bit_pos_ > 0) { byte_pos_++; bit_pos_ = 0; }
    }

    void align_to(std::size_t byte_boundary) {
        if (byte_boundary == 0) return;
        align_to_byte();
        if (byte_boundary > 1 && byte_pos_ % byte_boundary != 0) {
            byte_pos_ += byte_boundary - (byte_pos_ % byte_boundary);
        }
        if (byte_pos_ > data_.size()) byte_pos_ = data_.size();
    }

    void reset() { byte_pos_ = 0; bit_pos_ = 0; }

    cpp11::span<const uint8_t> underlying_data() const { return data_; }

private:
    cpp11::span<const uint8_t> data_;
    std::size_t byte_pos_;
    std::size_t bit_pos_;

    void advance(std::size_t bits) {
        bit_pos_ += bits;
        byte_pos_ += bit_pos_ / 8;
        bit_pos_ %= 8;
    }
};

}
}

#endif
