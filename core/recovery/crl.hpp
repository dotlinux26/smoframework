#pragma once

#include "../errors/error.hpp"
#include "../types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace smo::recovery {

struct CRLEntry {
    std::string cert_fingerprint;
    std::string node_id_hex;
    std::string reason;
    uint64_t    epoch = 0;
    int64_t     revoked_at = 0;

    Bytes serialize() const;
    static Result<CRLEntry> deserialize(BytesView data);
};

class CRL {
public:
    CRL() = default;

    // Add a certificate to the revocation list
    Result<void> revoke(const std::string& cert_fingerprint,
                         const std::string& node_id_hex,
                         const std::string& reason,
                         uint64_t epoch,
                         int64_t now);

    // Check if a certificate is revoked
    Result<bool> is_revoked(const std::string& cert_fingerprint) const;

    // Check by epoch: all certs with epoch < given epoch are implicitly revoked
    bool is_epoch_invalid(uint64_t cert_epoch, uint64_t current_epoch) const {
        return cert_epoch < current_epoch;
    }

    // Get all entries since given epoch
    std::vector<CRLEntry> entries_since(uint64_t epoch) const;

    // Serialize entire CRL
    Bytes serialize() const;
    static Result<CRL> deserialize(BytesView data);

    // Number of entries
    size_t count() const noexcept { return entries_.size(); }

    // Clear (used during hard recovery)
    void clear() { entries_.clear(); }

private:
    std::vector<CRLEntry> entries_;
};

// ── Revocation protocol messages ────────────────────────────────────
struct RevokeCertMsg {
    std::string cert_fingerprint;
    std::string node_id_hex;
    std::string reason;
    uint64_t    epoch = 0;
    Bytes       signature;     // Signed by issuing authority

    Bytes serialize() const;
    static Result<RevokeCertMsg> deserialize(BytesView data);
};

struct RevokeAckMsg {
    std::string cert_fingerprint;
    bool        accepted = false;
    std::string error_message;

    Bytes serialize() const;
    static Result<RevokeAckMsg> deserialize(BytesView data);
};

} // namespace smo::recovery
