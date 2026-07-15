#include "sqlite_store.hpp"

#include <sqlite3.h>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace smo {

static const char kKvSchema[] =
    "CREATE TABLE IF NOT EXISTS kv ("
    "  key   BLOB PRIMARY KEY,"
    "  value BLOB NOT NULL"
    ") WITHOUT ROWID;";

SqliteStore::SqliteStore(StoreID id, std::string base_path)
    : id_(id), base_path_(std::move(base_path))
{}

Result<void> SqliteStore::open() {
    namespace fs = std::filesystem;

    const auto& info = store_info(id_);
    fs::path dir(base_path_);
    fs::path file = dir / info.filename;

    // Ensure directory exists
    std::error_code ec;
    fs::create_directories(dir, ec);

    auto r = db_.open(file.string());
    if (!r) return r;

    // Ensure the kv table schema
    MigrationRunner mig(db_);
    r = mig.ensure_schema(info, kKvSchema);
    if (!r) {
        db_.close();
        return r;
    }

    return {};
}

// ── KV operations ───────────────────────────────────────────────────────

Result<void> SqliteStore::put(BytesView key, BytesView value) {
    auto stmt = db_.prepare(
        "INSERT OR REPLACE INTO kv (key, value) VALUES (?1, ?2);");
    if (!stmt) return stmt.error();

    auto r = stmt.value().bind_blob(1, key);
    if (!r) return r;
    r = stmt.value().bind_blob(2, value);
    if (!r) return r;

    auto rc = stmt.value().step();
    if (!rc) return rc.error();
    if (rc.value() != SQLITE_DONE) {
        return SMO_ERR_STORAGE(904, Error, RetrySafe, RetryOperation,
                               "unexpected SQLite result");
    }
    return {};
}

Result<Bytes> SqliteStore::get(BytesView key) {
    auto stmt = db_.prepare(
        "SELECT value FROM kv WHERE key = ?1;");
    if (!stmt) return stmt.error();

    auto r = stmt.value().bind_blob(1, key);
    if (!r) return r.error();

    auto rc = stmt.value().step();
    if (!rc) return rc.error();

    if (rc.value() == SQLITE_ROW) {
        return stmt.value().column_blob(0);
    }

    return SMO_ERR_STORAGE(902, Info, RetrySafe, None,
                           "key not found in store");
}

Result<void> SqliteStore::del(BytesView key) {
    auto stmt = db_.prepare(
        "DELETE FROM kv WHERE key = ?1;");
    if (!stmt) return stmt.error();

    auto r = stmt.value().bind_blob(1, key);
    if (!r) return r;

    auto rc = stmt.value().step();
    if (!rc) return rc.error();
    return {};
}

Result<std::vector<Bytes>> SqliteStore::list(BytesView prefix) {
    auto stmt = db_.prepare(
        "SELECT key FROM kv WHERE key >= ?1;");
    if (!stmt) return stmt.error();

    auto r = stmt.value().bind_blob(1, prefix);
    if (!r) return r.error();

    std::vector<Bytes> keys;
    while (true) {
        auto rc = stmt.value().step();
        if (!rc) return rc.error();
        if (rc.value() == SQLITE_DONE) break;

        Bytes k = stmt.value().column_blob(0);

        // Filter by prefix (since SQLite prefix matching on BLOBs is tricky)
        if (k.size() >= prefix.size() &&
            std::memcmp(k.data(), prefix.data(), prefix.size()) == 0)
        {
            keys.push_back(std::move(k));
        }
    }

    return keys;
}

// ── Transactions ────────────────────────────────────────────────────────

Result<void> SqliteStore::begin_transaction() {
    return db_.exec("BEGIN IMMEDIATE;");
}

Result<void> SqliteStore::commit() {
    return db_.exec("COMMIT;");
}

Result<void> SqliteStore::rollback() {
    return db_.exec("ROLLBACK;");
}

// ── Backup / Restore ────────────────────────────────────────────────────

Result<void> SqliteStore::backup(const std::string& dest_path) {
    if (!db_.is_open()) {
        return SMO_ERR_STORAGE(912, Error, RetrySafe, RetryOperation,
                               "database not open");
    }

    sqlite3* dest_db = nullptr;
    int rc = sqlite3_open(dest_path.c_str(), &dest_db);
    if (rc != SQLITE_OK) {
        if (dest_db) sqlite3_close(dest_db);
        return SMO_ERR_STORAGE(912, Error, RetrySafe, RetryOperation,
                               "cannot open backup destination");
    }

    sqlite3_backup* backup = sqlite3_backup_init(
        dest_db, "main", db_.handle(), "main");

    if (!backup) {
        sqlite3_close(dest_db);
        return SMO_ERR_STORAGE(912, Error, RetrySafe, RetryOperation,
                               "backup init failed");
    }

    sqlite3_backup_step(backup, -1);  // Copy all pages
    sqlite3_backup_finish(backup);
    sqlite3_close(dest_db);

    return {};
}

Result<void> SqliteStore::restore(const std::string& src_path) {
    // Close current database
    db_.close();

    namespace fs = std::filesystem;
    const auto& info = store_info(id_);
    fs::path dest = fs::path(base_path_) / info.filename;

    // Copy backup file to current location
    std::error_code ec;
    fs::copy_file(src_path, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        return SMO_ERR_STORAGE(913, Critical, NoRetry, ManualIntervention,
                               ec.message().c_str());
    }

    // Reopen
    return open();
}

} // namespace smo
