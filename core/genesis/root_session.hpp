#pragma once

#include "../crypto/impl.hpp"
#include "../crypto/signer_context.hpp"
#include "../errors/error.hpp"
#include "../types.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>

namespace smo::genesis {

// ── RootSessionState ──────────────────────────────────────────────────
enum class RootSessionState : uint8_t {
    Inactive   = 0,
    Active     = 1,
    Expired    = 2,
    Consumed   = 3,
};

// ── RootOperation ─────────────────────────────────────────────────────
enum class RootOperation : uint8_t {
    SignJoinToken      = 0,
    SignBootstrapCSR   = 1,
    SignRecovery       = 2,
    IssueRecovery      = 3,
    RotateRoot         = 4,
    EmergencyLockdown  = 5,
    ResetEpoch         = 6,
    ConstitutionChange = 7,
};

// ── Capability ────────────────────────────────────────────────────────
// Granular capabilities for SessionPolicy.  A session's capability set
// controls which RootOperations it may execute.
enum class Capability : uint8_t {
    SignJoinToken      = 0,
    SignBootstrapCSR   = 1,
    SignRecovery       = 2,
    IssueRecovery      = 3,
    RotateRoot         = 4,
    EmergencyLockdown  = 5,
    ResetEpoch         = 6,
    ConstitutionChange = 7,
};

// ── SessionPolicy ─────────────────────────────────────────────────────
struct SessionPolicy {
    std::set<Capability> caps;

    bool check(RootOperation op) const {
        Capability c;
        switch (op) {
            case RootOperation::SignJoinToken:      c = Capability::SignJoinToken; break;
            case RootOperation::SignBootstrapCSR:   c = Capability::SignBootstrapCSR; break;
            case RootOperation::SignRecovery:       c = Capability::SignRecovery; break;
            case RootOperation::IssueRecovery:      c = Capability::IssueRecovery; break;
            case RootOperation::RotateRoot:         c = Capability::RotateRoot; break;
            case RootOperation::EmergencyLockdown:  c = Capability::EmergencyLockdown; break;
            case RootOperation::ResetEpoch:         c = Capability::ResetEpoch; break;
            case RootOperation::ConstitutionChange: c = Capability::ConstitutionChange; break;
            default: return false;
        }
        return caps.count(c) > 0;
    }

    static SessionPolicy full() {
        SessionPolicy p;
        p.caps = {
            Capability::SignJoinToken, Capability::SignBootstrapCSR,
            Capability::SignRecovery,  Capability::IssueRecovery,
            Capability::RotateRoot,    Capability::EmergencyLockdown,
            Capability::ResetEpoch,    Capability::ConstitutionChange
        };
        return p;
    }

    static SessionPolicy recovery() {
        SessionPolicy p;
        p.caps = { Capability::SignRecovery, Capability::IssueRecovery };
        return p;
    }

    static SessionPolicy bootstrap() {
        SessionPolicy p;
        p.caps = { Capability::SignJoinToken, Capability::SignBootstrapCSR };
        return p;
    }
};

// ── RootRequest ───────────────────────────────────────────────────────
struct RootRequest {
    RootOperation operation;
    Bytes          payload;
    std::string    mesh_id;      // Domain context
    std::string    requester;    // Who requested (node_id or "admin")
    std::string    reason;       // Human-readable purpose (for audit)
    uint64_t       timestamp_ns = 0;
};

// ── RootResult ────────────────────────────────────────────────────────
struct RootResult {
    bool  success = false;
    Bytes output;
};

// ── AuditEvent ────────────────────────────────────────────────────────
struct AuditEvent {
    uint64_t      audit_id = 0;
    uint64_t      timestamp_ns = 0;
    RootOperation operation;
    std::string   reason;
    bool          success = false;
    std::string   details;
};

using AuditSink = std::function<void(const AuditEvent& event)>;

// ── RootSession ───────────────────────────────────────────────────────
class RootSession {
public:
    std::string                  session_id;
    std::string                  root_node_id;
    std::string                  root_public_key;
    std::unique_ptr<crypto::SignerContext> signer;
    SessionPolicy                policy;
    AuditSink                    audit_sink;

    RootSession() = default;

    ~RootSession() { destroy(); }

    RootSession(RootSession&& other) noexcept
        : session_id(std::move(other.session_id)),
          state_(other.state_),
          root_node_id(std::move(other.root_node_id)),
          root_public_key(std::move(other.root_public_key)),
          signer(std::move(other.signer)),
          policy(std::move(other.policy)),
          audit_sink(std::move(other.audit_sink)),
          created_at_(other.created_at_),
          expires_at_(other.expires_at_),
          consumed_at_(other.consumed_at_),
          audit_counter_(other.audit_counter_)
    {
        other.state_ = RootSessionState::Consumed;
    }

    RootSession& operator=(RootSession&& other) noexcept {
        if (this != &other) {
            destroy();
            session_id     = std::move(other.session_id);
            state_         = other.state_;
            root_node_id   = std::move(other.root_node_id);
            root_public_key = std::move(other.root_public_key);
            signer         = std::move(other.signer);
            policy         = std::move(other.policy);
            audit_sink     = std::move(other.audit_sink);
            created_at_    = other.created_at_;
            expires_at_    = other.expires_at_;
            consumed_at_   = other.consumed_at_;
            audit_counter_ = other.audit_counter_;
            other.state_   = RootSessionState::Consumed;
        }
        return *this;
    }

    RootSession(const RootSession&) = delete;
    RootSession& operator=(const RootSession&) = delete;

    // ── State accessors ─────────────────────────────────────────────
    RootSessionState state() const { return state_; }
    uint64_t created_at() const { return created_at_; }
    uint64_t expires_at() const { return expires_at_; }
    uint64_t consumed_at() const { return consumed_at_; }

    bool is_valid(uint64_t now_ns) const {
        return state_ == RootSessionState::Active &&
               (expires_at_ == 0 || now_ns <= expires_at_);
    }

    // ── Core API ────────────────────────────────────────────────────
    // 1. Activate — called by RootSessionManager or after unlock()
    //    If ctx is null the existing signer (from unlock) is kept.
    void activate(const std::string& sid,
                  const std::string& node_id,
                  const std::string& pubkey,
                  std::unique_ptr<crypto::SignerContext> ctx,
                  SessionPolicy pol,
                  AuditSink sink,
                  uint64_t now_ns,
                  uint64_t ttl_ns);

    // 2. Execute a privileged operation
    //    Pipeline: Validate → Policy → SignerContext → Audit → Result
    Result<RootResult> execute(const RootRequest& req, RngRef& rng, uint64_t now_ns);

    // 3. Destroy session (zeroize + invalidate + audit)
    void destroy();

    // 4. Consume (one-shot, for single-action sessions)
    Result<void> consume(uint64_t now_ns);

    // Serialization (metadata only — SignerContext is not serialized)
    Result<Bytes> serialize() const;
    static Result<RootSession> deserialize(BytesView data);

private:
    RootSessionState state_ = RootSessionState::Inactive;
    uint64_t created_at_  = 0;
    uint64_t expires_at_  = 0;
    uint64_t consumed_at_ = 0;
    uint64_t audit_counter_ = 1; // monotonic per session

    uint64_t next_audit_id() { return audit_counter_++; }
    void emit(const AuditEvent& ev);
};

// ── RootSessionManager ────────────────────────────────────────────────
struct RootSessionManager {
    RootSession session;
    uint64_t ttl_ns = std::chrono::hours(24).count() * 1'000'000'000ULL;

    Result<std::string> start_session(const std::string& root_node_id,
                                       const std::string& root_public_key,
                                       std::unique_ptr<crypto::SignerContext> signer,
                                       SessionPolicy policy,
                                       AuditSink audit_sink,
                                       uint64_t now_ns);

    Result<void> validate_session(const std::string& session_id,
                                   uint64_t now_ns);
    Result<void> expire_session(uint64_t now_ns);
};

} // namespace smo::genesis
