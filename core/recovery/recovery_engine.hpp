#pragma once

#include "../errors/error.hpp"
#include "../types.hpp"
#include "../governance/governance.hpp"
#include "../genesis/recovery_package.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace smo::recovery {

enum class RecoveryMode : uint8_t {
    None  = 0,
    Soft  = 1,
    Hard  = 2,
};

enum class RecoveryState : uint8_t {
    Inactive   = 0,
    AwaitingRoot = 1,
    Signing    = 2,
    InProgress = 3,
    Complete   = 4,
    Failed     = 5,
};

struct RecoverySession {
    RecoveryMode  mode       = RecoveryMode::None;
    RecoveryState state      = RecoveryState::Inactive;
    std::string   session_id;
    std::string   mesh_id;
    std::string   root_node_id;
    uint64_t      new_epoch  = 0;
    uint32_t      manifest_version = 0;
    int64_t       created_at = 0;
    int64_t       expires_at = 0;
    std::vector<GovernanceSignature> signatures;
    Bytes         recovery_token;

    bool is_valid(int64_t now_ns) const {
        return state != RecoveryState::Inactive &&
               state != RecoveryState::Failed &&
               state != RecoveryState::Complete &&
               (expires_at == 0 || now_ns <= expires_at);
    }

    Result<Bytes> serialize() const;
    static Result<RecoverySession> deserialize(BytesView data);
};

// ── Recovery Engine ─────────────────────────────────────────────────

struct RecoveryConfig {
    uint64_t session_ttl_ns = 3600ULL * 1'000'000'000;  // 1 hour
    std::string recovery_pkg_path;
    std::string manifest_path;
    std::string registry_path;
};

class RecoveryEngine {
public:
    explicit RecoveryEngine(RecoveryConfig config);

    // Check if recovery is needed
    RecoveryMode assess_mode(uint32_t total_authorities,
                              uint32_t online_authorities,
                              int quorum_threshold) const;

    // Start soft recovery (quorum exists)
    Result<RecoverySession> start_soft(
        const std::string& mesh_id,
        const std::string& root_node_id,
        uint64_t current_epoch,
        const std::string& recovery_passphrase,
        int64_t now_ns
    );

    // Start hard recovery (force reset)
    Result<RecoverySession> start_hard(
        const std::string& mesh_id,
        const std::string& root_node_id,
        uint64_t current_epoch,
        const std::string& recovery_passphrase,
        int64_t now_ns
    );

    // Add signature to recovery session
    Result<void> add_signature(RecoverySession& session,
                                const GovernanceSignature& sig,
                                int64_t now_ns);

    // Execute recovery (apply changes)
    Result<void> execute(RecoverySession& session, int64_t now_ns);

    // Cancel recovery
    Result<void> cancel(RecoverySession& session);

private:
    RecoveryConfig config_;
    Result<bool> verify_recovery_package(const std::string& passphrase);
};

// ── Recovery error codes (1500-1509, category Recovery) ─────────────
namespace RecoveryErrc {
    inline constexpr ErrorCode
    NotNeeded(ErrorCategory::Recovery, 1500, Severity::Info, RetryClass::NoRetry, Recovery::None);
    inline constexpr ErrorCode
    SessionExpired(ErrorCategory::Recovery, 1501, Severity::Warn, RetryClass::NoRetry, Recovery::ManualIntervention);
    inline constexpr ErrorCode
    SessionInvalid(ErrorCategory::Recovery, 1502, Severity::Error, RetryClass::NoRetry, Recovery::ManualIntervention);
    inline constexpr ErrorCode
    PassphraseMismatch(ErrorCategory::Recovery, 1503, Severity::Error, RetryClass::NoRetry, Recovery::ManualIntervention);
    inline constexpr ErrorCode
    QuorumStillPresent(ErrorCategory::Recovery, 1504, Severity::Warn, RetryClass::NoRetry, Recovery::None);
    inline constexpr ErrorCode
    EpochRollback(ErrorCategory::Recovery, 1505, Severity::Critical, RetryClass::NoRetry, Recovery::ManualIntervention);
} // namespace RecoveryErrc

} // namespace smo::recovery
