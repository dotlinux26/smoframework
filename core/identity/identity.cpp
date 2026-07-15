#include "identity.hpp"

#include <algorithm>
#include <cstring>

namespace smo {

// ---------------------------------------------------------------------------
// NodeID derivation
// ---------------------------------------------------------------------------

Result<NodeID> node_id_from_public_key(BytesView pk, const HashImpl& hash) {
    if (!hash.hash) {
        return SMO_ERR_IDENTITY(100, Error, NoRetry, RetryOperation,
                                "hash implementation is null");
    }
    auto digest = hash.hash(pk);
    if (!digest) return digest.error();

    NodeID id;
    size_t copy = std::min(digest.value().size(), id.value.size());
    std::memcpy(id.value.data(), digest.value().data(), copy);
    return id;
}

// ---------------------------------------------------------------------------
// IdentityState helpers
// ---------------------------------------------------------------------------

bool is_valid_transition(IdentityState from, IdentityState to) noexcept {
    switch (from) {
    case IdentityState::Uninitialized:
        return to == IdentityState::KeypairReady;
    case IdentityState::KeypairReady:
        return to == IdentityState::CertificatePending;
    case IdentityState::CertificatePending:
        return to == IdentityState::Enrolled;
    case IdentityState::Enrolled:
        return to == IdentityState::Active ||
               to == IdentityState::Suspended ||
               to == IdentityState::Retired;
    case IdentityState::Active:
        return to == IdentityState::Suspended ||
               to == IdentityState::Retired ||
               to == IdentityState::KeypairReady; // key rotation re-enters
    case IdentityState::Suspended:
        return to == IdentityState::Active ||
               to == IdentityState::Retired;
    case IdentityState::Retired:
        return false; // terminal state
    }
    return false;
}

const char* to_string(IdentityState s) noexcept {
    switch (s) {
    case IdentityState::Uninitialized:      return "Uninitialized";
    case IdentityState::KeypairReady:       return "KeypairReady";
    case IdentityState::CertificatePending: return "CertificatePending";
    case IdentityState::Enrolled:           return "Enrolled";
    case IdentityState::Active:             return "Active";
    case IdentityState::Suspended:          return "Suspended";
    case IdentityState::Retired:            return "Retired";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

Result<Identity> Identity::create(const CryptoProvider& crypto, RngRef& rng) {
    if (!crypto.signer.generate_keypair) {
        return SMO_ERR_IDENTITY(100, Error, NoRetry, RetryOperation,
                                "crypto provider has no keygen");
    }
    if (!crypto.hash.hash) {
        return SMO_ERR_IDENTITY(100, Error, NoRetry, RetryOperation,
                                "crypto provider has no hash");
    }

    auto kp = crypto.signer.generate_keypair(rng);
    if (!kp) return kp.error();

    auto pk = std::move(kp.value().public_key);
    auto sk = std::move(kp.value().secret_key);

    auto nid = node_id_from_public_key(pk, crypto.hash);
    if (!nid) return nid.error();

    return Identity(nid.value(), crypto.suite_id,
                    std::move(pk), std::move(sk),
                    IdentityState::KeypairReady);
}

Result<Identity> Identity::load(BytesView public_key, BytesView secret_key,
                                 CryptoSuiteID suite_id) {
    if (public_key.empty() || secret_key.empty()) {
        return SMO_ERR_IDENTITY(100, Error, NoRetry, RetryOperation,
                                "empty key material");
    }

    // Derive NodeID from stored public key
    // (caller must ensure the CryptoProvider is available for full hash;
    //  for now just store the public key bytes)
    NodeID id;
    size_t copy = std::min(public_key.size(), id.value.size());
    std::memcpy(id.value.data(), public_key.data(), copy);

    return Identity(id, suite_id,
                    Bytes(public_key.begin(), public_key.end()),
                    Bytes(secret_key.begin(), secret_key.end()),
                    IdentityState::KeypairReady);
}

Result<void> Identity::transition_to(IdentityState new_state) noexcept {
    if (!is_valid_transition(state_, new_state)) {
        return SMO_ERR_IDENTITY(107, Error, NoRetry, RestartFSM,
                                "invalid state transition");
    }
    state_ = new_state;
    return {};
}

} // namespace smo
