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
// Governance error codes (800-811)
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
} // namespace GovernanceErrc

// ===========================================================================
// GovernanceLevel — 5-tier model per RFC 0016 §2
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
// ProposalState — lifecycle of a governance proposal
// ===========================================================================
enum class ProposalState : uint8_t {
    Draft     = 0,  // Being created, not yet circulating
    Signing   = 1,  // Collecting signatures
    Committed = 2,  // Threshold met, enacted
    Rejected  = 3,  // Threshold expired or conflicting proposal won
    Expired   = 4,  // TTL elapsed without reaching threshold
};

const char* to_string(ProposalState s) noexcept;

// ===========================================================================
// GovernanceAction — type of change being proposed
// ===========================================================================
enum class GovernanceAction : uint8_t {
    PolicyChange     = 0,  // Modify mesh policy
    AuthorityCreate  = 1,  // Add a new Authority
    AuthorityRevoke  = 2,  // Remove an Authority
    EpochIncrement   = 3,  // Increment the capability epoch
    EmergencyLockdown= 4,  // Emergency mesh lockdown
};

const char* to_string(GovernanceAction a) noexcept;

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
    GovernanceLevel level      = GovernanceLevel::Authority;
    GovernanceAction action    = GovernanceAction::PolicyChange;
    Bytes           payload;  // Action-specific opaque data
    int64_t         created_at  = 0;
    int64_t         expires_at  = 0;
    ProposalState   state       = ProposalState::Draft;
    int             threshold   = 1;    // M-of-N: M signatures required
    std::vector<GovernanceSignature> signatures;

    // Check if the required number of distinct signatures have been collected
    bool threshold_met() const noexcept {
        return static_cast<int>(signatures.size()) >= threshold;
    }

    // Check if the proposal has expired
    bool is_expired(int64_t now) const noexcept {
        return expires_at > 0 && now >= expires_at;
    }

    Bytes serialize() const;
    static Result<GovernanceProposal> deserialize(BytesView data);
};

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

// GOVERNANCE_PROPOSAL message: full proposal
struct GovernanceProposalMsg {
    GovernanceProposal proposal;

    Bytes serialize() const;
    static Result<GovernanceProposalMsg> deserialize(BytesView data);
};

// GOVERNANCE_SIGNATURE message: proposal_id + signature
struct GovernanceSignatureMsg {
    ProposalID proposal_id;
    GovernanceSignature signature;

    Bytes serialize() const;
    static Result<GovernanceSignatureMsg> deserialize(BytesView data);
};

// GOVERNANCE_COMMIT message: proposal_id + result
struct GovernanceCommitMsg {
    ProposalID proposal_id;
    bool       accepted = false;

    Bytes serialize() const;
    static Result<GovernanceCommitMsg> deserialize(BytesView data);
};

// EPOCH_INCREMENT message: new epoch value + signatures
struct EpochIncrementMsg {
    uint64_t new_epoch = 0;
    std::vector<GovernanceSignature> signatures;

    Bytes serialize() const;
    static Result<EpochIncrementMsg> deserialize(BytesView data);
};

} // namespace smo
