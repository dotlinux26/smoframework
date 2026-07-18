#pragma once

#include "runtime_types.hpp"
#include "protocol/packet/packet.h"
#include "transport/transport.h"

#include <cstdint>
#include <functional>

namespace smo::runtime {

// ActionExecutor: dispatches NextActions (RFC 0041 §3)
//
// For Sprint 37:
//   - ActionDispatchMessage → build response Packet, send via transport
//   - All other actions → TODO (logged, not implemented)
//
// Executor is synchronous (inline, no thread pool).
class ActionExecutor {
public:
    using SendResponse = std::function<Result<void>(Packet&&)>;

    explicit ActionExecutor(SendResponse send_fn)
        : send_response_(std::move(send_fn)) {}

    // Execute a single NextAction.
    // For DispatchMessage: sends response via send_response_ callback.
    Result<void> execute(const NextAction& action, const Packet& original_pkt);

private:
    SendResponse send_response_;

    Result<void> on_dispatch_message(const ActionDispatchMessage& msg,
                                      const Packet& original_pkt);
};

} // namespace smo::runtime
