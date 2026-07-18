#pragma once

#include "../certificate/certificate.hpp"
#include "../crypto/impl.hpp"
#include "../errors/error.hpp"
#include "../types.hpp"
#include "root_session.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace smo::genesis {

struct RecoveryPackage {
    std::string mesh_id;
    std::string root_public_key;
    Bytes root_keypair_encrypted;    // Encrypted with recovery passphrase
    std::string recovery_passphrase_hash; // Blake3 hash of passphrase
    uint32_t epoch = 1;
    uint32_t manifest_version = 1;
    std::string genesis_manifest_json;
    uint64_t created_at = 0;

    Result<Bytes> serialize() const;
    static Result<RecoveryPackage> deserialize(BytesView data);

    // Verify integrity: check passphrase hash matches
    bool verify_passphrase(const std::string& passphrase) const;

    // Unlock: verify passphrase + version check, decrypt keypair, return RootSession.
    // The RootSession wraps a SignerContext (software backend by default);
    // call RootSession::destroy() or let it go out of scope to zeroize
    // key material inside the context.
    // Needs signer to construct the SignerContext for the decrypted key.
    Result<RootSession> unlock(const std::string& passphrase,
                                const HashImpl& hash,
                                const AeadImpl& aead,
                                const SignerImpl& signer,
                                RngRef& rng) const;
};

struct EmergencyRecoveryToken {
    Bytes token_blob;              // Root-signed recovery authorization
    std::string authorized_by;     // Root NodeID
    uint64_t created_at = 0;
    uint64_t expires_at = 0;
    uint32_t target_epoch = 0;

    bool is_valid(uint64_t now_ns) const {
        return expires_at == 0 || now_ns <= expires_at;
    }
};

} // namespace smo::genesis
