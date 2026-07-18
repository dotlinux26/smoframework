#pragma once

#include "bootstrap_snapshot.hpp"
#include "core/types.hpp"
#include "core/errors/error.hpp"
#include "core/authority/authority.hpp"
#include "core/mesh/mesh_manager.hpp"
#include "core/governance/governance.hpp"
#include "core/recovery/crl.hpp"
#include "core/network/packet_dispatcher.hpp"

#include <cstdint>
#include <vector>
#include <array>

namespace smo::bootstrap {

// ── Protocol constants ────────────────────────────────────────────────

inline constexpr uint8_t kProtocolVersion = 1;

// Two-level namespace (RFC 0020)
inline constexpr uint8_t kNamespaceBootstrap = 0x05;

// Message IDs within Bootstrap namespace
inline constexpr uint16_t kMsgBootstrapRequest  = 0x0001;
inline constexpr uint16_t kMsgBootstrapResponse = 0x0002;

// Combined opcode_id for Packet (namespace + message_id << 8)
inline constexpr uint32_t kOpcodeBootstrapRequest  =
    static_cast<uint32_t>(kNamespaceBootstrap) |
    (static_cast<uint32_t>(kMsgBootstrapRequest) << 8);
inline constexpr uint32_t kOpcodeBootstrapResponse =
    static_cast<uint32_t>(kNamespaceBootstrap) |
    (static_cast<uint32_t>(kMsgBootstrapResponse) << 8);

// ── BootstrapRequest ──────────────────────────────────────────────────

struct BootstrapRequest {
    uint8_t  version = kProtocolVersion;
    std::array<uint8_t, 8> nonce{};
    std::string node_id;
    std::string cert_fingerprint; // optional

    Bytes encode_cbor() const;
    static Result<BootstrapRequest> decode_cbor(BytesView data);
};

// ── BootstrapResponse ─────────────────────────────────────────────────

struct BootstrapResponse {
    uint8_t  version = kProtocolVersion;
    std::array<uint8_t, 8> nonce{}; // echoes request nonce
    BootstrapSnapshot snapshot;

    Bytes encode_cbor() const;
    static Result<BootstrapResponse> decode_cbor(BytesView data);
};

// ── Handler ───────────────────────────────────────────────────────────

// Assemble a BootstrapResponse for the given request.
// Uses live data from authority, mesh_manager, governance, and CRL.
Result<BootstrapResponse> handle_bootstrap_request(
    const BootstrapRequest& req,
    MeshManager& mesh_mgr,
    authority::MeshAuthority& authority,
    GovernanceEngine* governance,
    recovery::CRL* crl);

// Register the bootstrap request handler on a PacketDispatcher.
// After registration, incoming BOOTSTRAP_REQUEST packets are automatically
// handled: snapshot assembled, encoded as CBOR, sent back as BOOTSTRAP_RESPONSE.
void register_bootstrap_handler(
    network::PacketDispatcher& dispatcher,
    MeshManager& mesh_mgr,
    authority::MeshAuthority& authority,
    GovernanceEngine* governance,
    recovery::CRL* crl,
    hl::Transport* transport);

} // namespace smo::bootstrap
