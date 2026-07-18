#include "bootstrap_snapshot.hpp"

#include <cstring>
#include <sstream>

namespace smo::bootstrap {

// ── CBOR map key constants ────────────────────────────────────────────

namespace {
    constexpr uint64_t K_MESH_ID            = 1;
    constexpr uint64_t K_MESH_STATE         = 2;
    constexpr uint64_t K_EPOCH              = 3;
    constexpr uint64_t K_GENESIS_MANIFEST   = 4;
    constexpr uint64_t K_AUTHORITIES        = 5;
    constexpr uint64_t K_SEEDS              = 6;
    constexpr uint64_t K_POLICY_VERSION     = 7;
    constexpr uint64_t K_GOVERNANCE_VERSION = 8;
    constexpr uint64_t K_CRL_DIGEST         = 9;
    constexpr uint64_t K_CRL_COUNT          = 10;
    constexpr uint64_t K_HEALTH             = 11;
    constexpr uint64_t K_CIPHER_SUITE       = 12;
    constexpr uint64_t K_OPCODES            = 13;
    constexpr uint64_t K_ACTIVE_PROPOSALS   = 14;

    // AuthorityInfo sub-keys
    constexpr uint64_t K_AUTH_ID       = 1;
    constexpr uint64_t K_AUTH_ENDPOINT = 2;

    // HealthInfo sub-keys
    constexpr uint64_t K_HEALTH_LEVEL      = 1;
    constexpr uint64_t K_HEALTH_OPERATIONAL = 2;
}

// ── AuthorityInfo ─────────────────────────────────────────────────────

void AuthorityInfo::encode_cbor(cbor::Encoder& enc) const {
    enc.encode_map(2);
    enc.encode_uint(K_AUTH_ID);
    enc.encode_string(node_id);
    enc.encode_uint(K_AUTH_ENDPOINT);
    enc.encode_string(endpoint);
}

Result<AuthorityInfo> AuthorityInfo::decode_cbor(cbor::Decoder& dec) {
    AuthorityInfo info;
    auto map_sz = dec.decode_map_size();
    if (!map_sz) return map_sz.error();

    for (size_t i = 0; i < map_sz.value(); ++i) {
        auto key = dec.decode_uint();
        if (!key) return key.error();
        switch (key.value()) {
            case K_AUTH_ID: {
                auto v = dec.decode_string();
                if (!v) return v.error();
                info.node_id = std::move(v.value());
                break;
            }
            case K_AUTH_ENDPOINT: {
                auto v = dec.decode_string();
                if (!v) return v.error();
                info.endpoint = std::move(v.value());
                break;
            }
            default: {
                auto r = dec.skip();
                if (!r) return r.error();
                break;
            }
        }
    }
    return info;
}

// ── HealthInfo ────────────────────────────────────────────────────────

void HealthInfo::encode_cbor(cbor::Encoder& enc) const {
    enc.encode_map(2);
    enc.encode_uint(K_HEALTH_LEVEL);
    enc.encode_string(level);
    enc.encode_uint(K_HEALTH_OPERATIONAL);
    enc.encode_uint(operational ? 1 : 0);
}

Result<HealthInfo> HealthInfo::decode_cbor(cbor::Decoder& dec) {
    HealthInfo info;
    auto map_sz = dec.decode_map_size();
    if (!map_sz) return map_sz.error();

    for (size_t i = 0; i < map_sz.value(); ++i) {
        auto key = dec.decode_uint();
        if (!key) return key.error();
        switch (key.value()) {
            case K_HEALTH_LEVEL: {
                auto v = dec.decode_string();
                if (!v) return v.error();
                info.level = std::move(v.value());
                break;
            }
            case K_HEALTH_OPERATIONAL: {
                auto v = dec.decode_uint();
                if (!v) return v.error();
                info.operational = v.value() != 0;
                break;
            }
            default: {
                auto r = dec.skip();
                if (!r) return r.error();
                break;
            }
        }
    }
    return info;
}

// ── BootstrapSnapshot ─────────────────────────────────────────────────

Bytes BootstrapSnapshot::encode_cbor() const {
    cbor::Encoder enc;

    // Count top-level fields (always present)
    size_t field_count = 14;
    enc.encode_map(field_count);

    // 1: mesh_id
    enc.encode_uint(K_MESH_ID);
    enc.encode_string(mesh_id);

    // 2: mesh_state
    enc.encode_uint(K_MESH_STATE);
    enc.encode_string(mesh_state);

    // 3: epoch
    enc.encode_uint(K_EPOCH);
    enc.encode_uint(static_cast<uint64_t>(epoch));

    // 4: genesis_manifest_cbor
    enc.encode_uint(K_GENESIS_MANIFEST);
    enc.encode_bytes(genesis_manifest_cbor);

    // 5: authorities
    enc.encode_uint(K_AUTHORITIES);
    enc.encode_array(authorities.size());
    for (auto& auth : authorities) {
        auth.encode_cbor(enc);
    }

    // 6: seeds
    enc.encode_uint(K_SEEDS);
    enc.encode_array(seeds.size());
    for (auto& s : seeds) {
        enc.encode_string(s);
    }

    // 7: policy_version
    enc.encode_uint(K_POLICY_VERSION);
    enc.encode_uint(static_cast<uint64_t>(policy_version));

    // 8: governance_version
    enc.encode_uint(K_GOVERNANCE_VERSION);
    enc.encode_uint(static_cast<uint64_t>(governance_version));

    // 9: crl_digest
    enc.encode_uint(K_CRL_DIGEST);
    enc.encode_bytes(BytesView(crl_digest.data(), crl_digest.size()));

    // 10: crl_count
    enc.encode_uint(K_CRL_COUNT);
    enc.encode_uint(crl_count);

    // 11: health
    enc.encode_uint(K_HEALTH);
    health.encode_cbor(enc);

    // 12: cipher_suite
    enc.encode_uint(K_CIPHER_SUITE);
    enc.encode_uint(cipher_suite);

    // 13: opcodes
    enc.encode_uint(K_OPCODES);
    enc.encode_array(opcodes.size());
    for (auto& oc : opcodes) {
        enc.encode_uint(oc);
    }

    // 14: active_proposals
    enc.encode_uint(K_ACTIVE_PROPOSALS);
    enc.encode_uint(active_proposals);

    return enc.take();
}

Result<BootstrapSnapshot> BootstrapSnapshot::decode_cbor(BytesView data) {
    cbor::Decoder dec(data);
    BootstrapSnapshot snap;

    auto map_sz = dec.decode_map_size();
    if (!map_sz) return map_sz.error();

    for (size_t i = 0; i < map_sz.value(); ++i) {
        auto key = dec.decode_uint();
        if (!key) return key.error();
        switch (key.value()) {
            case K_MESH_ID: {
                auto v = dec.decode_string();
                if (!v) return v.error();
                snap.mesh_id = std::move(v.value());
                break;
            }
            case K_MESH_STATE: {
                auto v = dec.decode_string();
                if (!v) return v.error();
                snap.mesh_state = std::move(v.value());
                break;
            }
            case K_EPOCH: {
                auto v = dec.decode_uint();
                if (!v) return v.error();
                snap.epoch = static_cast<int64_t>(v.value());
                break;
            }
            case K_GENESIS_MANIFEST: {
                auto v = dec.decode_bytes();
                if (!v) return v.error();
                snap.genesis_manifest_cbor.assign(v.value().begin(), v.value().end());
                break;
            }
            case K_AUTHORITIES: {
                auto arr_sz = dec.decode_array_size();
                if (!arr_sz) return arr_sz.error();
                for (size_t j = 0; j < arr_sz.value(); ++j) {
                    auto auth = AuthorityInfo::decode_cbor(dec);
                    if (!auth) return auth.error();
                    snap.authorities.push_back(std::move(auth.value()));
                }
                break;
            }
            case K_SEEDS: {
                auto arr_sz = dec.decode_array_size();
                if (!arr_sz) return arr_sz.error();
                for (size_t j = 0; j < arr_sz.value(); ++j) {
                    auto v = dec.decode_string();
                    if (!v) return v.error();
                    snap.seeds.push_back(std::move(v.value()));
                }
                break;
            }
            case K_POLICY_VERSION: {
                auto v = dec.decode_uint();
                if (!v) return v.error();
                snap.policy_version = static_cast<int64_t>(v.value());
                break;
            }
            case K_GOVERNANCE_VERSION: {
                auto v = dec.decode_uint();
                if (!v) return v.error();
                snap.governance_version = static_cast<int64_t>(v.value());
                break;
            }
            case K_CRL_DIGEST: {
                auto v = dec.decode_bytes();
                if (!v) return v.error();
                if (v.value().size() == 32) {
                    std::memcpy(snap.crl_digest.data(), v.value().data(), 32);
                }
                break;
            }
            case K_CRL_COUNT: {
                auto v = dec.decode_uint();
                if (!v) return v.error();
                snap.crl_count = static_cast<uint32_t>(v.value());
                break;
            }
            case K_HEALTH: {
                auto h = HealthInfo::decode_cbor(dec);
                if (!h) return h.error();
                snap.health = std::move(h.value());
                break;
            }
            case K_CIPHER_SUITE: {
                auto v = dec.decode_uint();
                if (!v) return v.error();
                snap.cipher_suite = static_cast<uint8_t>(v.value());
                break;
            }
            case K_OPCODES: {
                auto arr_sz = dec.decode_array_size();
                if (!arr_sz) return arr_sz.error();
                for (size_t j = 0; j < arr_sz.value(); ++j) {
                    auto v = dec.decode_uint();
                    if (!v) return v.error();
                    snap.opcodes.push_back(static_cast<uint8_t>(v.value()));
                }
                break;
            }
            case K_ACTIVE_PROPOSALS: {
                auto v = dec.decode_uint();
                if (!v) return v.error();
                snap.active_proposals = static_cast<uint32_t>(v.value());
                break;
            }
            default: {
                auto r = dec.skip();
                if (!r) return r.error();
                break;
            }
        }
    }

    if (snap.mesh_id.empty()) {
        return SMO_ERR_PROTOCOL(900, Error, NoRetry, None,
                                "BootstrapSnapshot CBOR missing mesh_id");
    }

    return snap;
}

// ── JSON display (for CLI / logging — not wire format) ────────────────

static std::string json_esc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

std::string BootstrapSnapshot::to_json() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"mesh_id\":\"" << json_esc(mesh_id) << "\",";
    oss << "\"mesh_state\":\"" << json_esc(mesh_state) << "\",";
    oss << "\"epoch\":" << epoch << ",";

    oss << "\"authorities\":[";
    for (size_t i = 0; i < authorities.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\"node_id\":\"" << json_esc(authorities[i].node_id)
            << "\",\"endpoint\":\"" << json_esc(authorities[i].endpoint) << "\"}";
    }
    oss << "],";

    oss << "\"policy_version\":" << policy_version << ",";
    oss << "\"governance_version\":" << governance_version << ",";
    oss << "\"crl_count\":" << crl_count << ",";
    oss << "\"health\":{\"level\":\"" << json_esc(health.level)
        << "\",\"operational\":" << (health.operational ? "true" : "false") << "},";
    oss << "\"cipher_suite\":" << (int)cipher_suite << ",";
    oss << "\"active_proposals\":" << active_proposals;
    oss << "}";
    return oss.str();
}

} // namespace smo::bootstrap
