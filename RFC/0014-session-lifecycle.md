# RFC 0014 — Session Lifecycle

## Status
ACCEPTED — Session lifecycle frozen. Two-factor binding (cert + nonce) confirmed.

## Problem
Every contract execution requires an established session between Requester and Responder. Sessions bind identity (certificate + signed nonce), scope capabilities, and carry encryption state. Without a well-defined lifecycle, sessions leak, capabilities escape their scope, and replay attacks become possible.

## Decisions

### 1. Session is two-factor bound: certificate + signed nonce
Opening a session requires:
1. A valid `Certificate` (chain verifiable up to Root, epoch >= current, not expired).
2. A freshly generated nonce signed by the peer's private key.

Both proofs are required. A stolen certificate file is useless without the private key; a stolen private key is useless without a valid certificate.

### 2. Session carries a scoped CapabilitySet
When a session is opened, the Responder determines the effective `CapabilitySet` for that session based on the peer's Certificate capabilities and local policy. The session's CapabilitySet is a subset (possibly equal) of the Certificate's capabilities. Every contract execution checks `session.verify_capability(opcode)` — the certificate is not re-checked.

### 3. Session lifecycle states
```
CLOSED → HANDSHAKE → ESTABLISHED → ACTIVE → CLOSED
                       ↓
                    RENEWING
                       ↓
                  ESTABLISHED (extended TTL)
```
- CLOSED: initial state, no resources allocated.
- HANDSHAKE: nonce exchange, certificate verification, key derivation in progress.
- ESTABLISHED: session created, capability set assigned, no active contract.
- ACTIVE: contract in progress. Session cannot be closed while ACTIVE.
- RENEWING: session TTL approaching expiry; renewal handshake in progress.

### 4. Session TTL and expiry
Every session has a `max_ttl` (configurable, default 24 hours) and an `idle_timeout` (default 30 minutes). After `idle_timeout` of no contract activity, the session transitions to CLOSED. After `max_ttl`, the session expires and must be re-established. Renewal resets both timers.

### 5. Session carries optional encryption state
If transport-level encryption is not used (e.g., UDP), the session may derive a symmetric key for payload encryption. This key is stored in the session, not in the certificate or node identity. Session close destroys the key.

### 6. Session store is crash-recoverable
`session_store` persists all ESTABLISHED and ACTIVE sessions. On node restart, the session store is scanned: sessions that were ACTIVE are transitioned to FAILED (contracts are orphans); sessions that were ESTABLISHED are closed gracefully.

## Interfaces

```cpp
enum class SessionState : uint8_t {
    Closed, Handshake, Established, Active, Renewing
};

class Session {
    SessionID id() const;
    SessionState state() const;
    NodeID peer_id() const;
    const Certificate& peer_cert() const;
    const CapabilitySet& capabilities() const;

    // Called by session manager only
    Result<void> transition(SessionEvent event);
    Result<bool> verify_capability(Opcode opcode) const;
    Result<void> renew();
    Result<void> close();

    // Encryption (optional, nullptr if not negotiated)
    AEAD* encryption() const;
};

class SessionManager {
    Result<std::unique_ptr<Session>> open(
        TransportSession& transport,
        const Certificate& peer_cert,
        const NodeIdentity& identity,
        const CapabilitySet& local_capabilities,
        RngRef rng);
    Result<Session*> lookup(SessionID id);
    Result<void> close(SessionID id);
    Result<void> tick();  // check idle timeout, expiry; close stale sessions
    Result<void> recover(const std::filesystem::path& session_store_path);
};
```

## Consequences
- Two-factor session binding prevents key-only and cert-only attacks.
- Session-scoped CapabilitySet limits blast radius: a compromised session does not leak permanent capabilities.
- TTL + idle timeout prevent resource leaks from abandoned sessions.
- Renewal extends long-lived sessions without re-authentication.
- Crash recovery closes outstanding sessions deterministically, preventing ghost sessions.
- Optional per-session encryption enables transport-agnostic secure channels without assuming transport-level security.
