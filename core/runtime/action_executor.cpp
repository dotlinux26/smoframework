#include "action_executor.hpp"

#include "core/runtime/runtime_types.hpp"
#include "protocol/packet/packet.h"

namespace smo::runtime {

Result<void> ActionExecutor::execute(const NextAction& action,
                                      const Packet& original_pkt) {
    return std::visit(overloaded{
        [&](const ActionDispatchMessage& msg) {
            return on_dispatch_message(msg, original_pkt);
        },
        [&](const ActionDispatchContract&) {
            // TODO: schedule via Scheduler (Sprint 37+)
            return Result<void>{};
        },
        [&](const ActionScheduleRetry&) {
            // TODO: implement retry queue
            return Result<void>{};
        },
        [&](const ActionEmitEvent&) {
            // TODO: wire EventBus
            return Result<void>{};
        },
        [&](const ActionStoreContext&) {
            // Applied inline by contract — no action needed
            return Result<void>{};
        },
        [&](const ActionSpawnPlan&) {
            // TODO: resolve and spawn sub-plan
            return Result<void>{};
        },
        [&](const ActionNotify&) {
            // TODO: deliver notification
            return Result<void>{};
        },
        [&](const ActionCompensate&) {
            // TODO: trigger compensation
            return Result<void>{};
        },
        [&](const ActionAbort&) {
            // TODO: abort execution
            return Result<void>{};
        },
    }, action);
}

Result<void> ActionExecutor::on_dispatch_message(
    const ActionDispatchMessage& msg,
    const Packet& original_pkt)
{
    // Build response Packet from the original request packet
    Packet resp;
    resp.header = original_pkt.header;
    resp.opcode_id = original_pkt.opcode_id;
    resp.session_id = original_pkt.session_id;
    resp.intent_id = original_pkt.intent_id;
    resp.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    resp.payload = msg.data;

    return send_response_(std::move(resp));
}

} // namespace smo::runtime
