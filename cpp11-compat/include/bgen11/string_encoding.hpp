#ifndef BGEN11_STRING_ENCODING_HPP
#define BGEN11_STRING_ENCODING_HPP

#include <cstdint>
#include <string>
#include <cstring>
#include "../compat11/span.hpp"
#include "error.hpp"

namespace bgen11 {
namespace string_encoding {

enum Encoding {
    Encoding_Ascii = 0,
    Encoding_Utf8 = 1,
    Encoding_Iso8859_1 = 2
};

enum Padding {
    Padding_Null = 0,
    Padding_Space = 1
};

enum Trim {
    Trim_None = 0,
    Trim_Left = 1,
    Trim_Right = 2,
    Trim_Both = 3
};

inline Result<std::string> decode_string(cpp11::span<const uint8_t> bytes,
                                          int /*encoding*/ = Encoding_Ascii,
                                          int padding = Padding_Null,
                                          int trim = Trim_Right) {
    std::string s(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (padding == Padding_Null) {
        std::size_t pos = s.find('\0');
        if (pos != std::string::npos) s.resize(pos);
    }
    if (trim & Trim_Right) {
        while (!s.empty() && (s.back() == ' ' || s.back() == '\0'))
            s.pop_back();
    }
    if (trim & Trim_Left) {
        std::size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\0')) ++i;
        s.erase(0, i);
    }
    return s;
}

inline VoidResult encode_string(const std::string& s, std::size_t length,
                                 std::vector<uint8_t>& out,
                                 int /*encoding*/ = Encoding_Ascii,
                                 int padding = Padding_Null) {
    if (s.size() > length) {
        return cpp11::make_unexpected(Error(ErrorCode_StringTooLong,
            "encode_string: source length " + std::to_string(s.size()) +
            " exceeds capacity " + std::to_string(length)));
    }
    out.insert(out.end(), s.begin(), s.end());
    std::size_t pad = length - s.size();
    uint8_t fill = (padding == Padding_Space) ? uint8_t(' ') : uint8_t(0);
    for (std::size_t i = 0; i < pad; ++i) out.push_back(fill);
    return VoidResult();
}

}
}

#endif
