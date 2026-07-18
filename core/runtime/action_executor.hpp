#pragma once

#include "runtime_types.hpp"
#include "protocol/packet/packet.h"
#include "transport/transport.h"
#include "event_bus.hpp"

#include <cstdint>
#include <functional>

namespace smo::runtime {

// ActionExecutor: dispatches NextActions (RFC 0041 §3)
//
// For Sprint 37:
//   - ActionDispatchMessage → build response Packet, send via transport
//   - ActionEmitEvent → publish to EventBus
//   - All other actions → TODO (logged, not implemented)
//
// Executor is synchronous (inline, no thread pool).
class ActionExecutor {
public:
    using SendResponse = std::function<Result<void>(Packet&&)>;

    ActionExecutor(SendResponse send_fn, EventBus* event_bus = nullptr)
        : send_response_(std::move(send_fn))
        , event_bus_(event_bus) {}

    // Execute a single NextAction.
    // For DispatchMessage: sends response via send_response_ callback.
    // For EmitEvent: publishes to EventBus.
    Result<void> execute(const NextAction& action, const Packet& original_pkt);

private:
    SendResponse send_response_;
    EventBus* event_bus_ = nullptr;

    Result<void> on_dispatch_message(const ActionDispatchMessage& msg,
                                      const Packet& original_pkt);
    Result<void> on_emit_event(const ActionEmitEvent& event);
};

} // namespace smo::runtime
