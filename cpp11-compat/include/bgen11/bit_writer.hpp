#ifndef BGEN11_BIT_WRITER_HPP
#define BGEN11_BIT_WRITER_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "error.hpp"
#include "endian.hpp"
#include "../compat11/span.hpp"

namespace bgen11 {
namespace io {

class BitWriter {
public:
    BitWriter() : bit_pos_(0), has_error_(false) {}

    bool has_error() const { return has_error_; }
    const Error& error() const { return err_; }
    void set_error(const Error& e) { has_error_ = true; err_ = e; }

    Result<std::vector<uint8_t> > finish() {
        if (has_error_) return cpp11::make_unexpected(err_);
        return take();
    }

    VoidResult write_bits(uint64_t value, std::size_t count) {
        if (count == 0) return VoidResult();
        if (count > 64) {
            return cpp11::make_unexpected(Error(ErrorCode_InvalidArgument,
                "write_bits: count > 64"));
        }
        uint64_t mask = (count < 64) ? ((uint64_t(1) << count) - 1) : ~uint64_t(0);
        value &= mask;
        std::size_t remaining = count;
        while (remaining > 0) {
            std::size_t byte_index = bit_pos_ / 8;
            std::size_t bit_in_byte = bit_pos_ % 8;
            while (buf_.size() <= byte_index) buf_.push_back(0);
            std::size_t bits_in_byte = 8 - bit_in_byte;
            std::size_t bits_to_write = remaining < bits_in_byte ? remaining : bits_in_byte;
            uint64_t chunk = (value >> (remaining - bits_to_write)) & ((uint64_t(1) << bits_to_write) - 1);
            uint8_t shift = uint8_t(bits_in_byte - bits_to_write);
            buf_[byte_index] |= uint8_t(chunk << shift);
            bit_pos_ += bits_to_write;
            remaining -= bits_to_write;
        }
        return VoidResult();
    }

    VoidResult write_signed_bits(int64_t value, std::size_t count) {
        if (count == 0) return VoidResult();
        uint64_t mask = (count < 64) ? ((uint64_t(1) << count) - 1) : ~uint64_t(0);
        return write_bits(uint64_t(value) & mask, count);
    }

    VoidResult write_u8(uint8_t v) {
        align_to_byte();
        buf_.push_back(v);
        bit_pos_ += 8;
        return VoidResult();
    }

    VoidResult write_u16(uint16_t v, int e = Endian_Big) {
        align_to_byte();
        if (e == Endian_Big) {
            buf_.push_back(uint8_t(v >> 8));
            buf_.push_back(uint8_t(v));
        } else {
            buf_.push_back(uint8_t(v));
            buf_.push_back(uint8_t(v >> 8));
        }
        bit_pos_ += 16;
        return VoidResult();
    }

    VoidResult write_u32(uint32_t v, int e = Endian_Big) {
        align_to_byte();
        if (e == Endian_Big) {
            for (int i = 3; i >= 0; --i) buf_.push_back(uint8_t(v >> (i * 8)));
        } else {
            for (int i = 0; i < 4; ++i) buf_.push_back(uint8_t(v >> (i * 8)));
        }
        bit_pos_ += 32;
        return VoidResult();
    }

    VoidResult write_u64(uint64_t v, int e = Endian_Big) {
        align_to_byte();
        if (e == Endian_Big) {
            for (int i = 7; i >= 0; --i) buf_.push_back(uint8_t(v >> (i * 8)));
        } else {
            for (int i = 0; i < 8; ++i) buf_.push_back(uint8_t(v >> (i * 8)));
        }
        bit_pos_ += 64;
        return VoidResult();
    }

    VoidResult write_f32(float v, int e = Endian_Big) {
        uint32_t raw;
        std::memcpy(&raw, &v, sizeof(raw));
        return write_u32(raw, e);
    }

    VoidResult write_f64(double v, int e = Endian_Big) {
        uint64_t raw;
        std::memcpy(&raw, &v, sizeof(raw));
        return write_u64(raw, e);
    }

    VoidResult write_bytes(const uint8_t* data, std::size_t len) {
        align_to_byte();
        buf_.insert(buf_.end(), data, data + len);
        bit_pos_ += 8 * len;
        return VoidResult();
    }

    VoidResult write_bytes(cpp11::span<const uint8_t> bytes) {
        return write_bytes(bytes.data(), bytes.size());
    }

    VoidResult write_string(const std::string& s) {
        return write_bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

    void align_to_byte() {
        if (bit_pos_ % 8 != 0) bit_pos_ = (bit_pos_ / 8 + 1) * 8;
    }

    bool patch_u8(std::size_t byte_offset, uint8_t value) {
        if (byte_offset >= buf_.size()) return false;
        buf_[byte_offset] = value;
        return true;
    }

    bool patch_u16(std::size_t byte_offset, uint16_t value, int e = Endian_Big) {
        if (byte_offset + 2 > buf_.size()) return false;
        if (e == Endian_Big) {
            buf_[byte_offset] = uint8_t(value >> 8);
            buf_[byte_offset + 1] = uint8_t(value);
        } else {
            buf_[byte_offset] = uint8_t(value);
            buf_[byte_offset + 1] = uint8_t(value >> 8);
        }
        return true;
    }

    bool patch_u32(std::size_t byte_offset, uint32_t value, int e = Endian_Big) {
        if (byte_offset + 4 > buf_.size()) return false;
        if (e == Endian_Big) {
            for (int i = 0; i < 4; ++i) buf_[byte_offset + i] = uint8_t(value >> ((3 - i) * 8));
        } else {
            for (int i = 0; i < 4; ++i) buf_[byte_offset + i] = uint8_t(value >> (i * 8));
        }
        return true;
    }

    std::size_t bit_position() const { return bit_pos_; }
    std::size_t byte_size() const { return (bit_pos_ + 7) / 8; }
    std::size_t size_bytes() const { return byte_size(); }

    std::vector<uint8_t> take() {
        std::vector<uint8_t> r;
        r.swap(buf_);
        bit_pos_ = 0;
        return r;
    }

    const std::vector<uint8_t>& data() const { return buf_; }

private:
    std::vector<uint8_t> buf_;
    std::size_t bit_pos_;
    bool has_error_;
    Error err_;
};

}
}

#endif
