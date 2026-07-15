#pragma once

#include "../errors/error.hpp"
#include "../storage/database.hpp"
#include "../storage/migration.hpp"
#include "../storage/store_id.hpp"
#include "../types.hpp"
#include "discovery.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace smo {

enum class PeerEventType : uint8_t {
    Added   = 1,
    Removed = 2,
    Updated = 3,
    Renamed = 4,
    CapChanged = 5,
};

struct PeerEvent {
    int64_t        id = 0;
    NodeID         node_id;
    PeerEventType  type = PeerEventType::Added;
    Bytes          payload;
    int64_t        created_at = 0;
};

class PeerStore {
public:
    PeerStore() = default;

    PeerStore(PeerStore&&) = default;
    PeerStore& operator=(PeerStore&&) = default;

    PeerStore(const PeerStore&) = delete;
    PeerStore& operator=(const PeerStore&) = delete;

    Result<void> open(std::string_view base_path);
    void close();

    bool is_open() const { return db_.is_open(); }

    // ── CRUD ────────────────────────────────────────────────────────────
    Result<void> upsert(const PeerRecord& rec);
    Result<PeerRecord> lookup(const NodeID& id) const;
    Result<PeerRecord> lookup_by_name(std::string_view name) const;
    Result<std::vector<PeerRecord>> peers() const;
    Result<void> remove(const NodeID& id);

    // ── Filtered queries (for Selection Engine) ─────────────────────────
    Result<std::vector<PeerRecord>> peers_by_role(Role role) const;
    Result<std::vector<PeerRecord>> peers_by_tag(std::string_view tag) const;
    Result<std::vector<PeerRecord>> peers_by_os(std::string_view os) const;
    Result<std::vector<PeerRecord>> peers_by_arch(std::string_view arch) const;
    Result<std::vector<PeerRecord>> peers_by_mesh(std::string_view mesh) const;
    Result<std::vector<PeerRecord>> peers_by_state(PeerState state) const;

    // ── Sync helpers ────────────────────────────────────────────────────
    Result<void> sync_from_membership(const MembershipTable& table);
    Result<void> sync_to_membership(MembershipTable& table) const;

    // ── Events ──────────────────────────────────────────────────────────
    Result<void> record_event(PeerEventType type, const NodeID& id,
                               Bytes payload = {});
    Result<std::vector<PeerEvent>> recent_events(int64_t since_id = 0) const;

    // ── Stats ───────────────────────────────────────────────────────────
    Result<size_t> count() const;
    size_t capacity() const noexcept { return capacity_; }
    void set_capacity(size_t cap) noexcept { capacity_ = cap; }

private:
    static std::string tags_to_json(const std::vector<std::string>& tags);
    static std::vector<std::string> json_to_tags(std::string_view json);
    static std::string aliases_to_json(const std::vector<std::string>& aliases);
    static std::vector<std::string> json_to_aliases(std::string_view json);
    PeerRecord row_to_peer_record(const Statement& stmt) const;
    void bind_peer_record(Statement& stmt, const PeerRecord& rec, int& idx);

    DatabaseHandle db_;
    size_t capacity_ = 1000;
};

} // namespace smo
