#pragma once

#include "../crypto/impl.hpp"
#include "../errors/error.hpp"
#include "../types.hpp"

#include <cstdint>
#include <vector>

namespace smo {

// ---------------------------------------------------------------------------
// Role — node role within a mesh
//
// 6 canonical identity roles (Layer 1). Runtime permissions are governed
// by Policy (Layer 2) — see core/capability/capability.h.
//
// Reader is deprecated → use Member instead. Backward compat is preserved
// during deserialization with a warning.
// ---------------------------------------------------------------------------
enum class Role : uint8_t {
    Root        = 0,
    Authority   = 1,
    Contributor = 2,
    Reader      = 3,  // DEPRECATED — use Member instead
    Observer    = 4,
    Member      = 5,
    Recovery    = 6,
};

const char* to_string(Role r) noexcept;

// Convert deprecated Reader to Member with warning flag
inline constexpr Role role_deprecate_reader(Role r, bool& warned) {
    if (r == Role::Reader) {
        warned = true;
        return Role::Member;
    }
    return r;
}

// Maximum role value for validation
inline constexpr Role Role_Max = Role::Recovery;

// ---------------------------------------------------------------------------
// Certificate error codes (codes 220-229: name/identity validation)
// ---------------------------------------------------------------------------
namespace CertErrc {
    inline constexpr ErrorCode
    DisplayNameAlreadyExists(ErrorCategory::Certificate, 220, Severity::Warn,
                             RetryClass::RetrySafe, Recovery::None);
    inline constexpr ErrorCode
    InvalidDisplayName(ErrorCategory::Certificate, 221, Severity::Warn,
                       RetryClass::NoRetry, Recovery::None);
    inline constexpr ErrorCode
    DisplayNameTooLong(ErrorCategory::Certificate, 222, Severity::Warn,
                       RetryClass::NoRetry, Recovery::None);
} // namespace CertErrc

// ---------------------------------------------------------------------------
// CertStatus — certificate lifecycle status
// ---------------------------------------------------------------------------
enum class CertStatus : int8_t {
    Active    = 1,
    Revoked   = 0,
    Suspended = -1,
};

// ---------------------------------------------------------------------------
// Certificate — binds a public key to a mesh, role, epoch, and capabilities
// ---------------------------------------------------------------------------
class Certificate {
public:
    Bytes    subject_pubkey;    // The node's public key
    Bytes    issuer_pubkey;     // The issuer's public key (empty if Root self-signed)
    Bytes    mesh_id;           // 32-byte mesh identifier
    Role     role           = Role::Reader;
    std::string display_name;  // human-friendly node name (signed metadata)
    Bytes    capabilities;      // Opaque capability blob
    uint64_t epoch          = 0;
    int64_t  not_before     = 0; // Unix timestamp
    int64_t  not_after      = 0; // Unix timestamp
    Bytes    signature;         // Signed by issuer's private key

    Certificate() = default;

    // Serialize all fields except signature into canonical bytes.
    Bytes serialize() const;

    // Serialize all fields INCLUDING signature for wire/storage format.
    Bytes serialize_full() const;

    // Deserialize from canonical bytes. Returns error if malformed.
    static Result<Certificate> deserialize(BytesView data);

    // Compute cert_hash = Blake3(serialize())
    Result<Bytes> cert_hash(const HashImpl& hash) const;

    // Verify this certificate's signature using the issuer's public key.
    // For self-signed (Root): issuer_pubkey must equal subject_pubkey.
    Result<bool> verify(const SignerImpl& signer) const;

    // Check temporal validity
    bool is_valid_at(int64_t timestamp) const noexcept {
        return timestamp >= not_before && timestamp <= not_after;
    }
};

// ---------------------------------------------------------------------------
// CertificateChain — ordered list from leaf to root
// ---------------------------------------------------------------------------
class CertificateChain {
public:
    std::vector<Certificate> certs; // leaf → intermediates → root

    // Verify the entire chain up to a trusted root public key.
    // For each cert: issuer_pubkey must match next cert's subject_pubkey.
    // The last cert must be self-signed (issuer_pubkey == subject_pubkey).
    Result<void> verify(const CryptoProvider& crypto, BytesView root_pubkey) const;

    // Check all certs are temporally valid at `timestamp`
    bool is_valid_at(int64_t timestamp) const noexcept;

    // Add a certificate to the chain
    void push_back(Certificate cert) { certs.push_back(std::move(cert)); }
    size_t size() const { return certs.size(); }
    bool empty() const { return certs.empty(); }
};

// ---------------------------------------------------------------------------
// CertificateSigningRequest — proves node identity and requests a cert
// ---------------------------------------------------------------------------
class CertificateSigningRequest {
public:
    Bytes     new_public_key;   // The new node public key to certify
    Bytes     mesh_id;          // Target mesh
    Bytes     old_cert_hash;    // Hash of current certificate (for rotation)
    std::string display_name;  // Proposed display name (unique within mesh)
    std::string platform;       // OS platform, e.g. "linux", "windows"
    std::string version;        // SMO runtime version, e.g. "3.2.1"
    int64_t   timestamp     = 0;
    Bytes     signature;        // Signed by old private key (or new for initial)

    // Serialize all fields except signature
    Bytes serialize() const;

    // Serialize body only (without signature) for signing/verification
    Bytes serialize_body() const;

    // Deserialize from canonical bytes
    static Result<CertificateSigningRequest> deserialize(BytesView data);

    // Verify CSR signature against the provided public key
    Result<bool> verify(const SignerImpl& signer, BytesView signer_pubkey) const;

    // Sign the CSR with a private key
    Result<void> sign(const SignerImpl& signer, BytesView secret_key,
                      RngRef& rng);
};

} // namespace smo
