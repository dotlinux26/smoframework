#include "join_protocol.hpp"

#include "core/bootstrap/cbor.hpp"
#include "core/opcode/opcode.h"
#include "core/crypto/suite.hpp"
#include "core/crypto/impl.hpp"
#include "core/crypto/registry.hpp"
#include "core/enroll/join_token.hpp"
#include "core/errors/error.hpp"
#include "core/authority/authority.hpp"
#include "core/recovery/crl.hpp"
#include "core/certificate/certificate.hpp"

#include <random>
#include <chrono>
#include <cstring>

namespace smo::join {

// ── CBOR key constants ──────────────────────────────────────────────
namespace {
// JoinRequest keys (DISCUSSION_0039 §5.17)
constexpr uint64_t K_REQ_TOKEN     = 1;
constexpr uint64_t K_REQ_CSR       = 2;
constexpr uint64_t K_REQ_TIMESTAMP = 3;
constexpr uint64_t K_REQ_NONCE     = 4;
constexpr uint64_t K_REQ_CSR_HASH  = 5;
constexpr uint64_t K_REQ_SIG       = 6;

// JoinResponse keys (DISCUSSION_0039 §5.1)
constexpr uint64_t K_RESP_CERT     = 1;
constexpr uint64_t K_RESP_MESH_ID  = 2;
constexpr uint64_t K_RESP_MF_DIGEST = 3;
constexpr uint64_t K_RESP_MF_EPOCH = 4;
constexpr uint64_t K_RESP_BOOT_NODES = 5;
constexpr uint64_t K_RESP_NONCE    = 6;

// BootstrapSyncRequest keys (DISCUSSION_0039 §5.2)
constexpr uint64_t K_SYNC_REQ_MESH_ID = 1;
constexpr uint64_t K_SYNC_REQ_NODE_ID = 2;
constexpr uint64_t K_SYNC_REQ_MF_EPOCH = 3;
constexpr uint64_t K_SYNC_REQ_CRL_EPOCH = 4;
constexpr uint64_t K_SYNC_REQ_MEMBER_EPOCH = 5;
constexpr uint64_t K_SYNC_REQ_POLICY_VER = 6;

// BootstrapSyncResponse keys (DISCUSSION_0039 §5.4)
constexpr uint64_t K_SYNC_RESP_MF_DELTA   = 1;
constexpr uint64_t K_SYNC_RESP_MEMBER_DELTA = 2;
constexpr uint64_t K_SYNC_RESP_POLICY_DELTA = 3;
constexpr uint64_t K_SYNC_RESP_CRL_DELTA   = 4;
constexpr uint64_t K_SYNC_RESP_MF_EPOCH    = 5;
constexpr uint64_t K_SYNC_RESP_MEMBER_EPOCH = 6;
constexpr uint64_t K_SYNC_RESP_CRL_EPOCH   = 7;
constexpr uint64_t K_SYNC_RESP_POLICY_VER  = 8;
} // anonymous namespace

// ── JoinRequest ──────────────────────────────────────────────────────
Bytes JoinRequest::encode_cbor() const {
    cbor::Encoder enc;
    int fields = 2; // token + csr
    if (timestamp > 0) fields++;
    if (csr_hash.size() > 0) fields++;
    if (request_signature.size() > 0) fields++;
    fields++; // nonce always present
    enc.encode_map(fields);
    enc.encode_uint(K_REQ_TOKEN); enc.encode_string(token);
    enc.encode_uint(K_REQ_CSR); enc.encode_string(csr_pem);
    if (timestamp > 0) { enc.encode_uint(K_REQ_TIMESTAMP); enc.encode_int(timestamp); }
    enc.encode_uint(K_REQ_NONCE); enc.encode_bytes(BytesView(nonce.data(), nonce.size()));
    if (csr_hash.size() > 0) { enc.encode_uint(K_REQ_CSR_HASH); enc.encode_bytes(BytesView(csr_hash)); }
    if (request_signature.size() > 0) { enc.encode_uint(K_REQ_SIG); enc.encode_bytes(BytesView(request_signature)); }
    return enc.take();
}

Result<JoinRequest> JoinRequest::decode_cbor(BytesView data) {
    cbor::Decoder dec(data);
    JoinRequest req;
    auto map_sz = dec.decode_map_size();
    if (!map_sz) return map_sz.error();

    for (size_t i = 0; i < map_sz.value(); ++i) {
        auto key = dec.decode_uint();
        if (!key) return key.error();
        switch (key.value()) {
            case K_REQ_TOKEN: {
                auto v = dec.decode_string(); if (!v) return v.error();
                req.token = std::move(v.value()); break;
            }
            case K_REQ_CSR: {
                auto v = dec.decode_string(); if (!v) return v.error();
                req.csr_pem = std::move(v.value()); break;
            }
            case K_REQ_TIMESTAMP: {
                auto v = dec.decode_int(); if (!v) return v.error();
                req.timestamp = v.value(); break;
            }
            case K_REQ_NONCE: {
                auto v = dec.decode_bytes(); if (!v) return v.error();
                if (v.value().size() == 8) std::memcpy(req.nonce.data(), v.value().data(), 8);
                break;
            }
            case K_REQ_CSR_HASH: {
                auto v = dec.decode_bytes(); if (!v) return v.error();
                req.csr_hash = Bytes(v.value().begin(), v.value().end()); break;
            }
            case K_REQ_SIG: {
                auto v = dec.decode_bytes(); if (!v) return v.error();
                req.request_signature = Bytes(v.value().begin(), v.value().end()); break;
            }
            default: {
                auto r = dec.skip(); if (!r) return r.error();
            }
        }
    }
    return req;
}

// ── JoinResponse ────────────────────────────────────────────────────
Bytes JoinResponse::encode_cbor() const {
    cbor::Encoder enc;
    int fields = 0;
    if (!certificate_pem.empty()) fields++;
    if (!mesh_id.empty()) fields++;
    if (!manifest_digest.empty()) fields++;
    if (manifest_epoch > 0) fields++;
    if (!bootstrap_nodes.empty()) fields++;
    fields++; // nonce always present
    if (fields == 0) fields = 1; // at least empty map
    enc.encode_map(fields);
    if (!certificate_pem.empty()) { enc.encode_uint(K_RESP_CERT); enc.encode_string(certificate_pem); }
    if (!mesh_id.empty()) { enc.encode_uint(K_RESP_MESH_ID); enc.encode_string(mesh_id); }
    if (!manifest_digest.empty()) { enc.encode_uint(K_RESP_MF_DIGEST); enc.encode_bytes(BytesView(manifest_digest)); }
    if (manifest_epoch > 0) { enc.encode_uint(K_RESP_MF_EPOCH); enc.encode_uint(manifest_epoch); }
    if (!bootstrap_nodes.empty()) {
        enc.encode_uint(K_RESP_BOOT_NODES);
        enc.encode_array(bootstrap_nodes.size());
        for (auto& ep : bootstrap_nodes) enc.encode_string(ep);
    }
    enc.encode_uint(K_RESP_NONCE); enc.encode_bytes(BytesView(nonce.data(), nonce.size()));
    return enc.take();
}

Result<JoinResponse> JoinResponse::decode_cbor(BytesView data) {
    cbor::Decoder dec(data);
    JoinResponse resp;
    auto map_sz = dec.decode_map_size();
    if (!map_sz) return map_sz.error();

    for (size_t i = 0; i < map_sz.value(); ++i) {
        auto key = dec.decode_uint();
        if (!key) return key.error();
        switch (key.value()) {
            case K_RESP_CERT: {
                auto v = dec.decode_string(); if (!v) return v.error();
                resp.certificate_pem = std::move(v.value()); break;
            }
            case K_RESP_MESH_ID: {
                auto v = dec.decode_string(); if (!v) return v.error();
                resp.mesh_id = std::move(v.value()); break;
            }
            case K_RESP_MF_DIGEST: {
                auto v = dec.decode_bytes(); if (!v) return v.error();
                resp.manifest_digest = Bytes(v.value().begin(), v.value().end()); break;
            }
            case K_RESP_MF_EPOCH: {
                auto v = dec.decode_uint(); if (!v) return v.error();
                resp.manifest_epoch = v.value(); break;
            }
            case K_RESP_BOOT_NODES: {
                auto arr_sz = dec.decode_array_size(); if (!arr_sz) return arr_sz.error();
                for (size_t j = 0; j < arr_sz.value(); ++j) {
                    auto s = dec.decode_string(); if (!s) return s.error();
                    resp.bootstrap_nodes.push_back(std::move(s.value()));
                }
                break;
            }
            case K_RESP_NONCE: {
                auto v = dec.decode_bytes(); if (!v) return v.error();
                if (v.value().size() == 8) std::memcpy(resp.nonce.data(), v.value().data(), 8);
                break;
            }
            default: {
                auto r = dec.skip(); if (!r) return r.error();
            }
        }
    }
    return resp;
}

// ── BootstrapSyncRequest ───────────────────────────────────────────
Bytes BootstrapSyncRequest::encode_cbor() const {
    cbor::Encoder enc;
    int fields = 0;
    if (!mesh_id.empty()) fields++;
    if (!node_id.empty()) fields++;
    if (manifest_epoch > 0) fields++;
    if (crl_epoch > 0) fields++;
    if (membership_epoch > 0) fields++;
    if (policy_version > 0) fields++;
    enc.encode_map(fields);
    if (!mesh_id.empty()) { enc.encode_uint(K_SYNC_REQ_MESH_ID); enc.encode_string(mesh_id); }
    if (!node_id.empty()) { enc.encode_uint(K_SYNC_REQ_NODE_ID); enc.encode_string(node_id); }
    if (manifest_epoch > 0) { enc.encode_uint(K_SYNC_REQ_MF_EPOCH); enc.encode_uint(manifest_epoch); }
    if (crl_epoch > 0) { enc.encode_uint(K_SYNC_REQ_CRL_EPOCH); enc.encode_uint(crl_epoch); }
    if (membership_epoch > 0) { enc.encode_uint(K_SYNC_REQ_MEMBER_EPOCH); enc.encode_uint(membership_epoch); }
    if (policy_version > 0) { enc.encode_uint(K_SYNC_REQ_POLICY_VER); enc.encode_uint(policy_version); }
    return enc.take();
}

Result<BootstrapSyncRequest> BootstrapSyncRequest::decode_cbor(BytesView data) {
    cbor::Decoder dec(data);
    BootstrapSyncRequest req;
    auto map_sz = dec.decode_map_size();
    if (!map_sz) return map_sz.error();

    for (size_t i = 0; i < map_sz.value(); ++i) {
        auto key = dec.decode_uint();
        if (!key) return key.error();
        switch (key.value()) {
            case K_SYNC_REQ_MESH_ID: {
                auto v = dec.decode_string(); if (!v) return v.error();
                req.mesh_id = std::move(v.value()); break;
            }
            case K_SYNC_REQ_NODE_ID: {
                auto v = dec.decode_string(); if (!v) return v.error();
                req.node_id = std::move(v.value()); break;
            }
            case K_SYNC_REQ_MF_EPOCH: {
                auto v = dec.decode_uint(); if (!v) return v.error();
                req.manifest_epoch = v.value(); break;
            }
            case K_SYNC_REQ_CRL_EPOCH: {
                auto v = dec.decode_uint(); if (!v) return v.error();
                req.crl_epoch = v.value(); break;
            }
            case K_SYNC_REQ_MEMBER_EPOCH: {
                auto v = dec.decode_uint(); if (!v) return v.error();
                req.membership_epoch = v.value(); break;
            }
            case K_SYNC_REQ_POLICY_VER: {
                auto v = dec.decode_uint(); if (!v) return v.error();
                req.policy_version = v.value(); break;
            }
            default: {
                auto r = dec.skip(); if (!r) return r.error();
            }
        }
    }
    return req;
}

// ── BootstrapSyncResponse ───────────────────────────────────────────
Bytes BootstrapSyncResponse::encode_cbor() const {
    cbor::Encoder enc;
    int fields = 0;
    if (!manifest_delta.empty()) fields++;
    if (!membership_delta.empty()) fields++;
    if (!policy_delta.empty()) fields++;
    if (!crl_delta.empty()) fields++;
    if (manifest_epoch > 0) fields++;
    if (membership_epoch > 0) fields++;
    if (crl_epoch > 0) fields++;
    if (policy_version > 0) fields++;
    if (fields == 0) fields = 1;
    enc.encode_map(fields);
    if (!manifest_delta.empty()) { enc.encode_uint(K_SYNC_RESP_MF_DELTA); enc.encode_bytes(BytesView(manifest_delta)); }
    if (!membership_delta.empty()) { enc.encode_uint(K_SYNC_RESP_MEMBER_DELTA); enc.encode_bytes(BytesView(membership_delta)); }
    if (!policy_delta.empty()) { enc.encode_uint(K_SYNC_RESP_POLICY_DELTA); enc.encode_bytes(BytesView(policy_delta)); }
    if (!crl_delta.empty()) { enc.encode_uint(K_SYNC_RESP_CRL_DELTA); enc.encode_bytes(BytesView(crl_delta)); }
    if (manifest_epoch > 0) { enc.encode_uint(K_SYNC_RESP_MF_EPOCH); enc.encode_uint(manifest_epoch); }
    if (membership_epoch > 0) { enc.encode_uint(K_SYNC_RESP_MEMBER_EPOCH); enc.encode_uint(membership_epoch); }
    if (crl_epoch > 0) { enc.encode_uint(K_SYNC_RESP_CRL_EPOCH); enc.encode_uint(crl_epoch); }
    if (policy_version > 0) { enc.encode_uint(K_SYNC_RESP_POLICY_VER); enc.encode_uint(policy_version); }
    return enc.take();
}

Result<BootstrapSyncResponse> BootstrapSyncResponse::decode_cbor(BytesView data) {
    cbor::Decoder dec(data);
    BootstrapSyncResponse resp;
    auto map_sz = dec.decode_map_size();
    if (!map_sz) return map_sz.error();

    for (size_t i = 0; i < map_sz.value(); ++i) {
        auto key = dec.decode_uint();
        if (!key) return key.error();
        switch (key.value()) {
            case K_SYNC_RESP_MF_DELTA: {
                auto v = dec.decode_bytes(); if (!v) return v.error();
                resp.manifest_delta = Bytes(v.value().begin(), v.value().end()); break;
            }
            case K_SYNC_RESP_MEMBER_DELTA: {
                auto v = dec.decode_bytes(); if (!v) return v.error();
                resp.membership_delta = Bytes(v.value().begin(), v.value().end()); break;
            }
            case K_SYNC_RESP_POLICY_DELTA: {
                auto v = dec.decode_bytes(); if (!v) return v.error();
                resp.policy_delta = Bytes(v.value().begin(), v.value().end()); break;
            }
            case K_SYNC_RESP_CRL_DELTA: {
                auto v = dec.decode_bytes(); if (!v) return v.error();
                resp.crl_delta = Bytes(v.value().begin(), v.value().end()); break;
            }
            case K_SYNC_RESP_MF_EPOCH: {
                auto v = dec.decode_uint(); if (!v) return v.error();
                resp.manifest_epoch = v.value(); break;
            }
            case K_SYNC_RESP_MEMBER_EPOCH: {
                auto v = dec.decode_uint(); if (!v) return v.error();
                resp.membership_epoch = v.value(); break;
            }
            case K_SYNC_RESP_CRL_EPOCH: {
                auto v = dec.decode_uint(); if (!v) return v.error();
                resp.crl_epoch = v.value(); break;
            }
            case K_SYNC_RESP_POLICY_VER: {
                auto v = dec.decode_uint(); if (!v) return v.error();
                resp.policy_version = v.value(); break;
            }
            default: {
                auto r = dec.skip(); if (!r) return r.error();
            }
        }
    }
    return resp;
}

// ── Join FSM transition table ────────────────────────────────────────

std::vector<smo::TransitionRule> join_transition_table() {
    using enum JoinState;
    using enum JoinEvent;
    return {
        // Main path
        {static_cast<int64_t>(NEW),             static_cast<int64_t>(TOKEN_PARSED),    static_cast<int64_t>(TOKEN_RECEIVED)},
        {static_cast<int64_t>(TOKEN_RECEIVED),  static_cast<int64_t>(CSR_BUILT),       static_cast<int64_t>(CSR_CREATED)},
        {static_cast<int64_t>(CSR_CREATED),     static_cast<int64_t>(MSG_SENT),        static_cast<int64_t>(JOIN_SENT)},
        {static_cast<int64_t>(JOIN_SENT),       static_cast<int64_t>(RESPONSE_RCVD),   static_cast<int64_t>(WAIT_RESPONSE)},
        {static_cast<int64_t>(WAIT_RESPONSE),   static_cast<int64_t>(RESPONSE_RCVD),   static_cast<int64_t>(CERT_RECEIVED)},

        // CERT_VERIFY — new state per §5.21
        {static_cast<int64_t>(CERT_RECEIVED),   static_cast<int64_t>(CERT_VERIFIED),   static_cast<int64_t>(CERT_VERIFY)},
        {static_cast<int64_t>(CERT_RECEIVED),   static_cast<int64_t>(CERT_INVALID),    static_cast<int64_t>(FAILED)},

        // Bootstrap sync
        {static_cast<int64_t>(CERT_VERIFY),     static_cast<int64_t>(SYNC_REQUESTED),  static_cast<int64_t>(BOOTSTRAP_SYNC)},
        {static_cast<int64_t>(BOOTSTRAP_SYNC),  static_cast<int64_t>(SYNC_COMPLETE),   static_cast<int64_t>(WAIT_SYNC)},

        // Gossip sync
        {static_cast<int64_t>(WAIT_SYNC),       static_cast<int64_t>(GOSSIP_STARTED),  static_cast<int64_t>(GOSSIP_SYNC)},
        {static_cast<int64_t>(GOSSIP_SYNC),     static_cast<int64_t>(GOSSIP_COMPLETE), static_cast<int64_t>(WAIT_GOSSIP)},
        {static_cast<int64_t>(WAIT_GOSSIP),     static_cast<int64_t>(GOSSIP_COMPLETE), static_cast<int64_t>(READY)},

        // Retry / failure
        {static_cast<int64_t>(WAIT_RESPONSE),   static_cast<int64_t>(TIMEOUT),         static_cast<int64_t>(JOIN_SENT)},
        {static_cast<int64_t>(WAIT_SYNC),       static_cast<int64_t>(TIMEOUT),         static_cast<int64_t>(BOOTSTRAP_SYNC)},
        {static_cast<int64_t>(WAIT_GOSSIP),     static_cast<int64_t>(TIMEOUT),         static_cast<int64_t>(GOSSIP_SYNC)},
        {static_cast<int64_t>(WAIT_RESPONSE),   static_cast<int64_t>(FAIL),            static_cast<int64_t>(FAILED)},
        {static_cast<int64_t>(WAIT_SYNC),       static_cast<int64_t>(FAIL),            static_cast<int64_t>(FAILED)},
        {static_cast<int64_t>(WAIT_GOSSIP),     static_cast<int64_t>(FAIL),            static_cast<int64_t>(FAILED)},
        {static_cast<int64_t>(CERT_VERIFY),     static_cast<int64_t>(FAIL),            static_cast<int64_t>(FAILED)},
    };
}

std::vector<smo::StateTimeout> join_timeout_table() {
    using enum JoinState;
    return {
        {static_cast<int64_t>(WAIT_RESPONSE),  30'000'000'000ULL,  static_cast<int64_t>(FAILED)},  // 30s
        {static_cast<int64_t>(WAIT_SYNC),      30'000'000'000ULL,  static_cast<int64_t>(FAILED)},  // 30s
        {static_cast<int64_t>(WAIT_GOSSIP),    60'000'000'000ULL,  static_cast<int64_t>(FAILED)},  // 60s
    };
}

namespace {
    Bytes hex_to_bytes(const std::string& hex) {
        Bytes out;
        out.reserve(hex.size() / 2);
        for (size_t i = 0; i + 1 < hex.size(); i += 2) {
            char buf[3] = {hex[i], hex[i+1], 0};
            out.push_back(static_cast<uint8_t>(std::strtoul(buf, nullptr, 16)));
        }
        return out;
    }
}

// ── process_join_request ────────────────────────────────────────────
// Server-side: validates JoinRequest → issues cert → returns JoinResponse

Result<JoinResponse> process_join_request(
    const JoinRequest& req,
    MeshManager& mesh_mgr,
    authority::MeshAuthority& authority)
{
    // 1. Parse token
    auto token_result = enroll::parse_token(req.token);
    if (!token_result) {
        return SMO_ERR_CERT(213, Error, NoRetry, None,
                            "Invalid join token: " + token_result.error().message);
    }
    const auto& token = token_result.value();

    // 2. Verify timestamp (±30s window)
    auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (std::llabs(now_sec - req.timestamp) > 30) {
        return SMO_ERR_CERT(214, Error, NoRetry, None,
                            "JoinRequest timestamp outside ±30s window");
    }

    // 3. Check token expiry
    if (token.expiry_unix_sec > 0 && now_sec > token.expiry_unix_sec) {
        return SMO_ERR_CERT(213, Error, NoRetry, None, "Join token expired");
    }

    // 4. Verify request_signature: sign(token || timestamp || nonce || csr_hash)
    auto& reg = CryptoRegistry::instance();
    auto crypto_result = reg.get_suite(token.cipher_suite_id);
    if (!crypto_result) return crypto_result.error();
    const auto* crypto = crypto_result.value();

    {
        Bytes sig_check;
        sig_check.insert(sig_check.end(), req.token.begin(), req.token.end());
        sig_check.push_back(static_cast<uint8_t>((req.timestamp >> 56) & 0xFF));
        sig_check.push_back(static_cast<uint8_t>((req.timestamp >> 48) & 0xFF));
        sig_check.push_back(static_cast<uint8_t>((req.timestamp >> 40) & 0xFF));
        sig_check.push_back(static_cast<uint8_t>((req.timestamp >> 32) & 0xFF));
        sig_check.push_back(static_cast<uint8_t>((req.timestamp >> 24) & 0xFF));
        sig_check.push_back(static_cast<uint8_t>((req.timestamp >> 16) & 0xFF));
        sig_check.push_back(static_cast<uint8_t>((req.timestamp >> 8) & 0xFF));
        sig_check.push_back(static_cast<uint8_t>(req.timestamp & 0xFF));
        sig_check.insert(sig_check.end(), req.nonce.begin(), req.nonce.end());
        sig_check.insert(sig_check.end(), req.csr_hash.begin(), req.csr_hash.end());

        // Extract issuer public key from token issuer field
        // Format: "root:<fingerprint>" or "authority:<fingerprint>"
        auto delim = token.issuer.find(':');
        if (delim == std::string::npos) {
            return SMO_ERR_CERT(215, Error, NoRetry, None, "Invalid issuer field");
        }
        std::string issuer_pubkey_hex = token.issuer.substr(delim + 1);
        Bytes issuer_pubkey = hex_to_bytes(issuer_pubkey_hex);

        auto verify_res = crypto->signer.verify(
            BytesView(sig_check), BytesView(req.request_signature),
            BytesView(issuer_pubkey));
        if (!verify_res || !verify_res.value()) {
            return SMO_ERR_CERT(216, Error, NoRetry, None,
                                "JoinRequest signature verification failed");
        }
    }

    // 5. Parse CSR and sign certificate
    Bytes csr_bytes = hex_to_bytes(req.csr_pem);
    auto csr_result = CertificateSigningRequest::deserialize(BytesView(csr_bytes));
    if (!csr_result) {
        return SMO_ERR_CERT(217, Error, NoRetry, None,
                            "Invalid CSR: " + csr_result.error().message);
    }

    // 6. Issue certificate via Authority
    auto cert_result = authority.sign_csr(BytesView(csr_bytes), token.mesh_id);
    if (!cert_result) {
        return SMO_ERR_CERT(218, Error, NoRetry, None,
                            "Failed to issue certificate: " + cert_result.error().message);
    }

    // 7. Register node in registry
    const auto& cert = cert_result.value();
    auto hash_res = cert.cert_hash(crypto->hash);
    std::string fp = hash_res ? bytes_to_hex(hash_res.value()) : "unknown";

    authority::NodeRecord node_rec;
    node_rec.node_id_hex = bytes_to_hex(cert.subject_pubkey);
    node_rec.mesh_id = token.mesh_id;
    node_rec.role = token.admission.role;
    node_rec.status = "active";
    node_rec.epoch = static_cast<uint64_t>(cert.epoch);
    node_rec.last_seen = now_sec;
    node_rec.cert_fingerprint = fp;
    (void)authority.registry().register_node(node_rec);

    // 8. Build JoinResponse
    JoinResponse resp;
    resp.nonce = req.nonce;
    resp.certificate_pem = bytes_to_hex(cert.serialize_full());
    resp.mesh_id = token.mesh_id;
    resp.manifest_epoch = 1;

    // Get current mesh for bootstrap nodes
    auto mesh_res = mesh_mgr.get_mesh(token.mesh_id);
    if (mesh_res) {
        resp.bootstrap_nodes = mesh_res.value()->config.bootstrap_endpoints;
    }

    return resp;
}

// ── process_bootstrap_sync ───────────────────────────────────────────
// Server-side: epoch-based delta sync for post-join bootstrap

Result<BootstrapSyncResponse> process_bootstrap_sync(
    const BootstrapSyncRequest& req,
    MeshManager& mesh_mgr,
    authority::MeshAuthority& authority,
    recovery::CRL* crl)
{
    auto mesh_res = mesh_mgr.get_current_mesh();
    if (!mesh_res) {
        return SMO_ERR_PROTOCOL(900, Error, NoRetry, None,
                                "No active mesh for bootstrap sync");
    }
    auto& ctx = mesh_res.value();
    auto& cfg = ctx->config;
    uint64_t current_epoch = static_cast<uint64_t>(cfg.epoch);

    BootstrapSyncResponse resp;
    resp.nonce = req.nonce;
    resp.manifest_epoch = current_epoch;

    // Manifest delta
    if (req.manifest_epoch < current_epoch) {
        cbor::Encoder enc;
        enc.encode_map(4);
        enc.encode_uint(1); enc.encode_string(cfg.mesh_id);
        enc.encode_uint(2); enc.encode_uint(current_epoch);
        enc.encode_uint(3); enc.encode_string(cfg.display_name.empty() ? cfg.mesh_id : cfg.display_name);
        enc.encode_uint(4);
        enc.encode_array(cfg.bootstrap_endpoints.size());
        for (auto& ep : cfg.bootstrap_endpoints) enc.encode_string(ep);
        resp.manifest_delta = enc.take();
    }

    // Membership delta
    resp.membership_epoch = current_epoch;
    if (req.membership_epoch < current_epoch) {
        auto nodes = authority.registry().list_nodes(cfg.mesh_id);
        if (nodes && !nodes.value().empty()) {
            cbor::Encoder enc;
            enc.encode_array(nodes.value().size());
            for (auto& n : nodes.value()) {
                enc.encode_map(5);
                enc.encode_uint(1); enc.encode_string(n.node_id_hex);
                enc.encode_uint(2); enc.encode_string(n.role);
                enc.encode_uint(3); enc.encode_string(n.status);
                enc.encode_uint(4); enc.encode_uint(n.epoch);
                enc.encode_uint(5); enc.encode_int(n.last_seen);
            }
            resp.membership_delta = enc.take();
        }
    }

    // CRL delta
    resp.crl_epoch = current_epoch;
    if (crl && req.crl_epoch < current_epoch) {
        auto entries = crl->entries_since(req.crl_epoch);
        if (!entries.empty()) {
            cbor::Encoder enc;
            enc.encode_array(entries.size());
            for (auto& e : entries) {
                enc.encode_map(4);
                enc.encode_uint(1); enc.encode_string(e.cert_fingerprint);
                enc.encode_uint(2); enc.encode_string(e.node_id_hex);
                enc.encode_uint(3); enc.encode_uint(e.epoch);
                enc.encode_uint(4); enc.encode_int(e.revoked_at);
            }
            resp.crl_delta = enc.take();
        }
    }

    // Policy delta — simple epoch-based for now
    resp.policy_version = current_epoch;

    return resp;
}

} // namespace smo::join
