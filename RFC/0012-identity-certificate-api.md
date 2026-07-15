# RFC 0012 — Identity & Certificate API (Engineering)

## Status
ACCEPTED — engineering supplement to RFC 0006. Interfaces frozen for Stage 1 implementation.

## Problem
RFC 0006 defines the conceptual identity model (Ed25519, three-tier hierarchy, epoch-based revocation). This RFC defines the concrete C++ interfaces for keypair management, certificate chain verification, CSR generation, and lifecycle transitions required before any node can enroll or authenticate.

## Decisions

### 1. `NodeIdentity` owns the keypair lifecycle
A `NodeIdentity` object is created by `NodeIdentity::generate(SuiteID)` during `smo-node init`. It lives for the process lifetime. All signing and key-requiring operations reference this object — no other module holds a raw private key.

### 2. Keypair is stored in node_store, never serialized to wire
`node_store` reads/writes opaque bytes. The identity layer handles serialization (PKCS#8-style wrapper for Ed25519, Suite ID + key material). Private key bytes never enter the transport or protocol layers.

### 3. Certificate chain is a linked list of Certificate objects
Each `Certificate` contains: `subject_pubkey`, `issuer_pubkey` (or Root marker), `mesh_id`, `role`, `capabilities`, `epoch`, `not_before`, `not_after`, `signature`. Verification walks the chain: node → Authority → ..., terminating at a self-signed Root certificate.

### 4. CSR format for key rotation
`CertificateSigningRequest` contains: `new_public_key`, `mesh_id`, `old_cert_hash`, `timestamp`, `signature(old_privkey, csr_blob)`. Authority verifies the old key's signature before issuing a new certificate. This enables online key rotation without re-enrollment.

### 5. Lifecycle states
```
UNINITIALIZED → KEYPAIR_READY → CERTIFICATE_PENDING → ENROLLED → ACTIVE
                                                                  ↓
                                                             SUSPENDED
                                                                  ↓
                                                             RETIRED
```
Each transition requires specific inputs (e.g., ENROLLED → ACTIVE requires a valid certificate and matched mesh epoch).

## Interfaces

```cpp
struct Certificate {
    std::vector<uint8_t> subject_pubkey;
    std::vector<uint8_t> issuer_pubkey;      // empty if self-signed (Root)
    MeshID               mesh_id;
    Role                 role;
    CapabilitySet        capabilities;
    Epoch                epoch;
    TimePoint            not_before;
    TimePoint            not_after;
    std::vector<uint8_t> signature;
};

struct CertificateChain {
    std::vector<Certificate> certs;  // leaf → intermediate → root
    Result<void> verify(const CryptoProvider& crypto) const;
    Result<bool> is_valid_at(TimePoint tp) const;
};

struct CertificateSigningRequest {
    std::vector<uint8_t> new_public_key;
    MeshID               mesh_id;
    Hash256              old_cert_hash;     // hash of current cert
    TimePoint            timestamp;
    std::vector<uint8_t> signature;         // signed by old private key
};

class NodeIdentity {
    static Result<NodeIdentity> generate(CryptoSuiteID suite_id, RngRef rng);
    static Result<NodeIdentity> load(const std::filesystem::path& node_store_path);
    Result<void> store(const std::filesystem::path& node_store_path) const;

    NodeID node_id() const;                       // Blake3(public_key)
    std::span<const uint8_t> public_key() const;
    const Keypair& keypair() const;

    Result<CertificateSigningRequest> create_csr(
        const MeshID& mesh_id, RngRef rng);
    Result<void> install_certificate(const Certificate& cert);
    Result<CertificateChain> build_chain(
        const std::vector<Certificate>& pool) const;

    Result<void> rotate_keypair(RngRef rng);  // generates new keypair, old key signs CSR
    Result<void> suspend();
    Result<void> activate();
    Result<void> retire();
    IdentityState state() const;
};

enum class IdentityState : uint8_t {
    Uninitialized, KeypairReady, CertificatePending,
    Enrolled, Active, Suspended, Retired
};
```

## Consequences
- `NodeIdentity` is the single source of truth for key material. No other module accesses private key bytes.
- Certificate chain verification is explicit at every trust boundary (session open, contract accept, governance action).
- CSR + key rotation enables key refresh without node re-enrollment — critical for long-lived nodes.
- Lifecycle states map directly to CLI commands (`smo-node init`, `smo-node suspend`, etc.).
- All identity operations take `RngRef`, enabling deterministic testing and audit replay.
