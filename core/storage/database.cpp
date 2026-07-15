#include "database.hpp"

#include <cstring>

namespace smo {

// ---------------------------------------------------------------------------
// DatabaseHandle
// ---------------------------------------------------------------------------

Result<void> DatabaseHandle::open(const std::string& path) {
    if (db_) {
        return SMO_ERR_STORAGE(900, Critical, NoRetry, RebootNode,
                               "database already open");
    }

    sqlite3* db = nullptr;
    int rc = sqlite3_open(path.c_str(), &db);
    if (rc != SQLITE_OK) {
        std::string msg = sqlite3_errmsg(db);
        if (db) sqlite3_close(db);
        return SMO_ERR_STORAGE(900, Critical, NoRetry, RebootNode,
                               msg.c_str());
    }

    // WAL mode
    char* err = nullptr;
    rc = sqlite3_exec(db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        sqlite3_close(db);
        return SMO_ERR_STORAGE(900, Critical, NoRetry, RebootNode, msg.c_str());
    }

    // Busy timeout (5 seconds)
    sqlite3_busy_timeout(db, 5000);

    // Synchronous NORMAL (safe in WAL mode)
    rc = sqlite3_exec(db, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        sqlite3_close(db);
        return SMO_ERR_STORAGE(900, Critical, NoRetry, RebootNode, msg.c_str());
    }

    db_ = db;
    path_ = path;
    return {};
}

void DatabaseHandle::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
        path_.clear();
    }
}

Result<void> DatabaseHandle::exec(const char* sql) {
    if (!db_) {
        return SMO_ERR_STORAGE(905, Error, RetrySafe, RebootNode,
                               "database not open");
    }
    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        return SMO_ERR_STORAGE(904, Error, RetrySafe, RetryOperation, msg.c_str());
    }
    return {};
}

Result<Statement> DatabaseHandle::prepare(const char* sql) {
    if (!db_) {
        return SMO_ERR_STORAGE(905, Error, RetrySafe, RebootNode,
                               "database not open");
    }
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return SMO_ERR_STORAGE(905, Error, RetrySafe, RetryOperation,
                               sqlite3_errmsg(db_));
    }
    return Statement(db_, stmt);
}

// ---------------------------------------------------------------------------
// Statement
// ---------------------------------------------------------------------------

Result<int> Statement::step() {
    if (!stmt_) {
        return SMO_ERR_STORAGE(905, Error, RetrySafe, RetryOperation,
                               "statement not prepared");
    }
    int rc = sqlite3_step(stmt_);
    return rc;
}

Result<void> Statement::bind_blob(int index, const void* data, int size) {
    int rc = sqlite3_bind_blob(stmt_, index, data, size, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        return SMO_ERR_STORAGE(904, Error, RetrySafe, RetryOperation,
                               sqlite3_errmsg(db_));
    }
    return {};
}

Result<void> Statement::bind_int64(int index, int64_t val) {
    int rc = sqlite3_bind_int64(stmt_, index, val);
    if (rc != SQLITE_OK) {
        return SMO_ERR_STORAGE(904, Error, RetrySafe, RetryOperation,
                               sqlite3_errmsg(db_));
    }
    return {};
}

Result<void> Statement::bind_text(int index, const std::string& val) {
    int rc = sqlite3_bind_text(stmt_, index, val.c_str(),
                               static_cast<int>(val.size()),
                               SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        return SMO_ERR_STORAGE(904, Error, RetrySafe, RetryOperation,
                               sqlite3_errmsg(db_));
    }
    return {};
}

Bytes Statement::column_blob(int index) const {
    const void* data = sqlite3_column_blob(stmt_, index);
    int size = sqlite3_column_bytes(stmt_, index);
    Bytes out(static_cast<size_t>(size));
    if (size > 0) std::memcpy(out.data(), data, static_cast<size_t>(size));
    return out;
}

int64_t Statement::column_int64(int index) const {
    return sqlite3_column_int64(stmt_, index);
}

std::string Statement::column_text(int index) const {
    const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, index));
    int size = sqlite3_column_bytes(stmt_, index);
    return std::string(text, static_cast<size_t>(size));
}

} // namespace smo
