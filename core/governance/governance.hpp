#pragma once

#include "../errors/error.hpp"
#include "../identity/identity.hpp"
#include "../types.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace smo {

// ===========================================================================
// Governance error codes (800-811 + new)
// ===========================================================================
namespace GovernanceErrc {
    inline constexpr ErrorCode
    AuthorityNotFound(ErrorCategory::Governance, 800, Severity::Error, RetryClass::NoRetry, Recovery::GovernanceVote);
    inline constexpr ErrorCode
    AuthorityNotAuthorized(ErrorCategory::Governance, 801, Severity::Error, RetryClass::NoRetry, Recovery::GovernanceVote);
    inline constexpr ErrorCode
    ThresholdNotMet(ErrorCategory::Governance, 803, Severity::Warn, RetryClass::NoRetry, Recovery::GovernanceVote);
    inline constexpr ErrorCode
    ProposalInvalid(ErrorCategory::Governance, 810, Severity::Error, RetryClass::NoRetry, Recovery::None);
    inline constexpr ErrorCode
    EpochIncrementFailed(ErrorCategory::Governance, 807, Severity::Critical, RetryClass::NoRetry, Recovery::ManualIntervention);
    inline constexpr ErrorCode
    UnanimousRequired(ErrorCategory::Governance, 811, Severity::Error, RetryClass::NoRetry, Recovery::ManualIntervention);
    inline constexpr ErrorCode
    ProposalConflict(ErrorCategory::Governance, 812, Severity::Warn, RetryClass::NoRetry, Recovery::GovernanceVote);
    inline constexpr ErrorCode
    ActionNotAllowed(ErrorCategory::Governance, 813, Severity::Error, RetryClass::NoRetry, Recovery::ManualIntervention);
} // namespace GovernanceErrc

// ===========================================================================
// GovernanceTier — 2-tier governance model
// ===========================================================================
enum class GovernanceTier : uint8_t {
    Membership   = 0,  // Level A: registry only, no epoch++
    Constitution = 1,  // Level B: manifest_version++, epoch++, broadcast
    Unanimous    = 2,  // Subset of Constitution requiring full consensus
};

const char* to_string(GovernanceTier t) noexcept;

// ===========================================================================
// GovernanceLevel — 5-tier model per RFC 0016 §2 (kept for compat)
// ===========================================================================
enum class GovernanceLevel : uint8_t {
    Local     = 0,  // 1-of-1 (self)
    Authority = 1,  // 1-of-N
    Policy    = 2,  // M-of-N (default 2-of-3)
    Critical  = 3,  // M-of-N (default 3-of-5)
    Genesis   = 4,  // 1-of-1 (Root Key only, once)
};

const char* to_string(GovernanceLevel l) noexcept;

// ===========================================================================
// GovernanceAction — type of change being proposed
// ===========================================================================
enum class GovernanceAction : uint8_t {
    // Level A — Membership (registry only)
    AddAuthority      = 0,
    RemoveAuthority   = 1,
    SuspendAuthority  = 2,
    ResumeAuthority   = 3,

    // Level B — Constitution (manifest_version++, epoch++)
    ChangeMaximum     = 4,
    ChangeMinimum     = 5,
    ChangeQuorum      = 6,
    ChangePolicy      = 7,
    UpdateManifest    = 8,
    UpgradeRuntime    = 9,

    // Unanimous (Constitution + unanimous)
    ChangeCipherSuite    = 10,
    ChangeGovernanceRules= 11,
    DestroyMesh          = 12,
    ChangeRecovery       = 13,

    // Legacy (mapped for backward compat)
    PolicyChange     = 7,      // same as ChangePolicy
    AuthorityCreate  = 0,      // same as AddAuthority
    AuthorityRevoke  = 1,      // same as RemoveAuthority
    EpochIncrement   = 14,
    EmergencyLockdown= 15,
};

const char* to_string(GovernanceAction a) noexcept;

// ── Map action → tier ─────────────────────────────────────────────────
inline GovernanceTier action_to_tier(GovernanceAction a) noexcept {
    switch (a) {
        case GovernanceAction::AddAuthority:
        case GovernanceAction::RemoveAuthority:
        case GovernanceAction::SuspendAuthority:
        case GovernanceAction::ResumeAuthority:
            return GovernanceTier::Membership;

        case GovernanceAction::ChangeMaximum:
        case GovernanceAction::ChangeMinimum:
        case GovernanceAction::ChangeQuorum:
        case GovernanceAction::ChangePolicy:
        case GovernanceAction::UpdateManifest:
        case GovernanceAction::UpgradeRuntime:
            return GovernanceTier::Constitution;

        case GovernanceAction::ChangeCipherSuite:
        case GovernanceAction::ChangeGovernanceRules:
        case GovernanceAction::DestroyMesh:
        case GovernanceAction::ChangeRecovery:
            return GovernanceTier::Unanimous;

        case GovernanceAction::EpochIncrement:
            return GovernanceTier::Constitution;

        case GovernanceAction::EmergencyLockdown:
            return GovernanceTier::Membership;

        default:
            return GovernanceTier::Constitution;
    }
}

// ── Default quorum per action (given N total authorities) ──────────────
inline int default_quorum(GovernanceAction a, int total_authorities) {
    if (total_authorities < 1) return 1;
    auto tier = action_to_tier(a);
    switch (tier) {
        case GovernanceTier::Unanimous:
            return total_authorities;
        case GovernanceTier::Constitution:
            return std::max(1, (3 * total_authorities + 3) / 4);  // ceil(3N/4)
        case GovernanceTier::Membership:
        default:
            return std::max(1, (2 * total_authorities + 2) / 3);  // ceil(2N/3)
    }
}

// ===========================================================================
// ProposalState — lifecycle of a governance proposal
// ===========================================================================
enum class ProposalState : uint8_t {
    Draft      = 0,  // Being created, not yet circulating
    Signing    = 1,  // Collecting signatures
    Committed  = 2,  // Threshold met, enacted
    Rejected   = 3,  // Threshold expired or conflicting proposal won
    Expired    = 4,  // TTL elapsed without reaching threshold
    Conflicted = 5,  // Conflicting proposal won
};

const char* to_string(ProposalState s) noexcept;

// ===========================================================================
// ProposalID — unique identifier for a proposal
// ===========================================================================
struct ProposalID {
    uint64_t value = 0;

    bool operator==(const ProposalID& o) const noexcept = default;
    bool operator!=(const ProposalID& o) const noexcept = default;
};

// ===========================================================================
// GovernanceSignature — a single authority's signature on a proposal
// ===========================================================================
struct GovernanceSignature {
    NodeID  authority_id;
    Bytes   signature;  // Ed25519 signature over the proposal payload
    int64_t signed_at = 0;

    Bytes serialize() const;
    static Result<GovernanceSignature> deserialize(BytesView data);
};

// ===========================================================================
// GovernanceProposal — a proposal to change mesh governance
// ===========================================================================
struct GovernanceProposal {
    ProposalID      id;
    GovernanceTier  tier        = GovernanceTier::Membership;
    GovernanceLevel level       = GovernanceLevel::Authority;
    GovernanceAction action     = GovernanceAction::AddAuthority;
    Bytes           payload;   // Action-specific opaque data
    int64_t         created_at  = 0;
    int64_t         expires_at  = 0;
    ProposalState   state       = ProposalState::Draft;
    int             threshold   = 1;
    std::vector<GovernanceSignature> signatures;

    bool threshold_met() const noexcept {
        return static_cast<int>(signatures.size()) >= threshold;
    }

    bool is_expired(int64_t now) const noexcept {
        return expires_at > 0 && now >= expires_at;
    }

    Bytes serialize() const;
    static Result<GovernanceProposal> deserialize(BytesView data);
};

// ===========================================================================
// MeshHealth — mesh health status and display
// ===========================================================================
enum class HealthLevel : uint8_t {
    Healthy  = 0,
    Warning  = 1,
    Critical = 2,
    Recovery = 3,
};

struct MeshHealth {
    uint32_t total_authorities    = 0;
    uint32_t online_authorities   = 0;
    uint32_t offline_authorities  = 0;
    uint32_t min_required         = 1;
    uint32_t preferred            = 3;
    uint32_t maximum              = 7;
    int      current_quorum       = 0;
    int      required_quorum      = 0;
    bool     operational          = false;
    HealthLevel level             = HealthLevel::Recovery;

    std::string to_display() const;
};

inline MeshHealth compute_health(uint32_t total, uint32_t online,
                                  uint32_t min_req, uint32_t preferred,
                                  uint32_t maximum) {
    MeshHealth h;
    h.total_authorities   = total;
    h.online_authorities  = online;
    h.offline_authorities = total > online ? total - online : 0;
    h.min_required        = min_req;
    h.preferred           = preferred;
    h.maximum             = maximum;
    h.current_quorum      = default_quorum(GovernanceAction::ChangePolicy, static_cast<int>(online));
    h.required_quorum     = default_quorum(GovernanceAction::ChangePolicy, static_cast<int>(total));

    h.operational = online >= h.required_quorum;

    if (online == 0) {
        h.level = HealthLevel::Recovery;
    } else if (online < min_req) {
        h.level = HealthLevel::Critical;
    } else if (online < preferred) {
        h.level = HealthLevel::Warning;
    } else {
        h.level = HealthLevel::Healthy;
    }

    return h;
}

// ===========================================================================
// GovernanceEngine — manages proposals and executes them
// ===========================================================================
class GovernanceEngine {
public:
    GovernanceEngine() = default;

    // Submit a new proposal (enters Signing state)
    Result<ProposalID> submit(GovernanceProposal proposal);

    // Sign an existing proposal (adds signature)
    Result<void> sign(ProposalID id, NodeID authority, Bytes signature, int64_t now);

    // Commit a proposal (marks Committed if threshold met)
    Result<void> commit(ProposalID id, int64_t now);

    // Reject a proposal
    Result<void> reject(ProposalID id);

    // Look up a proposal
    Result<GovernanceProposal> get(ProposalID id) const;

    // All pending (Signing) proposals
    std::vector<GovernanceProposal> pending() const;

    // Tick — expire stale proposals
    void tick(int64_t now);

    // Number of proposals tracked
    size_t count() const noexcept { return proposals_.size(); }

    // Serialize all proposals
    Bytes serialize_all() const;

private:
    std::unordered_map<uint64_t, GovernanceProposal> proposals_;
    uint64_t next_id_ = 1;
};

// ===========================================================================
// Wire message types
// ===========================================================================
struct GovernanceProposalMsg {
    GovernanceProposal proposal;

    Bytes serialize() const;
    static Result<GovernanceProposalMsg> deserialize(BytesView data);
};

struct GovernanceSignatureMsg {
    ProposalID proposal_id;
    GovernanceSignature signature;

    Bytes serialize() const;
    static Result<GovernanceSignatureMsg> deserialize(BytesView data);
};

struct GovernanceCommitMsg {
    ProposalID proposal_id;
    bool       accepted = false;

    Bytes serialize() const;
    static Result<GovernanceCommitMsg> deserialize(BytesView data);
};

struct EpochIncrementMsg {
    uint64_t new_epoch = 0;
    std::vector<GovernanceSignature> signatures;

    Bytes serialize() const;
    static Result<EpochIncrementMsg> deserialize(BytesView data);
};

} // namespace smo
