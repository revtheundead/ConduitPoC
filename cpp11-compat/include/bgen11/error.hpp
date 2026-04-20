#ifndef BGEN11_ERROR_HPP
#define BGEN11_ERROR_HPP

#include <string>
#include "../compat11/expected.hpp"

namespace bgen11 {

enum ErrorCode {
    ErrorCode_BufferUnderrun = 100,
    ErrorCode_InvalidFieldValue = 101,
    ErrorCode_ConstraintViolation = 102,
    ErrorCode_UnknownEnumValue = 103,
    ErrorCode_UnknownDiscriminator = 104,
    ErrorCode_ExactConsumptionFailed = 105,
    ErrorCode_NegativeLength = 106,
    ErrorCode_DecodingFailed = 107,
    ErrorCode_UnknownTypeId = 108,
    ErrorCode_MaxLengthExceeded = 109,
    ErrorCode_StringEncodingError = 110,
    ErrorCode_ConstraintViolationDeferred = 111,

    ErrorCode_EncodeConstraintViolation = 200,
    ErrorCode_MissingRequiredField = 201,
    ErrorCode_EncodingFailed = 202,
    ErrorCode_BufferOverrun = 203,
    ErrorCode_StringTooLong = 204,
    ErrorCode_DirectionViolation = 205,

    ErrorCode_InternalError = 900,
    ErrorCode_NotImplemented = 901,
    ErrorCode_InvalidArgument = 902,
    ErrorCode_InvalidState = 903,
    ErrorCode_Timeout = 904
};

class Error {
public:
    Error() : code_(ErrorCode_InternalError) {}
    Error(int code, const std::string& message)
        : code_(code), message_(message) {}

    int code() const { return code_; }
    const std::string& message() const { return message_; }

    Error with_context(const std::string& ctx) const {
        Error r = *this;
        if (r.context_.empty()) r.context_ = ctx;
        else r.context_ = ctx + " > " + r.context_;
        return r;
    }

    const std::string& context() const { return context_; }

    std::string format() const {
        std::string s = "[" + std::to_string(code_) + "] " + message_;
        if (!context_.empty()) s += " (" + context_ + ")";
        return s;
    }

    bool operator==(const Error& other) const { return code_ == other.code_; }
    bool operator!=(const Error& other) const { return code_ != other.code_; }

private:
    int code_;
    std::string message_;
    std::string context_;
};

template <typename T>
struct ResultType {
    typedef cpp11::expected<T, Error> type;
};

template <typename T>
using Result = cpp11::expected<T, Error>;

typedef cpp11::expected<void, Error> VoidResult;

}

#define BGEN11_ERROR(code, msg) ::bgen11::Error((code), (msg))

#define BGEN11_TRY(expr) \
    do { \
        auto _bgen_r_ = (expr); \
        if (!_bgen_r_) return ::cpp11::make_unexpected(_bgen_r_.error()); \
    } while (0)

#define BGEN11_TRY_ASSIGN(type, var, expr) \
    auto _bgen_try_##var = (expr); \
    if (!_bgen_try_##var) return ::cpp11::make_unexpected(_bgen_try_##var.error()); \
    type var = std::move(*_bgen_try_##var)

#define BGEN11_ENSURE(cond, code, msg) \
    do { \
        if (!(cond)) return ::cpp11::make_unexpected(BGEN11_ERROR((code), (msg))); \
    } while (0)

#endif
