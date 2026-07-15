#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <span>

#include "../../core/errors/error.hpp"
#include "../../core/opcode/opcode.h"

namespace smo {

// §VI.1 — Packet Format
// Wire format: HEADER | SESSION_ID | INTENT_ID | OPCODE_ID | TIMESTAMP
//              | NONCE | PAYLOAD | SIGNATURE
//
// Zero-copy invariant: packet data is parsed in-place from a wire buffer.

struct PacketHeader {
    uint8_t  version{1};
    uint8_t  flags{0};
    uint16_t payload_len{0};
    uint16_t signature_scheme{0};  // 0 = Ed25519
} __attribute__((packed));

static_assert(sizeof(PacketHeader) == 6);

struct Packet {
    PacketHeader        header;
    std::array<uint8_t, 16> session_id{};   // SESSION ID
    std::array<uint8_t, 16> intent_id{};     // INTENT ID
    uint32_t            opcode_id{0};        // OPCODE ID
    int64_t             timestamp{0};        // TIMESTAMP  (unix ms)
    std::array<uint8_t, 8>  nonce{};         // NONCE
    std::vector<uint8_t>    payload;         // PAYLOAD   (variable)
    std::array<uint8_t, 64> signature{};     // SIGNATURE (Ed25519)
};

// Parse a wire buffer into a Packet without copying the payload.
// Returns a Packet whose payload span references the input buffer.
// Returns Protocol error on malformed input.
Result<Packet> packet_from_buffer(std::span<const uint8_t> wire);

// Serialize a packet into a writer callback.
// Format: header | session_id | intent_id | opcode | timestamp | nonce
//         | payload_len | payload | signature
Result<void> packet_to_buffer(const Packet& pkt, std::vector<uint8_t>& out);

} // namespace smo
