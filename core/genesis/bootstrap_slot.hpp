#pragma once

#include "../errors/error.hpp"
#include "../types.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace smo::genesis {

enum class SlotStatus : uint8_t {
    Vacant    = 0,
    Claimed   = 1,
    Fulfilled = 2,
    Expired   = 3,
    Revoked   = 4,
};

struct BootstrapSlot {
    uint32_t index = 0;
    SlotStatus status = SlotStatus::Vacant;
    std::string role;            // role requested (Authority, Member, etc.)
    std::string node_public_key; // public key of the claimant node
    std::string join_token_id;   // Join Token nonce used to claim
    uint64_t claimed_at = 0;     // timestamp_ns
    uint64_t expires_at = 0;     // timestamp_ns (creation + TTL)
    std::string signed_csr;      // Signed CSR (Root-signed)
    uint64_t fulfilled_at = 0;   // timestamp_ns

    bool is_expired(uint64_t now_ns) const {
        return expires_at > 0 && now_ns > expires_at;
    }

    Result<Bytes> serialize() const;
    static Result<BootstrapSlot> deserialize(BytesView data);
};

struct BootstrapSlotConfig {
    uint32_t count = 5;
    uint64_t ttl_ns = std::chrono::hours(72).count() * 1'000'000'000ULL;
};

struct SlotRing {
    std::vector<BootstrapSlot> slots;
    BootstrapSlotConfig config;

    // ── Mutators ──
    Result<uint32_t> claim_slot(const std::string& role,
                                const std::string& node_public_key,
                                const std::string& join_token_id,
                                uint64_t now_ns);

    Result<void> fulfill_slot(uint32_t index, const std::string& signed_csr, uint64_t now_ns);

    Result<void> expire_slot(uint32_t index);
    Result<void> revoke_slot(uint32_t index);

    uint32_t available_count(uint64_t now_ns) const;
    uint32_t claimed_count() const;
    uint32_t fulfilled_count() const;
};

} // namespace smo::genesis
