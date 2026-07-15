#pragma once

#include "../errors/error.hpp"
#include "database.hpp"
#include "store_id.hpp"

namespace smo {

// ---------------------------------------------------------------------------
// MigrationRunner — manages schema versioning and creation
//
// Uses PRAGMA user_version to track schema version per database.
// On open, checks current version and runs creation SQL if at version 0.
// ---------------------------------------------------------------------------
class MigrationRunner {
public:
    explicit MigrationRunner(DatabaseHandle& db) : db_(db) {}

    // Read current PRAGMA user_version
    Result<int> current_version();

    // Create the schema for a store if the database is at version 0.
    // If already at the target version, this is a no-op.
    // `create_sql` should be the full CREATE TABLE DDL for the store.
    Result<void> ensure_schema(const StoreInfo& info, const char* create_sql);

private:
    DatabaseHandle& db_;
};

} // namespace smo
