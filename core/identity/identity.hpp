#pragma once

#include "../crypto/impl.hpp"
#include "../errors/error.hpp"
#include "../types.hpp"

#include <array>
#include <cstdint>

namespace smo {

// ---------------------------------------------------------------------------
// NodeID — 32-byte unique node identifier
//
// NodeID = Blake3(NodePublicKey) per SPEC.md §16.4
// ---------------------------------------------------------------------------
struct NodeID {
    std::array<uint8_t, 32> value{};

    bool operator==(const NodeID& other) const noexcept = default;
    bool operator!=(const NodeID& other) const noexcept = default;
};

// Derive NodeID from a public key using a hash implementation.
// Returns the first 32 bytes of hash(pk).
Result<NodeID> node_id_from_public_key(BytesView pk, const HashImpl& hash);

// ---------------------------------------------------------------------------
// IdentityState — node identity lifecycle FSM
// ---------------------------------------------------------------------------
enum class IdentityState : uint8_t {
    Uninitialized      = 0,
    KeypairReady       = 1,
    CertificatePending = 2,
    Enrolled           = 3,
    Active             = 4,
    Suspended          = 5,
    Retired            = 6,
};

// Valid state transitions per SPEC.md §7.10.1
bool is_valid_transition(IdentityState from, IdentityState to) noexcept;

const char* to_string(IdentityState s) noexcept;

// ---------------------------------------------------------------------------
// Identity — holds a node's keypair and lifecycle state
// ---------------------------------------------------------------------------
class Identity {
public:
    Identity() = default;

    // Generate a new keypair and derive NodeID.
    static Result<Identity> create(const CryptoProvider& crypto, RngRef& rng);

    // Load from previously stored key material.
    static Result<Identity> load(BytesView public_key, BytesView secret_key,
                                  CryptoSuiteID suite_id);

    // Accessors
    const NodeID& node_id() const noexcept { return node_id_; }
    CryptoSuiteID suite_id() const noexcept { return suite_id_; }
    IdentityState state() const noexcept { return state_; }
    BytesView public_key() const noexcept { return public_key_; }
    BytesView secret_key() const noexcept { return secret_key_; }

    // State machine
    Result<void> transition_to(IdentityState new_state) noexcept;

private:
    Identity(NodeID node_id, CryptoSuiteID suite_id,
             Bytes public_key, Bytes secret_key,
             IdentityState state) noexcept
        : node_id_(node_id), suite_id_(suite_id),
          public_key_(std::move(public_key)),
          secret_key_(std::move(secret_key)),
          state_(state) {}

    NodeID        node_id_{};
    CryptoSuiteID suite_id_ = 0;
    Bytes         public_key_;
    Bytes         secret_key_;
    IdentityState state_ = IdentityState::Uninitialized;
};

} // namespace smo
