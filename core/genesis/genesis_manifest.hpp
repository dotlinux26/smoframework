#pragma once

#include "../errors/error.hpp"
#include "../types.hpp"

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace smo::genesis {

enum class DeploymentProfile : uint8_t {
    Personal    = 0,
    Homelab     = 1,
    Startup     = 2,
    Enterprise  = 3,
    AirGapped   = 4,
    Custom      = 5,
};

inline std::string to_string(DeploymentProfile p) {
    switch (p) {
        case DeploymentProfile::Personal:   return "personal";
        case DeploymentProfile::Homelab:    return "homelab";
        case DeploymentProfile::Startup:    return "startup";
        case DeploymentProfile::Enterprise: return "enterprise";
        case DeploymentProfile::AirGapped:  return "air-gapped";
        case DeploymentProfile::Custom:     return "custom";
    }
    return "unknown";
}

inline Result<DeploymentProfile> deployment_profile_from_string(const std::string& s) {
    if (s == "personal")   return DeploymentProfile::Personal;
    if (s == "homelab")    return DeploymentProfile::Homelab;
    if (s == "startup")    return DeploymentProfile::Startup;
    if (s == "enterprise") return DeploymentProfile::Enterprise;
    if (s == "air-gapped") return DeploymentProfile::AirGapped;
    if (s == "custom")     return DeploymentProfile::Custom;
    return SMO_ERR_GENESIS(100, Error, NoRetry, ManualIntervention,
                           "unknown deployment profile: " + s);
}

struct AuthorityRange {
    uint32_t minimum   = 1;
    uint32_t preferred = 3;
    uint32_t maximum   = 7;

    bool valid() const {
        return minimum >= 1 && minimum <= preferred && preferred <= maximum;
    }
};

struct QuorumConfig {
    std::string authority_create    = "2/3";
    std::string authority_revoke    = "3/4";
    std::string policy_update       = "2/3";
    std::string emergency_lockdown  = "1/3";
    std::string epoch_rotate        = "3/4";
    std::string recovery            = "1/2";
};

struct FaultTolerance {
    uint32_t max_authority_failures   = 1;
    uint32_t max_compromised          = 0;
};

struct GenesisManifest {
    uint32_t schema_version   = 1;
    std::string mesh_id;
    std::string root_public_key;
    uint32_t manifest_version = 1;
    uint32_t epoch            = 1;
    std::string state         = "genesis";

    DeploymentProfile profile  = DeploymentProfile::Enterprise;
    AuthorityRange authorities;
    QuorumConfig quorum;
    FaultTolerance fault_tolerance;

    uint64_t created_at = 0;
    uint32_t wizard_version = 1;

    Result<Bytes> serialize() const;
    static Result<GenesisManifest> deserialize(BytesView data);
};

// ── Helper: compute recommended quorum from authority count ──────────
inline QuorumConfig compute_recommended_quorum(uint32_t n) {
    QuorumConfig q;
    if (n == 0) return q;
    q.authority_create   = std::to_string(n / 2 + 1) + "/" + std::to_string(n);
    q.authority_revoke   = std::to_string(2 * n / 3 + 1) + "/" + std::to_string(n);
    q.policy_update      = std::to_string(n / 2 + 1) + "/" + std::to_string(n);
    q.emergency_lockdown = std::to_string((n + 2) / 3) + "/" + std::to_string(n);
    q.epoch_rotate       = std::to_string(2 * n / 3 + 1) + "/" + std::to_string(n);
    q.recovery           = std::to_string(n / 2 + 1) + "/" + std::to_string(n);
    return q;
}

// ── Defaults per profile ──────────────────────────────────────────────
inline void apply_profile_defaults(DeploymentProfile p, AuthorityRange& ar, QuorumConfig& q, FaultTolerance& ft) {
    switch (p) {
        case DeploymentProfile::Personal:
            ar = {1, 1, 3};
            ft = {0, 0};
            break;
        case DeploymentProfile::Homelab:
            ar = {1, 3, 5};
            ft = {1, 0};
            break;
        case DeploymentProfile::Startup:
            ar = {1, 3, 5};
            ft = {1, 0};
            break;
        case DeploymentProfile::Enterprise:
            ar = {1, 5, 15};
            ft = {2, 1};
            break;
        case DeploymentProfile::AirGapped:
            ar = {1, 3, 5};
            ft = {1, 0};
            break;
        case DeploymentProfile::Custom:
            break;
    }
    q = compute_recommended_quorum(ar.preferred);
}

} // namespace smo::genesis
