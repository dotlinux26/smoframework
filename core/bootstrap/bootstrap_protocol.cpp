#include "bootstrap_protocol.hpp"
#include "core/opcode/opcode.h"
#include "core/crypto/suite.hpp"
#include "core/errors/error.hpp"

#include <cstring>

#include <cstring>
#include <random>

namespace smo::bootstrap {

// ── CBOR key constants for BootstrapRequest/Response ──────────────────

namespace {
    constexpr uint64_t K_REQ_VERSION  = 1;
    constexpr uint64_t K_REQ_NONCE    = 2;
    constexpr uint64_t K_REQ_NODE_ID  = 3;
    constexpr uint64_t K_REQ_CERT_FP  = 4;

    constexpr uint64_t K_RESP_VERSION = 1;
    constexpr uint64_t K_RESP_NONCE   = 2;
    constexpr uint64_t K_RESP_SNAPSHOT = 3;
}

// ── BootstrapRequest ──────────────────────────────────────────────────

Bytes BootstrapRequest::encode_cbor() const {
    cbor::Encoder enc;
    enc.encode_map(cert_fingerprint.empty() ? 3 : 4);
    enc.encode_uint(K_REQ_VERSION);
    enc.encode_uint(version);
    enc.encode_uint(K_REQ_NONCE);
    enc.encode_bytes(BytesView(nonce.data(), nonce.size()));
    enc.encode_uint(K_REQ_NODE_ID);
    enc.encode_string(node_id);
    if (!cert_fingerprint.empty()) {
        enc.encode_uint(K_REQ_CERT_FP);
        enc.encode_string(cert_fingerprint);
    }
    return enc.take();
}

Result<BootstrapRequest> BootstrapRequest::decode_cbor(BytesView data) {
    cbor::Decoder dec(data);
    BootstrapRequest req;
    auto map_sz = dec.decode_map_size();
    if (!map_sz) return map_sz.error();

    for (size_t i = 0; i < map_sz.value(); ++i) {
        auto key = dec.decode_uint();
        if (!key) return key.error();
        switch (key.value()) {
            case K_REQ_VERSION: {
                auto v = dec.decode_uint();
                if (!v) return v.error();
                req.version = static_cast<uint8_t>(v.value());
                break;
            }
            case K_REQ_NONCE: {
                auto v = dec.decode_bytes();
                if (!v) return v.error();
                if (v.value().size() == 8) {
                    std::memcpy(req.nonce.data(), v.value().data(), 8);
                }
                break;
            }
            case K_REQ_NODE_ID: {
                auto v = dec.decode_string();
                if (!v) return v.error();
                req.node_id = std::move(v.value());
                break;
            }
            case K_REQ_CERT_FP: {
                auto v = dec.decode_string();
                if (!v) return v.error();
                req.cert_fingerprint = std::move(v.value());
                break;
            }
            default: {
                auto r = dec.skip();
                if (!r) return r.error();
                break;
            }
        }
    }

    if (req.node_id.empty()) {
        return SMO_ERR_PROTOCOL(900, Error, NoRetry, None,
                                "BootstrapRequest missing node_id");
    }

    return req;
}

// ── BootstrapResponse ─────────────────────────────────────────────────

Bytes BootstrapResponse::encode_cbor() const {
    cbor::Encoder enc;
    enc.encode_map(3);
    enc.encode_uint(K_RESP_VERSION);
    enc.encode_uint(version);
    enc.encode_uint(K_RESP_NONCE);
    enc.encode_bytes(BytesView(nonce.data(), nonce.size()));
    enc.encode_uint(K_RESP_SNAPSHOT);
    auto snap_bytes = snapshot.encode_cbor();
    enc.encode_bytes(snap_bytes);
    return enc.take();
}

Result<BootstrapResponse> BootstrapResponse::decode_cbor(BytesView data) {
    cbor::Decoder dec(data);
    BootstrapResponse resp;
    auto map_sz = dec.decode_map_size();
    if (!map_sz) return map_sz.error();

    for (size_t i = 0; i < map_sz.value(); ++i) {
        auto key = dec.decode_uint();
        if (!key) return key.error();
        switch (key.value()) {
            case K_RESP_VERSION: {
                auto v = dec.decode_uint();
                if (!v) return v.error();
                resp.version = static_cast<uint8_t>(v.value());
                break;
            }
            case K_RESP_NONCE: {
                auto v = dec.decode_bytes();
                if (!v) return v.error();
                if (v.value().size() == 8) {
                    std::memcpy(resp.nonce.data(), v.value().data(), 8);
                }
                break;
            }
            case K_RESP_SNAPSHOT: {
                auto v = dec.decode_bytes();
                if (!v) return v.error();
                auto snap = BootstrapSnapshot::decode_cbor(v.value());
                if (!snap) return snap.error();
                resp.snapshot = std::move(snap.value());
                break;
            }
            default: {
                auto r = dec.skip();
                if (!r) return r.error();
                break;
            }
        }
    }
    return resp;
}

// ── Handler ───────────────────────────────────────────────────────────

Result<BootstrapResponse> handle_bootstrap_request(
    const BootstrapRequest& req,
    MeshManager& mesh_mgr,
    authority::MeshAuthority& authority,
    GovernanceEngine* governance,
    recovery::CRL* crl)
{
    BootstrapResponse resp;
    resp.version = kProtocolVersion;
    resp.nonce = req.nonce;

    // Get mesh context
    auto mesh_res = mesh_mgr.get_current_mesh();
    if (!mesh_res) {
        return SMO_ERR_PROTOCOL(900, Error, NoRetry, None,
                                "No active mesh for bootstrap");
    }
    auto& ctx = mesh_res.value();
    auto& cfg = ctx->config;

    auto& snap = resp.snapshot;
    snap.mesh_id = cfg.mesh_id;
    snap.mesh_state = "Online";
    snap.epoch = cfg.epoch;

    // Policy / governance versions
    snap.policy_version = cfg.epoch;
    snap.governance_version = cfg.epoch;

    // Cipher suite
    snap.cipher_suite = static_cast<uint8_t>(cfg.cipher_suite_id);

    // Supported opcodes
    snap.opcodes = {
        static_cast<uint8_t>(Opcode::LS),
        static_cast<uint8_t>(Opcode::PUT),
        static_cast<uint8_t>(Opcode::GET),
        static_cast<uint8_t>(Opcode::EXEC),
        static_cast<uint8_t>(Opcode::REVOKE_CERT),
        static_cast<uint8_t>(Opcode::EPOCH_INCREMENT),
        static_cast<uint8_t>(Opcode::RECOVERY_SESSION),
        static_cast<uint8_t>(Opcode::CRL_SYNC)
    };

    // Authorities from NodeRegistry
    auto nodes_res = authority.registry().list_nodes(cfg.mesh_id);
    if (nodes_res) {
        for (auto& node : nodes_res.value()) {
            if (node.role == "Authority" || node.role == "Root") {
                AuthorityInfo info;
                info.node_id = node.node_id_hex;
                // Use bootstrap_endpoints as fallback for authority endpoints
                snap.authorities.push_back(std::move(info));
            }
        }
    }

    // Seeds
    snap.seeds = cfg.bootstrap_endpoints;

    // CRL
    if (crl) {
        snap.crl_count = static_cast<uint32_t>(crl->count());
        // Compute Blake3 digest of CRL entries
        // For now, use epoch-based version tracking
        std::memset(snap.crl_digest.data(), 0, 32);
        if (crl->count() > 0) {
            // Placeholder: set digest to epoch (will be replaced with real hash)
            uint64_t epoch_val = static_cast<uint64_t>(cfg.epoch);
            std::memcpy(snap.crl_digest.data(), &epoch_val, sizeof(epoch_val));
        }
    }

    // Genesis manifest (minimal CBOR)
    {
        cbor::Encoder man_enc;
        man_enc.encode_map(3);
        man_enc.encode_uint(1); man_enc.encode_string(cfg.mesh_id);
        man_enc.encode_uint(2); man_enc.encode_string(cfg.root_pubkey);
        man_enc.encode_uint(3); man_enc.encode_uint(static_cast<uint64_t>(cfg.epoch));
        snap.genesis_manifest_cbor = man_enc.take();
    }

    // Health
    {
        uint32_t total = static_cast<uint32_t>(snap.authorities.size());
        uint32_t online = 0;
        if (nodes_res) {
            for (auto& node : nodes_res.value()) {
                if ((node.role == "Authority" || node.role == "Root") &&
                    (node.status == "active" || node.status == "online")) {
                    online++;
                }
            }
        }
        uint32_t quorum = total > 0 ? (total * 2 + 2) / 3 : 1;
        snap.health.operational = online >= quorum;

        if (!snap.health.operational) {
            snap.health.level = "Critical";
        } else if (online < total) {
            snap.health.level = "Warning";
        } else {
            snap.health.level = "Healthy";
        }
    }

    // Governance — active proposals
    if (governance) {
        snap.active_proposals = static_cast<uint32_t>(governance->pending().size());
    }

    return resp;
}

// ── Dispatcher registration ──────────────────────────────────────────

void register_bootstrap_handler(
    network::PacketDispatcher& dispatcher,
    MeshManager& mesh_mgr,
    authority::MeshAuthority& authority,
    GovernanceEngine* governance,
    recovery::CRL* crl,
    hl::Transport* transport)
{
    dispatcher.register_handler(
        kOpcodeBootstrapRequest,
        [&mesh_mgr, &authority, governance, crl](Packet&& pkt, const hl::Endpoint& remote, hl::Transport& t) -> Result<void> {
            (void)remote;
            auto req = BootstrapRequest::decode_cbor(pkt.payload);
            if (!req) return req.error();

            auto resp = handle_bootstrap_request(req.value(), mesh_mgr, authority, governance, crl);
            if (!resp) return resp.error();

            auto resp_bytes = resp.value().encode_cbor();

            Packet resp_pkt;
            resp_pkt.header.version = 1;
            resp_pkt.opcode_id = kOpcodeBootstrapResponse;
            resp_pkt.session_id = pkt.session_id;
            resp_pkt.intent_id = pkt.intent_id;
            resp_pkt.timestamp = pkt.timestamp;
            resp_pkt.nonce = pkt.nonce;
            resp_pkt.payload = std::move(resp_bytes);

            std::error_code ec = t.send(std::move(resp_pkt), remote);
            if (ec) {
                return SMO_ERR_TRANSPORT(static_cast<int>(ec.value()),
                                          Error, RetrySafe, None,
                                          "Failed to send bootstrap response");
            }
            return {};
        }
    );

    (void)transport;
}

} // namespace smo::bootstrap
