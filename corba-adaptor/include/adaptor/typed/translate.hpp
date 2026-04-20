// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - BMDL <-> CORBA Type Translation
//
// The bgen-generated C++ structs (in namespaces `tcp_peer` and `corba_peer`)
// use plain C++ types, accessor methods, and wrapper classes for
// enums/flags.  The CORBA IDL stubs (in namespace `CorbaAdaptor`) use
// TAO-generated structs that live on the wire.  This header provides
// `to_corba(...)` overloads that convert each "receive" BMDL message the
// adaptor cares about into its CORBA counterpart before publishing to
// consumers.
//
// All conversions are trivial field copies — no heap allocation beyond
// whatever the CORBA sequence types do internally — so they are defined
// inline and kept header-only.
//
// The IDL uses signed types (long, long long) for portability.  The bgen
// types use unsigned C++ types (uint32_t, uint64_t).  Explicit casts are
// applied where needed.

#ifndef ADAPTOR_TYPED_TRANSLATE_HPP
#define ADAPTOR_TYPED_TRANSLATE_HPP

#include <CorbaAdaptorC.h>

#include "tcp-peer/tcp_peer.hpp"
#include "tcp-peer/messages.hpp"
#include "corba-peer/corba_peer.hpp"
#include "corba-peer/messages.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace adaptor { namespace translate {

// ============================================================================
// Helpers
// ============================================================================

/// Copy a fixed-length byte array into a CORBA OctetSeq, truncating to the
/// caller-supplied logical length (e.g. `data_length` field).
template <std::size_t N>
inline void copy_bytes(CorbaAdaptor::OctetSeq& out,
                       const std::array<std::uint8_t, N>& in,
                       std::size_t logical_length) {
    const CORBA::ULong len = static_cast<CORBA::ULong>(std::min<std::size_t>(logical_length, N));
    out.length(len);
    for (CORBA::ULong i = 0; i < len; ++i) {
        out[i] = in[i];
    }
}

// ============================================================================
// TCP peer — receive-side messages
// ============================================================================

inline CorbaAdaptor::HeartbeatMsg to_corba(const tcp_peer::Heartbeat& src) {
    CorbaAdaptor::HeartbeatMsg dst;
    dst.sequence  = static_cast<CORBA::Long>(src.sequence());
    dst.timestamp = static_cast<CORBA::LongLong>(src.timestamp());
    return dst;
}

inline CorbaAdaptor::StatusReportMsg to_corba(const tcp_peer::StatusReport& src) {
    CorbaAdaptor::StatusReportMsg dst;
    dst.status      = static_cast<CORBA::Octet>(src.status());
    dst.flags       = static_cast<CORBA::UShort>(src.flags().raw());
    dst.uptime_secs = static_cast<CORBA::Long>(src.uptime_secs());
    dst.error_count = src.error_count();
    return dst;
}

inline CorbaAdaptor::DataPayloadMsg to_corba(const tcp_peer::DataPayload& src) {
    CorbaAdaptor::DataPayloadMsg dst;
    dst.channel_id  = src.channel_id();
    dst.sequence    = static_cast<CORBA::Long>(src.sequence());
    dst.data_length = src.data_length();
    copy_bytes(dst.data, src.data(), src.data_length());
    return dst;
}

inline CorbaAdaptor::CommandResponseMsg to_corba(const tcp_peer::CommandResponse& src) {
    CorbaAdaptor::CommandResponseMsg dst;
    dst.command_id  = src.command_id();
    dst.result_code = static_cast<CORBA::Octet>(src.result_code());
    copy_bytes(dst.result_data, src.result_data(), src.result_length());
    return dst;
}

// ============================================================================
// CORBA peer — receive-side messages
// ============================================================================

inline CorbaAdaptor::TelemetryMsg to_corba(const corba_peer::TelemetryRecord& src) {
    CorbaAdaptor::TelemetryMsg dst;
    dst.source_id = src.source_id();
    dst.sequence  = static_cast<CORBA::Long>(src.sequence());
    dst.timestamp = static_cast<CORBA::LongLong>(src.timestamp());
    dst.channel   = src.channel();
    dst.value     = src.value();
    return dst;
}

inline CorbaAdaptor::EventMsg to_corba(const corba_peer::EventRecord& src) {
    CorbaAdaptor::EventMsg dst;
    dst.source_id  = src.source_id();
    dst.timestamp  = static_cast<CORBA::LongLong>(src.timestamp());
    dst.event_code = src.event_code();
    dst.severity   = static_cast<CORBA::Octet>(src.severity());
    copy_bytes(dst.data, src.data(), src.data_len());
    return dst;
}

inline CorbaAdaptor::AlarmMsg to_corba(const corba_peer::AlarmRecord& src) {
    CorbaAdaptor::AlarmMsg dst;
    dst.source_id = src.source_id();
    dst.timestamp = static_cast<CORBA::LongLong>(src.timestamp());
    dst.alarm_id  = src.alarm_id();
    dst.severity  = static_cast<CORBA::Octet>(src.severity());
    dst.flags     = static_cast<CORBA::Octet>(src.flags().raw());
    return dst;
}

} } // namespace adaptor::translate

#endif
