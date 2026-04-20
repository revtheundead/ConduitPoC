#ifndef BGEN11_SESSION_TRAITS_HPP
#define BGEN11_SESSION_TRAITS_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include "../compat11/span.hpp"
#include "../compat11/any.hpp"
#include "../compat11/string_view.hpp"
#include "error.hpp"

namespace bgen11 {
namespace traits {

struct DecodedMessage {
    uint64_t type_id;
    std::string type_name;
    cpp11::any payload;
    std::vector<uint8_t> raw;
};

struct EncodeResult {
    std::vector<uint8_t> bytes;
    std::vector<std::pair<std::string, std::string> > auto_fields;
};

class ISession {
public:
    virtual ~ISession() {}

    virtual Result<std::vector<DecodedMessage> >
    decode_frame(cpp11::span<const uint8_t> data) = 0;

    virtual Result<EncodeResult>
    encode_wrap(uint64_t type_id, const cpp11::any& payload) = 0;

    virtual Result<EncodeResult>
    encode_batch(uint64_t /*type_id*/,
                 cpp11::span<const cpp11::any> /*payloads*/) {
        return cpp11::make_unexpected(Error(ErrorCode_NotImplemented,
            "encode_batch not supported by this session"));
    }

    virtual cpp11::span<const uint8_t> sync_pattern() const = 0;
    virtual std::size_t min_frame_header_size() const = 0;
    virtual std::size_t extract_frame_length(cpp11::span<const uint8_t> header) const = 0;
    virtual cpp11::span<const uint64_t> leaf_type_ids() const = 0;
    virtual cpp11::string_view type_name(uint64_t type_id) const = 0;
    virtual bool is_receive_only(uint64_t type_id) const = 0;
    virtual cpp11::string_view protocol_name() const = 0;
    virtual std::string format_message(uint64_t type_id, const cpp11::any& payload) const = 0;
    virtual std::string format_outbound(
        uint64_t type_id, const cpp11::any& payload,
        cpp11::span<const std::pair<std::string, std::string> > auto_fields) const = 0;
    virtual void reset() = 0;
};

}
}

#endif
