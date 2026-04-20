// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - CORBA Raw-Data Peer (C++11)

#ifndef ADAPTOR_CORBA_PEER_HPP
#define ADAPTOR_CORBA_PEER_HPP

#include <cstdint>
#include <memory>
#include <string>

#include "compat11/span.hpp"

#include "adaptor/types.hpp"

#include <CorbaAdaptorS.h>
#include <tao/ORB.h>
#include <tao/PortableServer/PortableServer.h>

namespace adaptor {

struct CorbaPeerConfig {
    std::string channel_ior;
    bool        auto_reconnect;
    uint32_t    initial_delay_ms;
    uint32_t    max_delay_ms;
    double      backoff_multiplier;
    uint32_t    max_attempts;

    CorbaPeerConfig()
        : auto_reconnect(true),
          initial_delay_ms(1000),
          max_delay_ms(30000),
          backoff_multiplier(2.0),
          max_attempts(0) {}
};

class CorbaPeer {
public:
    CorbaPeer(CORBA::ORB_ptr orb,
              PortableServer::POA_ptr poa,
              const CorbaPeerConfig& config);
    ~CorbaPeer();

    CorbaPeer(const CorbaPeer&);
    CorbaPeer& operator=(const CorbaPeer&);

    void set_data_callback(RawBytesCallback cb);
    void set_state_callback(PeerStateCallback cb);

    void start();
    void stop();

    bool send(cpp11::span<const uint8_t> data);
    bool is_connected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace adaptor

#endif
