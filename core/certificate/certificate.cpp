#include "certificate.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace smo {

// ---------------------------------------------------------------------------
// Helpers: serialize primitives
// ---------------------------------------------------------------------------

static void append_u32(Bytes& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

static void append_u64(Bytes& out, uint64_t v) {
    append_u32(out, static_cast<uint32_t>(v >> 32));
    append_u32(out, static_cast<uint32_t>(v));
}

static void append_bytes(Bytes& out, BytesView data) {
    append_u32(out, static_cast<uint32_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
}

static bool read_u32(BytesView data, size_t& offset, uint32_t& out) {
    if (offset + 4 > data.size()) return false;
    out = (static_cast<uint32_t>(data[offset]) << 24) |
          (static_cast<uint32_t>(data[offset + 1]) << 16) |
          (static_cast<uint32_t>(data[offset + 2]) << 8) |
          static_cast<uint32_t>(data[offset + 3]);
    offset += 4;
    return true;
}

static bool read_u64(BytesView data, size_t& offset, uint64_t& out) {
    uint32_t hi, lo;
    if (!read_u32(data, offset, hi)) return false;
    if (!read_u32(data, offset, lo)) return false;
    out = (static_cast<uint64_t>(hi) << 32) | lo;
    return true;
}

static bool read_bytes(BytesView data, size_t& offset, Bytes& out) {
    uint32_t len;
    if (!read_u32(data, offset, len)) return false;
    if (offset + len > data.size()) return false;
    out.assign(data.begin() + static_cast<ptrdiff_t>(offset),
               data.begin() + static_cast<ptrdiff_t>(offset + len));
    offset += len;
    return true;
}

// ---------------------------------------------------------------------------
// Role
// ---------------------------------------------------------------------------

const char* to_string(Role r) noexcept {
    switch (r) {
    case Role::Root:        return "Root";
    case Role::Authority:   return "Authority";
    case Role::Contributor: return "Contributor";
    case Role::Reader:      return "Reader";
    case Role::Observer:    return "Observer";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// Certificate
// ---------------------------------------------------------------------------

Bytes Certificate::serialize() const {
    Bytes out;
    append_bytes(out, mesh_id);
    append_bytes(out, subject_pubkey);
    append_bytes(out, issuer_pubkey);
    out.push_back(static_cast<uint8_t>(role));
    append_bytes(out, capabilities);
    append_u64(out, epoch);
    append_u64(out, static_cast<uint64_t>(not_before));
    append_u64(out, static_cast<uint64_t>(not_after));
    return out;
}

Result<Certificate> Certificate::deserialize(BytesView data) {
    Certificate cert;
    size_t offset = 0;

    if (!read_bytes(data, offset, cert.mesh_id))
        return SMO_ERR_CERT(203, Error, NoRetry, Reenroll,
                            "failed to parse mesh_id");
    if (!read_bytes(data, offset, cert.subject_pubkey))
        return SMO_ERR_CERT(203, Error, NoRetry, Reenroll,
                            "failed to parse subject_pubkey");
    if (!read_bytes(data, offset, cert.issuer_pubkey))
        return SMO_ERR_CERT(203, Error, NoRetry, Reenroll,
                            "failed to parse issuer_pubkey");

    if (offset >= data.size())
        return SMO_ERR_CERT(203, Error, NoRetry, Reenroll,
                            "truncated: missing role");
    cert.role = static_cast<Role>(data[offset]);
    offset += 1;

    if (!read_bytes(data, offset, cert.capabilities))
        return SMO_ERR_CERT(203, Error, NoRetry, Reenroll,
                            "failed to parse capabilities");
    if (!read_u64(data, offset, cert.epoch))
        return SMO_ERR_CERT(203, Error, NoRetry, Reenroll,
                            "failed to parse epoch");

    uint64_t nb, na;
    if (!read_u64(data, offset, nb))
        return SMO_ERR_CERT(203, Error, NoRetry, Reenroll,
                            "failed to parse not_before");
    if (!read_u64(data, offset, na))
        return SMO_ERR_CERT(203, Error, NoRetry, Reenroll,
                            "failed to parse not_after");
    cert.not_before = static_cast<int64_t>(nb);
    cert.not_after  = static_cast<int64_t>(na);

    return cert;
}

Result<Bytes> Certificate::cert_hash(const HashImpl& hash) const {
    auto body = serialize();
    return hash.hash(body);
}

Result<bool> Certificate::verify(const SignerImpl& signer) const {
    if (signature.empty()) {
        return SMO_ERR_CERT(204, Alert, NoRetry, None,
                            "certificate has no signature");
    }
    auto body = serialize();
    return signer.verify(body, signature, issuer_pubkey);
}

// ---------------------------------------------------------------------------
// CertificateChain
// ---------------------------------------------------------------------------

Result<void> CertificateChain::verify(const CryptoProvider& crypto,
                                       BytesView root_pubkey) const {
    if (certs.empty()) {
        return SMO_ERR_CERT(200, Error, NoRetry, Reenroll,
                            "empty certificate chain");
    }
    if (!crypto.signer.verify) {
        return SMO_ERR_CERT(203, Error, NoRetry, Reenroll,
                            "crypto provider has no verify");
    }

    // Verify each cert in the chain
    for (size_t i = 0; i < certs.size(); ++i) {
        const auto& cert = certs[i];

        // Check signature
        auto body = cert.serialize();
        // Determine verifying key: for this cert, use issuer_pubkey.
        // For the chain to be valid, issuer_pubkey must be the subject_pubkey
        // of the next cert (or root_pubkey for the last).
        auto ok = crypto.signer.verify(body, cert.signature, cert.issuer_pubkey);
        if (!ok) return ok.error();
        if (!ok.value()) {
            return SMO_ERR_CERT(204, Alert, NoRetry, None,
                                "certificate signature invalid");
        }

        // Chain linkage: this cert's issuer must match next cert's subject
        if (i + 1 < certs.size()) {
            const auto& next = certs[i + 1];
            if (cert.issuer_pubkey.size() != next.subject_pubkey.size() ||
                std::memcmp(cert.issuer_pubkey.data(),
                            next.subject_pubkey.data(),
                            cert.issuer_pubkey.size()) != 0)
            {
                return SMO_ERR_CERT(217, Error, NoRetry, ManualIntervention,
                                    "chain linkage broken");
            }
        } else {
            // Last cert in chain: issuer must be the trusted root
            if (cert.issuer_pubkey.size() != root_pubkey.size() ||
                std::memcmp(cert.issuer_pubkey.data(), root_pubkey.data(),
                            cert.issuer_pubkey.size()) != 0)
            {
                return SMO_ERR_CERT(218, Alert, NoRetry, ManualIntervention,
                                    "root key fingerprint mismatch");
            }
        }
    }

    return {};
}

bool CertificateChain::is_valid_at(int64_t timestamp) const noexcept {
    for (const auto& cert : certs) {
        if (!cert.is_valid_at(timestamp)) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// CertificateSigningRequest
// ---------------------------------------------------------------------------

Bytes CertificateSigningRequest::serialize() const {
    Bytes out;
    append_bytes(out, new_public_key);
    append_bytes(out, mesh_id);
    append_bytes(out, old_cert_hash);
    append_u64(out, static_cast<uint64_t>(timestamp));
    return out;
}

Result<CertificateSigningRequest> CertificateSigningRequest::deserialize(
    BytesView data) {
    CertificateSigningRequest csr;
    size_t offset = 0;

    if (!read_bytes(data, offset, csr.new_public_key))
        return SMO_ERR_CERT(209, Alert, NoRetry, None,
                            "failed to parse CSR new_public_key");
    if (!read_bytes(data, offset, csr.mesh_id))
        return SMO_ERR_CERT(209, Alert, NoRetry, None,
                            "failed to parse CSR mesh_id");
    if (!read_bytes(data, offset, csr.old_cert_hash))
        return SMO_ERR_CERT(209, Alert, NoRetry, None,
                            "failed to parse CSR old_cert_hash");

    uint64_t ts;
    if (!read_u64(data, offset, ts))
        return SMO_ERR_CERT(209, Alert, NoRetry, None,
                            "failed to parse CSR timestamp");
    csr.timestamp = static_cast<int64_t>(ts);

    return csr;
}

Result<bool> CertificateSigningRequest::verify(
    const SignerImpl& signer, BytesView signer_pubkey) const {
    if (signature.empty()) {
        return SMO_ERR_CERT(209, Alert, NoRetry, None,
                            "CSR has no signature");
    }
    auto body = serialize();
    return signer.verify(body, signature, signer_pubkey);
}

Result<void> CertificateSigningRequest::sign(
    const SignerImpl& signer, BytesView secret_key, RngRef& rng) {
    auto body = serialize();
    auto sig = signer.sign(body, secret_key, rng);
    if (!sig) return sig.error();
    signature = std::move(sig.value());
    return {};
}

} // namespace smo
