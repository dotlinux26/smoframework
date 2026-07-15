#include "peer_store.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <sstream>

namespace smo {

static const char kPeerSchema[] =
    "CREATE TABLE IF NOT EXISTS peers ("
    "  node_id         BLOB PRIMARY KEY,"
    "  display_name    TEXT NOT NULL DEFAULT '',"
    "  hostname        TEXT NOT NULL DEFAULT '',"
    "  mesh_name       TEXT NOT NULL DEFAULT '',"
    "  role            INTEGER NOT NULL DEFAULT 3,"
    "  tags            TEXT NOT NULL DEFAULT '[]',"
    "  platform        TEXT NOT NULL DEFAULT '',"
    "  arch            TEXT NOT NULL DEFAULT '',"
    "  version         TEXT NOT NULL DEFAULT '',"
    "  location        TEXT NOT NULL DEFAULT '',"
    "  aliases         TEXT NOT NULL DEFAULT '[]',"
    "  endpoint_scheme TEXT NOT NULL DEFAULT 'tcp',"
    "  endpoint_host   TEXT NOT NULL DEFAULT '',"
    "  endpoint_port   INTEGER NOT NULL DEFAULT 0,"
    "  state           INTEGER NOT NULL DEFAULT 0,"
    "  last_seen       INTEGER NOT NULL DEFAULT 0,"
    "  ping_misses     INTEGER NOT NULL DEFAULT 0,"
    "  rtt_ms          REAL NOT NULL DEFAULT 0.0,"
    "  created_at      INTEGER NOT NULL DEFAULT 0,"
    "  updated_at      INTEGER NOT NULL DEFAULT 0"
    ");";

static const char kEventSchema[] =
    "CREATE TABLE IF NOT EXISTS peer_events ("
    "  id              INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  node_id         BLOB NOT NULL,"
    "  event_type      INTEGER NOT NULL,"
    "  payload         BLOB,"
    "  created_at      INTEGER NOT NULL"
    ");";

static const char kPeerIndexes[] =
    "CREATE INDEX IF NOT EXISTS idx_peers_display_name ON peers(display_name);"
    "CREATE INDEX IF NOT EXISTS idx_peers_state ON peers(state);"
    "CREATE INDEX IF NOT EXISTS idx_peers_role ON peers(role);"
    "CREATE INDEX IF NOT EXISTS idx_peers_mesh ON peers(mesh_name);";

// ===========================================================================
// Helpers
// ===========================================================================

std::string PeerStore::tags_to_json(const std::vector<std::string>& tags) {
    if (tags.empty()) return "[]";
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < tags.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << tags[i] << "\"";
    }
    oss << "]";
    return oss.str();
}

std::vector<std::string> PeerStore::json_to_tags(std::string_view json) {
    std::vector<std::string> tags;
    if (json.empty() || json == "[]") return tags;
    size_t pos = 1; // skip '['
    while (pos < json.size() && json[pos] != ']') {
        if (json[pos] == '"') {
            size_t start = pos + 1;
            size_t end = json.find('"', start);
            if (end != std::string_view::npos) {
                tags.emplace_back(json.substr(start, end - start));
                pos = end + 1;
            } else break;
        }
        if (pos < json.size() && json[pos] == ',') pos++;
    }
    return tags;
}

std::string PeerStore::aliases_to_json(const std::vector<std::string>& aliases) {
    return tags_to_json(aliases); // same format
}

std::vector<std::string> PeerStore::json_to_aliases(std::string_view json) {
    return json_to_tags(json); // same format
}

// ===========================================================================
// Mapping: row → PeerRecord
// ===========================================================================

PeerRecord PeerStore::row_to_peer_record(const Statement& stmt) const {
    PeerRecord rec;
    Bytes nid = stmt.column_blob(0);
    if (nid.size() == 32)
        std::memcpy(rec.node_id.value.data(), nid.data(), 32);
    rec.display_name = stmt.column_text(1);
    rec.hostname     = stmt.column_text(2);
    rec.mesh_name    = stmt.column_text(3);
    rec.role         = static_cast<Role>(stmt.column_int64(4));
    rec.tags         = json_to_tags(stmt.column_text(5));
    rec.platform     = stmt.column_text(6);
    rec.arch         = stmt.column_text(7);
    rec.version      = stmt.column_text(8);
    rec.location     = stmt.column_text(9);
    rec.aliases      = json_to_aliases(stmt.column_text(10));
    rec.endpoint.scheme = stmt.column_text(11);
    rec.endpoint.host   = stmt.column_text(12);
    rec.endpoint.port   = static_cast<uint16_t>(stmt.column_int64(13));
    rec.state        = static_cast<PeerState>(stmt.column_int64(14));
    rec.last_seen    = stmt.column_int64(15);
    rec.ping_misses  = static_cast<int>(stmt.column_int64(16));
    rec.rtt_ms       = stmt.column_text(17).empty()
                           ? 0.0 : std::stod(stmt.column_text(17));
    return rec;
}

void PeerStore::bind_peer_record(Statement& stmt, const PeerRecord& rec,
                                  int& idx) {
    stmt.bind_blob(idx++, rec.node_id.value);
    stmt.bind_text(idx++, rec.display_name).ignore();
    stmt.bind_text(idx++, rec.hostname).ignore();
    stmt.bind_text(idx++, rec.mesh_name).ignore();
    stmt.bind_int64(idx++, static_cast<int64_t>(rec.role));
    stmt.bind_text(idx++, tags_to_json(rec.tags)).ignore();
    stmt.bind_text(idx++, rec.platform).ignore();
    stmt.bind_text(idx++, rec.arch).ignore();
    stmt.bind_text(idx++, rec.version).ignore();
    stmt.bind_text(idx++, rec.location).ignore();
    stmt.bind_text(idx++, aliases_to_json(rec.aliases)).ignore();
    stmt.bind_text(idx++, rec.endpoint.scheme).ignore();
    stmt.bind_text(idx++, rec.endpoint.host).ignore();
    stmt.bind_int64(idx++, rec.endpoint.port);
    stmt.bind_int64(idx++, static_cast<int64_t>(rec.state));
    stmt.bind_int64(idx++, rec.last_seen);
    stmt.bind_int64(idx++, rec.ping_misses);
    std::string rtt_str = std::to_string(rec.rtt_ms);
    stmt.bind_text(idx++, rtt_str).ignore();
    stmt.bind_int64(idx++, rec.last_seen); // created_at = last seen for new
    stmt.bind_int64(idx++, rec.last_seen); // updated_at
}

// ===========================================================================
// Open / Close
// ===========================================================================

Result<void> PeerStore::open(std::string_view base_path) {
    namespace fs = std::filesystem;
    fs::path dir(base_path);
    fs::path file = dir / "peer.db";

    std::error_code ec;
    fs::create_directories(dir, ec);

    auto r = db_.open(file.string());
    if (!r) return r;

    MigrationRunner mig(db_);
    r = mig.ensure_schema(store_info(StoreID::Peer), kPeerSchema);
    if (!r) { db_.close(); return r; }

    r = db_.exec(kEventSchema);
    if (!r) { db_.close(); return r; }

    r = db_.exec("BEGIN IMMEDIATE;");
    if (!r) { db_.close(); return r; }
    r = db_.exec(kPeerIndexes);
    if (!r) { db_.rollback(); db_.close(); return r; }
    r = db_.exec("COMMIT;");
    if (!r) { db_.close(); return r; }

    return {};
}

void PeerStore::close() {
    db_.close();
}

// ===========================================================================
// CRUD
// ===========================================================================

Result<void> PeerStore::upsert(const PeerRecord& rec) {
    auto stmt = db_.prepare(
        "INSERT OR REPLACE INTO peers "
        "(node_id, display_name, hostname, mesh_name, role, tags,"
        " platform, arch, version, location, aliases,"
        " endpoint_scheme, endpoint_host, endpoint_port,"
        " state, last_seen, ping_misses, rtt_ms, created_at, updated_at"
        ") VALUES ("
        " ?1,  ?2,  ?3,  ?4,  ?5,  ?6,"
        " ?7,  ?8,  ?9, ?10, ?11,"
        " ?12, ?13, ?14,"
        " ?15, ?16, ?17, ?18,"
        " COALESCE((SELECT created_at FROM peers WHERE node_id = ?1), ?19),"
        " ?20"
        ");");
    if (!stmt) return stmt.error();

    int idx = 1;
    bind_peer_record(stmt.value(), rec, idx);

    auto rc = stmt.value().step();
    if (!rc) return rc.error();

    return {};
}

Result<PeerRecord> PeerStore::lookup(const NodeID& id) const {
    auto stmt = db_.prepare(
        "SELECT * FROM peers WHERE node_id = ?1;");
    if (!stmt) return stmt.error();

    auto r = stmt.value().bind_blob(1, id.value);
    if (!r) return r.error();

    auto rc = stmt.value().step();
    if (!rc) return rc.error();
    if (rc.value() != SQLITE_ROW) {
        return SMO_ERR_DISCOVERY(400, Info, RetrySafe, None,
                                "peer not found in store");
    }
    return row_to_peer_record(stmt.value());
}

Result<PeerRecord> PeerStore::lookup_by_name(std::string_view name) const {
    auto stmt = db_.prepare(
        "SELECT * FROM peers WHERE display_name = ?1 LIMIT 1;");
    if (!stmt) return stmt.error();

    auto r = stmt.value().bind_text(1, std::string(name));
    if (!r) return r.error();

    auto rc = stmt.value().step();
    if (!rc) return rc.error();
    if (rc.value() != SQLITE_ROW) {
        return SMO_ERR_DISCOVERY(400, Info, RetrySafe, None,
                                "no peer with that name");
    }
    return row_to_peer_record(stmt.value());
}

Result<std::vector<PeerRecord>> PeerStore::peers() const {
    auto stmt = db_.prepare("SELECT * FROM peers ORDER BY display_name;");
    if (!stmt) return stmt.error();

    std::vector<PeerRecord> results;
    while (true) {
        auto rc = stmt.value().step();
        if (!rc) return rc.error();
        if (rc.value() == SQLITE_DONE) break;
        results.push_back(row_to_peer_record(stmt.value()));
    }
    return results;
}

Result<void> PeerStore::remove(const NodeID& id) {
    auto stmt = db_.prepare("DELETE FROM peers WHERE node_id = ?1;");
    if (!stmt) return stmt.error();

    auto r = stmt.value().bind_blob(1, id.value);
    if (!r) return r.error();

    auto rc = stmt.value().step();
    if (!rc) return rc.error();
    return {};
}

// ===========================================================================
// Filtered queries
// ===========================================================================

Result<std::vector<PeerRecord>> PeerStore::peers_by_role(Role role) const {
    auto stmt = db_.prepare("SELECT * FROM peers WHERE role = ?1 ORDER BY display_name;");
    if (!stmt) return stmt.error();
    stmt.value().bind_int64(1, static_cast<int64_t>(role)).ignore();
    std::vector<PeerRecord> results;
    while (true) {
        auto rc = stmt.value().step();
        if (!rc) return rc.error();
        if (rc.value() == SQLITE_DONE) break;
        results.push_back(row_to_peer_record(stmt.value()));
    }
    return results;
}

Result<std::vector<PeerRecord>> PeerStore::peers_by_tag(std::string_view tag) const {
    auto like = "%\"" + std::string(tag) + "\"%";
    auto stmt = db_.prepare(
        "SELECT * FROM peers WHERE tags LIKE ?1 ORDER BY display_name;");
    if (!stmt) return stmt.error();
    stmt.value().bind_text(1, like).ignore();
    std::vector<PeerRecord> results;
    while (true) {
        auto rc = stmt.value().step();
        if (!rc) return rc.error();
        if (rc.value() == SQLITE_DONE) break;
        results.push_back(row_to_peer_record(stmt.value()));
    }
    return results;
}

Result<std::vector<PeerRecord>> PeerStore::peers_by_os(std::string_view os) const {
    auto stmt = db_.prepare(
        "SELECT * FROM peers WHERE platform = ?1 ORDER BY display_name;");
    if (!stmt) return stmt.error();
    stmt.value().bind_text(1, std::string(os)).ignore();
    std::vector<PeerRecord> results;
    while (true) {
        auto rc = stmt.value().step();
        if (!rc) return rc.error();
        if (rc.value() == SQLITE_DONE) break;
        results.push_back(row_to_peer_record(stmt.value()));
    }
    return results;
}

Result<std::vector<PeerRecord>> PeerStore::peers_by_arch(std::string_view arch) const {
    auto stmt = db_.prepare(
        "SELECT * FROM peers WHERE arch = ?1 ORDER BY display_name;");
    if (!stmt) return stmt.error();
    stmt.value().bind_text(1, std::string(arch)).ignore();
    std::vector<PeerRecord> results;
    while (true) {
        auto rc = stmt.value().step();
        if (!rc) return rc.error();
        if (rc.value() == SQLITE_DONE) break;
        results.push_back(row_to_peer_record(stmt.value()));
    }
    return results;
}

Result<std::vector<PeerRecord>> PeerStore::peers_by_mesh(std::string_view mesh) const {
    auto stmt = db_.prepare(
        "SELECT * FROM peers WHERE mesh_name = ?1 ORDER BY display_name;");
    if (!stmt) return stmt.error();
    stmt.value().bind_text(1, std::string(mesh)).ignore();
    std::vector<PeerRecord> results;
    while (true) {
        auto rc = stmt.value().step();
        if (!rc) return rc.error();
        if (rc.value() == SQLITE_DONE) break;
        results.push_back(row_to_peer_record(stmt.value()));
    }
    return results;
}

Result<std::vector<PeerRecord>> PeerStore::peers_by_state(PeerState state) const {
    auto stmt = db_.prepare(
        "SELECT * FROM peers WHERE state = ?1 ORDER BY display_name;");
    if (!stmt) return stmt.error();
    stmt.value().bind_int64(1, static_cast<int64_t>(state)).ignore();
    std::vector<PeerRecord> results;
    while (true) {
        auto rc = stmt.value().step();
        if (!rc) return rc.error();
        if (rc.value() == SQLITE_DONE) break;
        results.push_back(row_to_peer_record(stmt.value()));
    }
    return results;
}

// ===========================================================================
// Sync helpers
// ===========================================================================

Result<void> PeerStore::sync_from_membership(const MembershipTable& table) {
    auto peers = table.peers();
    for (auto& rec : peers) {
        auto r = upsert(rec);
        if (!r) return r;
    }
    return {};
}

Result<void> PeerStore::sync_to_membership(MembershipTable& table) const {
    auto r = peers();
    if (!r) return r.error();
    for (auto& rec : r.value()) {
        auto u = table.upsert(std::move(rec));
        if (!u) return u;
    }
    return {};
}

// ===========================================================================
// Events
// ===========================================================================

Result<void> PeerStore::record_event(PeerEventType type, const NodeID& id,
                                      Bytes payload) {
    auto stmt = db_.prepare(
        "INSERT INTO peer_events (node_id, event_type, payload, created_at)"
        " VALUES (?1, ?2, ?3, ?4);");
    if (!stmt) return stmt.error();

    auto now = std::chrono::system_clock::to_time_t(
                   std::chrono::system_clock::now());

    stmt.value().bind_blob(1, id.value).ignore();
    stmt.value().bind_int64(2, static_cast<int64_t>(type)).ignore();
    if (!payload.empty())
        stmt.value().bind_blob(3, payload).ignore();
    else
        stmt.value().bind_blob(3, nullptr, 0).ignore();
    stmt.value().bind_int64(4, now).ignore();

    return {};
}

Result<std::vector<PeerEvent>> PeerStore::recent_events(int64_t since_id) const {
    auto stmt = db_.prepare(
        "SELECT id, node_id, event_type, payload, created_at"
        " FROM peer_events WHERE id > ?1"
        " ORDER BY id ASC LIMIT 100;");
    if (!stmt) return stmt.error();

    stmt.value().bind_int64(1, since_id).ignore();

    std::vector<PeerEvent> events;
    while (true) {
        auto rc = stmt.value().step();
        if (!rc) return rc.error();
        if (rc.value() == SQLITE_DONE) break;

        PeerEvent ev;
        ev.id = stmt.value().column_int64(0);
        Bytes nid = stmt.value().column_blob(1);
        if (nid.size() == 32)
            std::memcpy(ev.node_id.value.data(), nid.data(), 32);
        ev.type = static_cast<PeerEventType>(stmt.value().column_int64(2));
        ev.payload = stmt.value().column_blob(3);
        ev.created_at = stmt.value().column_int64(4);
        events.push_back(std::move(ev));
    }
    return events;
}

// ===========================================================================
// Stats
// ===========================================================================

Result<size_t> PeerStore::count() const {
    auto stmt = db_.prepare("SELECT COUNT(*) FROM peers;");
    if (!stmt) return stmt.error();

    auto rc = stmt.value().step();
    if (!rc) return rc.error();
    if (rc.value() == SQLITE_ROW) {
        return static_cast<size_t>(stmt.value().column_int64(0));
    }
    return size_t{0};
}

} // namespace smo
