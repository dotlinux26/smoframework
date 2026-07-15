#pragma once

#include "../errors/error.hpp"
#include "../types.hpp"
#include "store_id.hpp"

#include <sqlite3.h>
#include <string>

namespace smo {

class Statement;

// ---------------------------------------------------------------------------
// DatabaseHandle — RAII wrapper around sqlite3*
//
// Owns a single SQLite3 database connection. Opens with WAL mode,
// configures busy timeout and synchronous mode per RFC 0022.
// ---------------------------------------------------------------------------
class DatabaseHandle {
public:
    DatabaseHandle() = default;
    ~DatabaseHandle() { close(); }

    DatabaseHandle(DatabaseHandle&& other) noexcept
        : db_(other.db_), path_(std::move(other.path_))
    { other.db_ = nullptr; }

    DatabaseHandle& operator=(DatabaseHandle&& other) noexcept {
        if (this != &other) { close(); db_ = other.db_; path_ = std::move(other.path_); other.db_ = nullptr; }
        return *this;
    }

    DatabaseHandle(const DatabaseHandle&) = delete;
    DatabaseHandle& operator=(const DatabaseHandle&) = delete;

    // Open (or create) the database at `path`. Applies WAL pragmas on first open.
    Result<void> open(const std::string& path);

    // Close the database connection.
    void close();

    // Execute raw SQL (no bindings). Returns error on failure.
    Result<void> exec(const char* sql);
    Result<void> exec(const std::string& sql) { return exec(sql.c_str()); }

    // Prepare a statement. Returns a RAII Statement wrapper.
    Result<Statement> prepare(const char* sql);

    sqlite3* handle() { return db_; }
    bool is_open() const { return db_ != nullptr; }
    const std::string& path() const { return path_; }

    // Last inserted rowid (after INSERT)
    int64_t last_insert_rowid() const { return sqlite3_last_insert_rowid(db_); }

    // Number of rows changed by last statement
    int changes() const { return sqlite3_changes(db_); }

private:
    sqlite3*    db_ = nullptr;
    std::string path_;
};

// ---------------------------------------------------------------------------
// Statement — RAII wrapper for sqlite3_stmt*
// ---------------------------------------------------------------------------
class Statement {
public:
    Statement() = default;
    Statement(sqlite3* db, sqlite3_stmt* stmt) : db_(db), stmt_(stmt) {}
    ~Statement() { finalize(); }

    Statement(Statement&& other) noexcept
        : db_(other.db_), stmt_(other.stmt_)
    { other.db_ = nullptr; other.stmt_ = nullptr; }

    Statement& operator=(Statement&& other) noexcept {
        if (this != &other) { finalize(); db_ = other.db_; stmt_ = other.stmt_; other.db_ = nullptr; other.stmt_ = nullptr; }
        return *this;
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void finalize() {
        if (stmt_) { sqlite3_finalize(stmt_); stmt_ = nullptr; db_ = nullptr; }
    }

    // Step the statement. Returns SQLITE_ROW, SQLITE_DONE, or error code.
    Result<int> step();

    // Bind helpers
    Result<void> bind_blob(int index, const void* data, int size);
    Result<void> bind_blob(int index, BytesView data) {
        return bind_blob(index, data.data(), static_cast<int>(data.size()));
    }
    Result<void> bind_int64(int index, int64_t val);
    Result<void> bind_text(int index, const std::string& val);

    // Column accessors (call after step returns SQLITE_ROW)
    Bytes        column_blob(int index) const;
    int64_t      column_int64(int index) const;
    std::string  column_text(int index) const;

    sqlite3_stmt* handle() { return stmt_; }
    explicit operator bool() const { return stmt_ != nullptr; }

private:
    sqlite3*      db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

} // namespace smo
