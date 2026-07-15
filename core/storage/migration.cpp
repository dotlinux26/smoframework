#include "migration.hpp"

#include <string>

namespace smo {

Result<int> MigrationRunner::current_version() {
    auto stmt = db_.prepare("PRAGMA user_version;");
    if (!stmt) return stmt.error();

    auto rc = stmt.value().step();
    if (!rc) return rc.error();
    if (rc.value() != SQLITE_ROW) {
        return SMO_ERR_STORAGE(905, Error, RetrySafe, RetryOperation,
                               "PRAGMA user_version returned no row");
    }

    return static_cast<int>(stmt.value().column_int64(0));
}

Result<void> MigrationRunner::ensure_schema(const StoreInfo& info,
                                             const char* create_sql) {
    auto ver = current_version();
    if (!ver) return ver.error();

    if (ver.value() == 0) {
        auto r = db_.exec(create_sql);
        if (!r) return r;

        // Set schema version
        std::string pragma = "PRAGMA user_version = ";
        pragma += std::to_string(info.schema_version);
        pragma += ";";
        return db_.exec(pragma);
    }

    if (ver.value() > info.schema_version) {
        return SMO_ERR_STORAGE(900, Critical, NoRetry, RebootNode,
                               "database schema is newer than this build");
    }

    if (ver.value() < info.schema_version) {
        return SMO_ERR_STORAGE(901, Critical, NoRetry, RebootNode,
                               "database requires migration");
    }

    return {};
}

} // namespace smo
