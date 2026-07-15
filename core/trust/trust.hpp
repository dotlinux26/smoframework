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
// Trust error codes (codes 200-208 within Trust category, mapped from
// spec codes 1200-1208 for 10-bit code field fit)
// ===========================================================================
namespace TrustErrc {
    inline constexpr ErrorCode
    ScoreUnavailable(ErrorCategory::Trust, 200, Severity::Info, RetryClass::RetrySafe, Recovery::None);
    inline constexpr ErrorCode
    BelowThreshold(ErrorCategory::Trust, 201, Severity::Info, RetryClass::NoRetry, Recovery::None);
    inline constexpr ErrorCode
    PenaltyApplied(ErrorCategory::Trust, 202, Severity::Info, RetryClass::NoRetry, Recovery::None);
    inline constexpr ErrorCode
    DigestInvalid(ErrorCategory::Trust, 203, Severity::Warn, RetryClass::NoRetry, Recovery::None);
    inline constexpr ErrorCode
    DigestStale(ErrorCategory::Trust, 204, Severity::Info, RetryClass::NoRetry, Recovery::None);
    inline constexpr ErrorCode
    WitnessSelectionFailed(ErrorCategory::Trust, 205, Severity::Warn, RetryClass::RetrySafe, Recovery::RestartFSM);
    inline constexpr ErrorCode
    AttestationInvalid(ErrorCategory::Trust, 206, Severity::Error, RetryClass::NoRetry, Recovery::None);
    inline constexpr ErrorCode
    AttestationConflict(ErrorCategory::Trust, 207, Severity::Warn, RetryClass::NoRetry, Recovery::None);
    inline constexpr ErrorCode
    AttestationExpired(ErrorCategory::Trust, 208, Severity::Warn, RetryClass::NoRetry, Recovery::None);
} // namespace TrustErrc

// ===========================================================================
// TrustLevel — discrete trust tiers
// ===========================================================================
enum class TrustLevel : uint8_t {
    None     = 0,  // No trust data / below 0.2
    Low      = 1,  // 0.2 – 0.4
    Medium   = 2,  // 0.4 – 0.7
    High     = 3,  // 0.7 – 0.9
    Absolute = 4,  // >= 0.9 (trust anchor)
};

const char* to_string(TrustLevel l) noexcept;

// ===========================================================================
// TrustComponents — four dimensions of trust (per §XI)
// ===========================================================================
struct TrustComponents {
    double citizen{0.0};       // online time, heartbeat stability
    double execution{0.0};     // contract success ratio
    double witness{0.0};       // witness participation and accuracy
    double consistency{0.0};   // result agreement with majority
};

// Composite: Citizen×0.2 + Execution×0.5 + Witness×0.2 + Consistency×0.1
// Weights are defaults; local policy MAY override.
double compute_composite(const TrustComponents& c) noexcept;

// ===========================================================================
// TrustConfig — configurable weights and decay parameters
// ===========================================================================
struct TrustConfig {
    double weight_citizen{0.2};
    double weight_execution{0.5};
    double weight_witness{0.2};
    double weight_consistency{0.1};
    double decay_half_life_days{30.0};
    double citizen_penalty_offline{0.001};
    double requester_penalty_rejected{0.01};
    double requester_penalty_no_authority{0.05};
};

// ===========================================================================
// TrustScore — per-peer score record
// ===========================================================================
struct TrustScore {
    NodeID          node_id;
    TrustComponents components;
    double          composite{0.0};
    int64_t         last_updated{0};

    TrustLevel level() const noexcept;

    Bytes serialize() const;
    static Result<TrustScore> deserialize(BytesView data);
};

// ===========================================================================
// Attestation — witness attestation about a peer
// ===========================================================================
struct Attestation {
    NodeID witness_id;       // Who is attesting
    NodeID subject_id;       // Who is being attested about
    double claimed_score{0.0};
    int64_t timestamp{0};
    Bytes   signature;       // witness's signature

    Bytes serialize() const;
    static Result<Attestation> deserialize(BytesView data);
};

// ===========================================================================
// TrustAnchor — pre-configured root of trust
// ===========================================================================
struct TrustAnchor {
    NodeID  node_id;
    Bytes   public_key;
    int64_t added_at{0};

    bool operator==(const TrustAnchor& o) const noexcept {
        return node_id == o.node_id;
    }
};

// ===========================================================================
// TrustDigest — gossip payload containing scores for known peers
// ===========================================================================
struct TrustDigest {
    NodeID  origin;
    int64_t sequence{0};       // Monotonic counter, newest wins
    int64_t timestamp{0};
    std::vector<TrustScore> scores;

    Bytes serialize() const;
    static Result<TrustDigest> deserialize(BytesView data);
};

// ===========================================================================
// TrustManager — central trust authority for the node
// ===========================================================================
class TrustManager {
public:
    TrustManager() = default;
    explicit TrustManager(TrustConfig cfg) : config_(cfg) {}

    // --- Score management ---

    // Record a successful execution
    void record_success(NodeID node, double weight = 1.0, int64_t now = 0);

    // Record a failed execution
    void record_failure(NodeID node, double weight = 1.0, int64_t now = 0);

    // Record an offline detection (citizen penalty)
    void record_offline(NodeID node, int64_t now = 0);

    // Get the composite trust score for a peer
    Result<double> get_score(NodeID node) const;

    // Get the full TrustScore record
    Result<TrustScore> get_record(NodeID node) const;

    // Get all tracked scores
    std::vector<TrustScore> all_scores() const;

    // --- Trust anchors ---

    void add_trust_anchor(TrustAnchor anchor);
    bool remove_trust_anchor(NodeID node);
    bool is_trust_anchor(NodeID node) const;
    std::vector<TrustAnchor> trust_anchors() const;

    // --- Attestation ---

    // Verify an attestation (check timestamp window + signature)
    Result<void> verify_attestation(const Attestation& att, int64_t now,
                                     int64_t max_age = 3600000000000LL) const;

    // Apply an attestation to the subject's score
    void apply_attestation(const Attestation& att);

    // --- Digest ---

    // Produce a trust digest for gossip
    TrustDigest produce_digest(NodeID origin, int64_t now);
    int64_t digest_sequence() const noexcept { return digest_seq_; }

    // Apply a received digest (newer wins)
    Result<void> apply_digest(const TrustDigest& digest);

    // --- Config ---

    const TrustConfig& config() const noexcept { return config_; }
    void set_config(const TrustConfig& cfg) { config_ = cfg; }

    // Compute trust level from a composite score
    static TrustLevel compute_trust_level(double score) noexcept;

    // --- Serialization ---

    Bytes serialize() const;
    static Result<TrustManager> deserialize(BytesView data);

    // --- Utility ---

    void tick(int64_t now);
    size_t count() const noexcept { return scores_.size(); }

private:
    static uint64_t node_id_key(NodeID id) noexcept;

    TrustConfig config_;
    std::unordered_map<uint64_t, TrustScore> scores_;
    std::vector<TrustAnchor> anchors_;
    int64_t digest_seq_ = 0;
};

} // namespace smo
