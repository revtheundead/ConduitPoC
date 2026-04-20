// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Command Receiver Servant (C++11)

#ifndef ADAPTOR_COMMAND_SERVANT_HPP
#define ADAPTOR_COMMAND_SERVANT_HPP

#include <CorbaAdaptorS.h>

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>

#include "compat11/span.hpp"

namespace adaptor {

typedef std::function<
    CorbaAdaptor::CommandResult(const std::string&,
                                cpp11::span<const uint8_t>)>
    CommandHandler;

typedef std::function<
    CorbaAdaptor::CommandResult(const std::string&)>
    QueryHandler;

class CommandReceiverServant : public POA_CorbaAdaptor::CommandReceiver {
public:
    typedef std::function<void()> ShutdownCallback;

    CommandReceiverServant();
    virtual ~CommandReceiverServant();

    CommandReceiverServant(const CommandReceiverServant&);
    CommandReceiverServant& operator=(const CommandReceiverServant&);

    virtual CorbaAdaptor::CommandResult execute_command(
        const char* command,
        const CorbaAdaptor::OctetSeq& params);

    virtual CorbaAdaptor::CommandResult query(const char* name);

    virtual void request_shutdown();

    void register_command(const std::string& name, CommandHandler handler);
    void register_query(const std::string& name, QueryHandler handler);
    void set_default_command_handler(CommandHandler handler);
    void set_default_query_handler(QueryHandler handler);
    void set_shutdown_callback(ShutdownCallback cb);

private:
    mutable std::mutex                       mu_;
    std::map<std::string, CommandHandler>    cmd_handlers_;
    std::map<std::string, QueryHandler>      query_handlers_;
    CommandHandler                           default_cmd_handler_;
    QueryHandler                             default_query_handler_;
    ShutdownCallback                         shutdown_cb_;
};

} // namespace adaptor

#endif
