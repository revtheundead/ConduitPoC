// SPDX-License-Identifier: MIT
// Conduit CORBA Adaptor - Command Receiver Servant Implementation (C++11)

#include "adaptor/command_servant.hpp"

#include <ace/Log_Msg.h>

#include <utility>

namespace adaptor {

CommandReceiverServant::CommandReceiverServant() {}
CommandReceiverServant::~CommandReceiverServant() {}

CorbaAdaptor::CommandResult CommandReceiverServant::execute_command(
    const char* command,
    const CorbaAdaptor::OctetSeq& params) {
    std::lock_guard<std::mutex> lock(mu_);
    std::string cmd_name(command);

    std::map<std::string, CommandHandler>::iterator it = cmd_handlers_.find(cmd_name);
    if (it != cmd_handlers_.end()) {
        cpp11::span<const uint8_t> param_span(params.get_buffer(), params.length());
        return it->second(cmd_name, param_span);
    }

    if (default_cmd_handler_) {
        cpp11::span<const uint8_t> param_span(params.get_buffer(), params.length());
        return default_cmd_handler_(cmd_name, param_span);
    }

    CorbaAdaptor::CommandResult result;
    result.status  = CorbaAdaptor::CMD_REJECTED;
    result.message = CORBA::string_dup("unknown command");
    return result;
}

CorbaAdaptor::CommandResult CommandReceiverServant::query(const char* name) {
    std::lock_guard<std::mutex> lock(mu_);
    std::string query_name(name);

    std::map<std::string, QueryHandler>::iterator it = query_handlers_.find(query_name);
    if (it != query_handlers_.end()) return it->second(query_name);
    if (default_query_handler_)     return default_query_handler_(query_name);

    CorbaAdaptor::CommandResult result;
    result.status  = CorbaAdaptor::CMD_REJECTED;
    result.message = CORBA::string_dup("unknown query");
    return result;
}

void CommandReceiverServant::request_shutdown() {
    std::lock_guard<std::mutex> lock(mu_);
    if (shutdown_cb_) shutdown_cb_();
}

void CommandReceiverServant::register_command(const std::string& name, CommandHandler handler) {
    std::lock_guard<std::mutex> lock(mu_);
    cmd_handlers_[name] = std::move(handler);
}

void CommandReceiverServant::register_query(const std::string& name, QueryHandler handler) {
    std::lock_guard<std::mutex> lock(mu_);
    query_handlers_[name] = std::move(handler);
}

void CommandReceiverServant::set_default_command_handler(CommandHandler handler) {
    std::lock_guard<std::mutex> lock(mu_);
    default_cmd_handler_ = std::move(handler);
}

void CommandReceiverServant::set_default_query_handler(QueryHandler handler) {
    std::lock_guard<std::mutex> lock(mu_);
    default_query_handler_ = std::move(handler);
}

void CommandReceiverServant::set_shutdown_callback(ShutdownCallback cb) {
    std::lock_guard<std::mutex> lock(mu_);
    shutdown_cb_ = std::move(cb);
}

} // namespace adaptor
