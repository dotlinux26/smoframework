#pragma once

#include "../capability/capability.h"
#include "../certificate/certificate.hpp"
#include "../crypto/impl.hpp"
#include "../errors/error.hpp"
#include "../identity/identity.hpp"
#include "../types.hpp"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace smo {

// ---------------------------------------------------------------------------
// Session error codes (500-511)
// ---------------------------------------------------------------------------
namespace SessionErrc {
    inline constexpr ErrorCode
    OpenFailed(ErrorCategory::Session, 500, Severity::Error, RetryClass::RetrySafe, Recovery::Reconnect);
    inline constexpr ErrorCode
    Closed(ErrorCategory::Session, 501, Severity::Info, RetryClass::RetrySafe, Recovery::Reconnect);
    inline constexpr ErrorCode
    Expired(ErrorCategory::Session, 502, Severity::Warn, RetryClass::RetrySafe, Recovery::Reconnect);
    inline constexpr ErrorCode
    Rejected(ErrorCategory::Session, 503, Severity::Error, RetryClass::NoRetry, Recovery::Reconnect);
    inline constexpr ErrorCode
    CapExceeded(ErrorCategory::Session, 504, Severity::Warn, RetryClass::NoRetry, Recovery::None);
    inline constexpr ErrorCode
    CapNotGranted(ErrorCategory::Session, 507, Severity::Error, RetryClass::NoRetry, Recovery::None);
} // namespace SessionErrc

// ---------------------------------------------------------------------------
// SessionId — 128-bit unique session identifier
// ---------------------------------------------------------------------------
struct SessionId {
    std::array<uint8_t, 16> bytes{};

    bool operator==(const SessionId& other) const noexcept = default;
    bool operator!=(const SessionId& other) const noexcept = default;

    // Derive from a byte sequence (e.g., Blake3(peer_pubkey || nonce))
    static Result<SessionId> derive(BytesView seed, const HashImpl& hash);

    Bytes to_bytes() const;
    static Result<SessionId> from_bytes(BytesView data);
};

// ---------------------------------------------------------------------------
// SessionState — 5-state FSM per RFC 0014
// ---------------------------------------------------------------------------
enum class SessionState : uint8_t {
    Closed       = 0,
    Handshake    = 1,
    Established  = 2,
    Active       = 3,
    Renewing     = 4,
};

const char* to_string(SessionState s) noexcept;

// ---------------------------------------------------------------------------
// SessionEvent — events that drive session FSM transitions
// ---------------------------------------------------------------------------
enum class SessionEvent : uint8_t {
    OpenRequest      = 0,  // Received SESSION_OPEN
    Established      = 1,  // Handshake complete
    Activate         = 2,  // Contract starts
    CompleteContract = 3,  // Contract ends
    Renew            = 4,  // Renewal initiated
    Close            = 5,  // SESSION_CLOSE received
    Timeout          = 6,  // TTL or idle timeout
    Error            = 7,  // Protocol error
};

// Valid transitions per RFC 0014 §3
bool is_valid_transition(SessionState from, SessionEvent event) noexcept;
SessionState apply_transition(SessionState from, SessionEvent event) noexcept;

// ---------------------------------------------------------------------------
// Session — FSM-driven peer session
// ---------------------------------------------------------------------------
class Session {
public:
    Session() = default;

    // Create in Handshake state
    static Result<Session> create(SessionId id, NodeID peer_id,
                                   Certificate peer_cert,
                                   CapabilitySet capabilities,
                                   int64_t now, uint64_t ttl_ns);

    const SessionId& id() const noexcept { return id_; }
    SessionState state() const noexcept { return state_; }
    const NodeID& peer_id() const noexcept { return peer_id_; }
    const Certificate& peer_cert() const noexcept { return peer_cert_; }
    const CapabilitySet& capabilities() const noexcept { return capabilities_; }

    int64_t created_at() const noexcept { return created_at_; }
    int64_t expires_at() const noexcept { return expires_at_; }
    int64_t last_active() const noexcept { return last_active_; }

    // FSM transition
    Result<void> on_event(SessionEvent event, int64_t now) noexcept;

    // Renew session TTL
    Result<void> renew(int64_t now, uint64_t ttl_ns) noexcept;

    // Check if session is valid at a given timestamp
    bool is_valid_at(int64_t now) const noexcept {
        return state_ != SessionState::Closed && now < expires_at_;
    }

    // Verify a capability is in the session's set
    bool has_capability(Capability cap) const noexcept {
        return capabilities_.test(static_cast<size_t>(cap));
    }

    // Serialization
    Bytes serialize() const;
    static Result<Session> deserialize(BytesView data);

private:
    SessionId     id_{};
    SessionState  state_ = SessionState::Closed;
    NodeID        peer_id_{};
    Certificate   peer_cert_;
    CapabilitySet capabilities_;
    int64_t       created_at_  = 0;
    int64_t       expires_at_  = 0;
    int64_t       last_active_ = 0;
};

// ---------------------------------------------------------------------------
// SessionManager — manages all active sessions
// ---------------------------------------------------------------------------
class SessionManager {
public:
    SessionManager() = default;

    // Create a new session and add to manager
    Result<Session*> open(Session session);

    // Look up a session by ID
    Session* lookup(const SessionId& id);

    // Close a session (moves to Closed state)
    Result<void> close(const SessionId& id, int64_t now);

    // Transition an existing session
    Result<void> transition(const SessionId& id, SessionEvent event, int64_t now);

    // Tick — expire sessions that have timed out
    void tick(int64_t now);

    // Number of active sessions
    size_t active_count() const noexcept { return sessions_.size(); }

    // Collect and remove closed sessions
    void collect_garbage();

    // Serialize all sessions for crash recovery
    Bytes serialize_all() const;

private:
    std::unordered_map<uint64_t, Session> sessions_;

    static uint64_t to_key(const SessionId& id);
};

// ---------------------------------------------------------------------------
// Session message serialization (wire format)
// ---------------------------------------------------------------------------

// SESSION_OPEN message: nonce(32) + signature(64) + cert(variable)
struct SessionOpenMsg {
    Bytes     nonce;       // 32 bytes
    Bytes     signature;   // 64 bytes (Ed25519)
    Bytes     cert_data;   // Serialized certificate

    Bytes serialize() const;
    static Result<SessionOpenMsg> deserialize(BytesView data);
};

// SESSION_CLOSE message: reason(1) + signature(64)
struct SessionCloseMsg {
    uint8_t   reason{0};  // 0=normal, 1=expiry, 2=error
    Bytes     signature;  // 64 bytes

    Bytes serialize() const;
    static Result<SessionCloseMsg> deserialize(BytesView data);
};

} // namespace smo
