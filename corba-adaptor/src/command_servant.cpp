// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Command Receiver Servant Implementation

#include <adaptor/command_servant.hpp>

#include <ace/Log_Msg.h>

namespace adaptor {

CommandReceiverServant::CommandReceiverServant() = default;
CommandReceiverServant::~CommandReceiverServant() = default;

// --- CORBA interface --------------------------------------------------------

CorbaAdaptor::CommandResult CommandReceiverServant::execute_command(
    const char* command,
    const CorbaAdaptor::OctetSeq& params)
{
    ACE_DEBUG((LM_DEBUG, "CommandReceiver: execute_command('%s', %u bytes)\n",
               command, params.length()));

    std::lock_guard lock(mu_);
    std::string cmd_name(command);

    // Look up specific handler first
    if (auto it = cmd_handlers_.find(cmd_name); it != cmd_handlers_.end()) {
        std::span<const uint8_t> param_span(params.get_buffer(), params.length());
        return it->second(cmd_name, param_span);
    }

    // Fall back to default handler
    if (default_cmd_handler_) {
        std::span<const uint8_t> param_span(params.get_buffer(), params.length());
        return default_cmd_handler_(cmd_name, param_span);
    }

    // No handler found
    CorbaAdaptor::CommandResult result;
    result.status  = CorbaAdaptor::CMD_REJECTED;
    result.message = CORBA::string_dup("unknown command");
    return result;
}

CorbaAdaptor::CommandResult CommandReceiverServant::query(const char* name) {
    ACE_DEBUG((LM_DEBUG, "CommandReceiver: query('%s')\n", name));

    std::lock_guard lock(mu_);
    std::string query_name(name);

    if (auto it = query_handlers_.find(query_name); it != query_handlers_.end()) {
        return it->second(query_name);
    }

    if (default_query_handler_) {
        return default_query_handler_(query_name);
    }

    CorbaAdaptor::CommandResult result;
    result.status  = CorbaAdaptor::CMD_REJECTED;
    result.message = CORBA::string_dup("unknown query");
    return result;
}

void CommandReceiverServant::request_shutdown() {
    ACE_DEBUG((LM_INFO, "CommandReceiver: shutdown requested\n"));

    std::lock_guard lock(mu_);
    if (shutdown_cb_) {
        shutdown_cb_();
    }
}

// --- Local API --------------------------------------------------------------

void CommandReceiverServant::register_command(
    const std::string& name, CommandHandler handler)
{
    std::lock_guard lock(mu_);
    cmd_handlers_[name] = std::move(handler);
}

void CommandReceiverServant::register_query(
    const std::string& name, QueryHandler handler)
{
    std::lock_guard lock(mu_);
    query_handlers_[name] = std::move(handler);
}

void CommandReceiverServant::set_default_command_handler(CommandHandler handler) {
    std::lock_guard lock(mu_);
    default_cmd_handler_ = std::move(handler);
}

void CommandReceiverServant::set_default_query_handler(QueryHandler handler) {
    std::lock_guard lock(mu_);
    default_query_handler_ = std::move(handler);
}

void CommandReceiverServant::set_shutdown_callback(ShutdownCallback cb) {
    std::lock_guard lock(mu_);
    shutdown_cb_ = std::move(cb);
}

} // namespace adaptor
