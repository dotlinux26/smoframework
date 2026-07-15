#include "schema.h"

#include <string>

namespace smo {

const char* to_string(MessageType mt) noexcept {
    switch (mt) {
        case MessageType::CONTRACT_PROPOSAL:   return "CONTRACT_PROPOSAL";
        case MessageType::CONTRACT_ACCEPT:     return "CONTRACT_ACCEPT";
        case MessageType::CONTRACT_REJECT:     return "CONTRACT_REJECT";
        case MessageType::CONTRACT_RESULT:     return "CONTRACT_RESULT";
        case MessageType::WITNESS_REQUEST:     return "WITNESS_REQUEST";
        case MessageType::WITNESS_RESPONSE:    return "WITNESS_RESPONSE";
        case MessageType::HEARTBEAT:           return "HEARTBEAT";
        case MessageType::SESSION_OPEN:        return "SESSION_OPEN";
        case MessageType::SESSION_CLOSE:       return "SESSION_CLOSE";
        case MessageType::CAPABILITY_GRANT:    return "CAPABILITY_GRANT";
        case MessageType::CAPABILITY_REVOKE:   return "CAPABILITY_REVOKE";
        default:                               return "UNKNOWN";
    }
}

bool is_control_message(MessageType mt) noexcept {
    switch (mt) {
        case MessageType::HEARTBEAT:
        case MessageType::SESSION_OPEN:
        case MessageType::SESSION_CLOSE:
        case MessageType::CAPABILITY_GRANT:
        case MessageType::CAPABILITY_REVOKE:
            return true;
        default:
            return false;
    }
}

bool is_contract_message(MessageType mt) noexcept {
    switch (mt) {
        case MessageType::CONTRACT_PROPOSAL:
        case MessageType::CONTRACT_ACCEPT:
        case MessageType::CONTRACT_REJECT:
        case MessageType::CONTRACT_RESULT:
            return true;
        default:
            return false;
    }
}

bool is_witness_message(MessageType mt) noexcept {
    return mt == MessageType::WITNESS_REQUEST ||
           mt == MessageType::WITNESS_RESPONSE;
}

ProtocolVersion current_protocol_version() noexcept {
    return {1, 0};
}

bool is_compatible(const ProtocolVersion& peer, const ProtocolVersion& local) noexcept {
    return peer.major == local.major;
}

} // namespace smo
