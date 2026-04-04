// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Command Receiver Servant
//
// Implements the CommandReceiver CORBA interface.  External systems call
// execute_command() / query() to interact with the adaptor.  Commands
// are dispatched to registered handlers by name.

#pragma once

#include <CorbaAdaptorS.h>

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace adaptor {

// ============================================================================
// Command handler signature
// ============================================================================

/// A command handler receives the command name, binary params, and returns
/// a status + message pair.
using CommandHandler = std::function<
    CorbaAdaptor::CommandResult(const std::string& command,
                                std::span<const uint8_t> params)>;

/// A query handler receives the query name and returns status + message.
using QueryHandler = std::function<
    CorbaAdaptor::CommandResult(const std::string& name)>;

// ============================================================================
// CommandReceiverServant
// ============================================================================

class CommandReceiverServant : public POA_CorbaAdaptor::CommandReceiver {
public:
    CommandReceiverServant();
    ~CommandReceiverServant() override;

    CommandReceiverServant(const CommandReceiverServant&) = delete;
    CommandReceiverServant& operator=(const CommandReceiverServant&) = delete;

    // --- CORBA interface ----------------------------------------------------

    CorbaAdaptor::CommandResult execute_command(
        const char* command,
        const CorbaAdaptor::OctetSeq& params) override;

    CorbaAdaptor::CommandResult query(const char* name) override;

    void request_shutdown() override;

    // --- Local API ----------------------------------------------------------

    /// Register a handler for a specific command name.
    void register_command(const std::string& name, CommandHandler handler);

    /// Register a handler for a specific query name.
    void register_query(const std::string& name, QueryHandler handler);

    /// Register a catch-all command handler for unrecognized commands.
    void set_default_command_handler(CommandHandler handler);

    /// Register a catch-all query handler for unrecognized queries.
    void set_default_query_handler(QueryHandler handler);

    /// Set the callback invoked when request_shutdown() is called.
    using ShutdownCallback = std::function<void()>;
    void set_shutdown_callback(ShutdownCallback cb);

private:
    mutable std::mutex                              mu_;
    std::unordered_map<std::string, CommandHandler>  cmd_handlers_;
    std::unordered_map<std::string, QueryHandler>    query_handlers_;
    CommandHandler                                   default_cmd_handler_;
    QueryHandler                                     default_query_handler_;
    ShutdownCallback                                 shutdown_cb_;
};

} // namespace adaptor
