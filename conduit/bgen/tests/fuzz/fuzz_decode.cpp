// SPDX-License-Identifier: MIT
// Conduit - Fuzz harness for decode paths
//
// Feeds random bytes to decode_bytes() for various generated message types.
// Should never crash -- all invalid input must be handled gracefully.
//
// Build:
//   cmake -DCONDUIT_BUILD_FUZZ=ON -DCMAKE_CXX_COMPILER=clang++ ..
//   cmake --build . --target fuzz_decode
//
// Run:
//   ./fuzz_decode corpus_dir/ -max_total_time=300

#include <cstdint>
#include <cstddef>
#include <span>

// Include generated message headers from test fixtures
#include "all_types/messages.hpp"
#include "asterix/messages.hpp"
#include "sentry_link/messages.hpp"
#include "session_protocol/messages.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const auto span = std::span<const uint8_t>(data, size);

    // Try decoding as various message types -- none should crash.
    // decode_bytes returns std::expected; we discard the result.

    // Simple flat message with many field types
    (void)all_types::AllTypesMessage::decode_bytes(span);

    // ASTERIX bitmap-based records
    (void)asterix::Cat001Record::decode_bytes(span);

    // Session-wrapped packet with choice dispatch
    (void)sentry_link::Frame::decode_bytes(span);

    // Session protocol packet
    (void)session_test::Packet::decode_bytes(span);

    return 0;
}
