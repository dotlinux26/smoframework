#pragma once

#include <cstdint>
#include <string_view>

namespace smo {

// Schema versioning for wire compatibility.
// Every node advertises its protocol version during heartbeat.

struct ProtocolVersion {
    uint8_t major{1};
    uint8_t minor{0};
};

enum class MessageType : uint8_t {
    CONTRACT_PROPOSAL   = 0x01,
    CONTRACT_ACCEPT     = 0x02,
    CONTRACT_REJECT     = 0x03,
    CONTRACT_RESULT     = 0x04,
    WITNESS_REQUEST     = 0x10,
    WITNESS_RESPONSE    = 0x11,
    HEARTBEAT           = 0x20,
    SESSION_OPEN        = 0x30,
    SESSION_CLOSE       = 0x31,
    CAPABILITY_GRANT    = 0x40,
    CAPABILITY_REVOKE   = 0x41,
};

const char* to_string(MessageType mt) noexcept;
bool is_control_message(MessageType mt) noexcept;
bool is_contract_message(MessageType mt) noexcept;
bool is_witness_message(MessageType mt) noexcept;

ProtocolVersion current_protocol_version() noexcept;
bool is_compatible(const ProtocolVersion& peer, const ProtocolVersion& local) noexcept;

} // namespace smo
