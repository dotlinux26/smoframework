#include "bootstrap_slot.hpp"

#include <algorithm>
#include <sstream>
#include <iterator>

namespace smo::genesis {

// ── BootstrapSlot serialization (simple JSON-like) ──────────────────
Result<Bytes> BootstrapSlot::serialize() const {
    std::ostringstream oss;
    auto esc = [](const std::string& s) -> std::string {
        std::string out;
        out += '"';
        for (char c : s) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        out += '"';
        return out;
    };

    oss << "{\n";
    oss << "  \"index\": " << index << ",\n";
    oss << "  \"status\": " << (int)status << ",\n";
    oss << "  \"role\": " << esc(role) << ",\n";
    oss << "  \"node_public_key\": " << esc(node_public_key) << ",\n";
    oss << "  \"join_token_id\": " << esc(join_token_id) << ",\n";
    oss << "  \"claimed_at\": " << claimed_at << ",\n";
    oss << "  \"expires_at\": " << expires_at << ",\n";
    oss << "  \"signed_csr\": " << esc(signed_csr) << ",\n";
    oss << "  \"fulfilled_at\": " << fulfilled_at << "\n";
    oss << "}\n";

    std::string str = oss.str();
    return Bytes(str.begin(), str.end());
}

Result<BootstrapSlot> BootstrapSlot::deserialize(BytesView data) {
    std::string json(reinterpret_cast<const char*>(data.data()), data.size());

    auto find_int = [&](const std::string& key) -> uint64_t {
        auto pos = json.find(key);
        if (pos == std::string::npos) return 0;
        pos = json.find(':', pos);
        if (pos == std::string::npos) return 0;
        pos = json.find_first_of("0123456789", pos);
        if (pos == std::string::npos) return 0;
        char* end = nullptr;
        return strtoull(json.c_str() + pos, &end, 10);
    };

    auto find_str = [&](const std::string& key) -> std::string {
        auto pos = json.find(key);
        if (pos == std::string::npos) return {};
        pos = json.find(':', pos);
        if (pos == std::string::npos) return {};
        pos = json.find_first_of('"', pos);
        if (pos == std::string::npos) return {};
        ++pos;
        auto end = json.find('"', pos);
        if (end == std::string::npos) return {};
        return json.substr(pos, end - pos);
    };

    BootstrapSlot slot;
    slot.index          = (uint32_t)find_int("index");
    slot.status         = (SlotStatus)find_int("status");
    slot.role           = find_str("role");
    slot.node_public_key = find_str("node_public_key");
    slot.join_token_id  = find_str("join_token_id");
    slot.claimed_at     = find_int("claimed_at");
    slot.expires_at     = find_int("expires_at");
    slot.signed_csr     = find_str("signed_csr");
    slot.fulfilled_at   = find_int("fulfilled_at");
    return slot;
}

// ── SlotRing mutators ────────────────────────────────────────────────
Result<uint32_t> SlotRing::claim_slot(const std::string& role,
                                      const std::string& node_public_key,
                                      const std::string& join_token_id,
                                      uint64_t now_ns) {
    for (size_t i = 0; i < slots.size(); ++i) {
        auto& s = slots[i];
        if (s.status == SlotStatus::Vacant ||
            (s.status == SlotStatus::Claimed && s.is_expired(now_ns))) {
            s.index            = (uint32_t)i;
            s.status           = SlotStatus::Claimed;
            s.role             = role;
            s.node_public_key  = node_public_key;
            s.join_token_id    = join_token_id;
            s.claimed_at       = now_ns;
            s.expires_at       = now_ns + config.ttl_ns;
            s.signed_csr       = {};
            s.fulfilled_at     = 0;
            return (uint32_t)i;
        }
    }
    return SMO_ERR_GENESIS(1402, Error, NoRetry, ManualIntervention,
                           "all bootstrap slots exhausted");
}

Result<void> SlotRing::fulfill_slot(uint32_t index, const std::string& csr, uint64_t now_ns) {
    if (index >= slots.size()) {
        return SMO_ERR_GENESIS(1401, Error, NoRetry, ManualIntervention,
                               "slot index out of range");
    }
    auto& s = slots[index];
    if (s.status != SlotStatus::Claimed) {
        return SMO_ERR_GENESIS(1401, Error, NoRetry, ManualIntervention,
                               "slot not in claimed state");
    }
    s.status       = SlotStatus::Fulfilled;
    s.signed_csr   = csr;
    s.fulfilled_at = now_ns;
    return {};
}

Result<void> SlotRing::expire_slot(uint32_t index) {
    if (index >= slots.size()) {
        return SMO_ERR_GENESIS(1401, Error, NoRetry, ManualIntervention,
                               "slot index out of range");
    }
    auto& s = slots[index];
    if (s.status != SlotStatus::Claimed) {
        return SMO_ERR_GENESIS(1401, Error, NoRetry, ManualIntervention,
                               "slot not in claimed state");
    }
    s.status = SlotStatus::Expired;
    return {};
}

Result<void> SlotRing::revoke_slot(uint32_t index) {
    if (index >= slots.size()) {
        return SMO_ERR_GENESIS(1401, Error, NoRetry, ManualIntervention,
                               "slot index out of range");
    }
    auto& s = slots[index];
    if (s.status != SlotStatus::Claimed && s.status != SlotStatus::Fulfilled) {
        return SMO_ERR_GENESIS(1401, Error, NoRetry, ManualIntervention,
                               "slot not in claimable state");
    }
    s.status = SlotStatus::Revoked;
    return {};
}

uint32_t SlotRing::available_count(uint64_t now_ns) const {
    uint32_t cnt = 0;
    for (const auto& s : slots) {
        if (s.status == SlotStatus::Vacant ||
            (s.status == SlotStatus::Claimed && s.is_expired(now_ns))) {
            ++cnt;
        }
    }
    return cnt;
}

uint32_t SlotRing::claimed_count() const {
    uint32_t cnt = 0;
    for (const auto& s : slots) {
        if (s.status == SlotStatus::Claimed) ++cnt;
    }
    return cnt;
}

uint32_t SlotRing::fulfilled_count() const {
    uint32_t cnt = 0;
    for (const auto& s : slots) {
        if (s.status == SlotStatus::Fulfilled) ++cnt;
    }
    return cnt;
}

} // namespace smo::genesis
