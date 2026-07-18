#pragma once

#include "cbor.hpp"
#include "core/types.hpp"
#include "core/errors/error.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace smo::bootstrap {

// BootstrapSnapshot — complete mesh state returned in a BootstrapResponse.
// CBOR map keys follow RFC 0034 §Payload Serialization.

struct AuthorityInfo {
    std::string node_id;
    std::string endpoint;

    void encode_cbor(cbor::Encoder& enc) const;
    static Result<AuthorityInfo> decode_cbor(cbor::Decoder& dec);
};

struct HealthInfo {
    std::string level;       // "Healthy", "Warning", "Critical", "Recovery"
    bool        operational = false;

    void encode_cbor(cbor::Encoder& enc) const;
    static Result<HealthInfo> decode_cbor(cbor::Decoder& dec);
};

struct BootstrapSnapshot {
    uint8_t  schema_version = 1;
    std::string mesh_id;
    std::string mesh_state;
    int64_t     epoch = 1;

    // Genesis manifest as CBOR map bytes (raw payload)
    Bytes genesis_manifest_cbor;

    // Authority nodes
    std::vector<AuthorityInfo> authorities;
    std::vector<std::string> seeds;

    // Version info
    int64_t policy_version     = 1;
    int64_t governance_version = 1;

    // CRL
    std::array<uint8_t, 32> crl_digest{}; // Blake3 hash
    uint32_t crl_count = 0;

    // Health
    HealthInfo health;

    // Capabilities
    uint8_t cipher_suite = 3; // 1=Classical, 2=Hybrid, 3=PurePQC
    std::vector<uint8_t> opcodes;

    // Governance
    uint32_t active_proposals = 0;

    // Serialization (CBOR)
    Bytes encode_cbor() const;
    static Result<BootstrapSnapshot> decode_cbor(BytesView data);

    // JSON display (for CLI / logging)
    std::string to_json() const;
};

} // namespace smo::bootstrap
