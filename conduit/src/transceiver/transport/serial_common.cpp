// SPDX-License-Identifier: MIT
// Conduit - Serial Transport (shared validation)

#include <conduit/transceiver/transport/serial.hpp>
#include <format>

namespace conduit::transceiver::transport {

VoidResult validate_serial_config(const SerialConfig& config) {
    CONDUIT_ENSURE(!config.port.empty(), ErrorCode::InvalidConfig,
                   "Serial port name must not be empty");

    CONDUIT_ENSURE(config.baud_rate > 0, ErrorCode::InvalidConfig,
                   "Baud rate must be positive");

    CONDUIT_ENSURE(config.data_bits >= 5 && config.data_bits <= 8,
                   ErrorCode::InvalidConfig,
                   std::format("Data bits must be 5-8, got {}",
                               config.data_bits));

    CONDUIT_ENSURE(config.recv_buffer_size > 0, ErrorCode::InvalidConfig,
                   "Receive buffer size must be positive");

    return {};
}

} // namespace conduit::transceiver::transport
