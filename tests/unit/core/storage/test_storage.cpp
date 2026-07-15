// SPDX-License-Identifier: Apache-2.0
//
// Storage — unit tests

#include <storage/database.hpp>
#include <storage/migration.hpp>
#include <storage/sqlite_store.hpp>
#include <storage/store_id.hpp>
#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace smo;

// ---------------------------------------------------------------------------
// Minimal test runner
// ---------------------------------------------------------------------------
static int failures = 0;

#define TEST(name)                                                      \
    do {                                                                \
        printf("  TEST %-50s ... ", name);                              \
        fflush(stdout);

#define END_TEST(result)                                                \
        if (result) {                                                   \
            printf("PASS\n");                                           \
        } else {                                                        \
            printf("FAIL\n");                                           \
            ++failures;                                                 \
        }                                                               \
    } while (false)

#define ASSERT(cond)                                                    \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("\n    ASSERTION FAILED at %s:%d: %s\n",             \
                   __FILE__, __LINE__, #cond);                          \
            return false;                                               \
        }                                                               \
    } while (false)

#define ASSERT_EQ(a, b)                                                 \
    do {                                                                \
        if ((a) != (b)) {                                               \
            printf("\n    ASSERTION FAILED at %s:%d: %s == %s\n"        \
                   "      LHS=%d  RHS=%d\n",                            \
                   __FILE__, __LINE__, #a, #b,                          \
                   static_cast<int>(a), static_cast<int>(b));           \
            return false;                                               \
        }                                                               \
    } while (false)

// Temporary directory helper
struct TempDir {
    std::string path;
    TempDir() {
        auto tmp = std::filesystem::temp_directory_path();
        path = (tmp / "smo_test_storage_XXXXXX").string();
        // mkdtemp wants a mutable C string
        char buf[256];
        std::strncpy(buf, path.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        ::mkdtemp(buf);
        path = buf;
    }
    ~TempDir() { std::filesystem::remove_all(path); }
    std::string file(const char* name) const { return path + "/" + name; }
};

// ==========================================================================
// Tests
// ==========================================================================

static bool test_store_id_constants() {
    ASSERT_EQ(static_cast<int>(StoreID::Node), 0);
    ASSERT_EQ(static_cast<int>(StoreID::Mesh), 1);
    ASSERT_EQ(static_cast<int>(StoreID::Session), 2);
    ASSERT_EQ(static_cast<int>(StoreID::Trust), 3);
    ASSERT_EQ(static_cast<int>(StoreID::Audit), 4);
    ASSERT_EQ(static_cast<int>(StoreID::DAG), 5);
    ASSERT_EQ(static_cast<int>(StoreID::Peer), 6);
    ASSERT_EQ(static_cast<int>(StoreID::Governance), 7);
    return true;
}

static bool test_store_info() {
    for (int i = 0; i < kStoreCount; ++i) {
        const auto& info = kStoreInfos[i];
        ASSERT(info.name != nullptr);
        ASSERT(info.filename != nullptr);
        ASSERT(info.schema_version == 1);
        ASSERT(static_cast<int>(info.id) == i);
    }
    const auto& node = store_info(StoreID::Node);
    ASSERT(std::strcmp(node.name, "node") == 0);
    ASSERT(std::strcmp(node.filename, "node.db") == 0);
    return true;
}

static bool test_database_open_close() {
    TempDir dir;
    DatabaseHandle db;

    auto r = db.open(dir.file("test.db"));
    ASSERT(r);
    ASSERT(db.is_open());
    ASSERT(db.path() == dir.file("test.db"));

    db.close();
    ASSERT(!db.is_open());
    return true;
}

static bool test_database_double_open() {
    TempDir dir;
    DatabaseHandle db;

    auto r = db.open(dir.file("double.db"));
    ASSERT(r);

    r = db.open(dir.file("double2.db"));
    ASSERT(!r);
    ASSERT_EQ(r.error().code.code, 900);
    return true;
}

static bool test_database_exec() {
    TempDir dir;
    DatabaseHandle db;
    ASSERT(db.open(dir.file("exec.db")));

    auto r = db.exec("CREATE TABLE t (x INTEGER PRIMARY KEY, y TEXT);");
    ASSERT(r);

    r = db.exec("INSERT INTO t VALUES (1, 'hello');");
    ASSERT(r);

    r = db.exec("SELECT * FROM t;");
    ASSERT(r);
    return true;
}

static bool test_database_prepare_and_step() {
    TempDir dir;
    DatabaseHandle db;
    ASSERT(db.open(dir.file("stmt.db")));
    ASSERT(db.exec("CREATE TABLE t (k INTEGER PRIMARY KEY, v TEXT);"));
    ASSERT(db.exec("INSERT INTO t VALUES (1, 'alpha');"));
    ASSERT(db.exec("INSERT INTO t VALUES (2, 'beta');"));

    auto stmt = db.prepare("SELECT v FROM t WHERE k = ?1;");
    ASSERT(stmt);

    auto r = stmt.value().bind_int64(1, 2);
    ASSERT(r);

    auto rc = stmt.value().step();
    ASSERT(rc);
    ASSERT_EQ(rc.value(), SQLITE_ROW);

    std::string val = stmt.value().column_text(0);
    ASSERT(val == "beta");

    auto rc2 = stmt.value().step();
    ASSERT(rc2);
    ASSERT_EQ(rc2.value(), SQLITE_DONE);
    return true;
}

static bool test_statement_blob_roundtrip() {
    TempDir dir;
    DatabaseHandle db;
    ASSERT(db.open(dir.file("blob.db")));
    ASSERT(db.exec("CREATE TABLE b (id INTEGER PRIMARY KEY, data BLOB);"));

    auto stmt = db.prepare("INSERT INTO b VALUES (1, ?1);");
    ASSERT(stmt);

    Bytes data = { 0xDE, 0xAD, 0xBE, 0xEF };
    ASSERT(stmt.value().bind_blob(1, data));

    auto rc = stmt.value().step();
    ASSERT(rc);
    ASSERT_EQ(rc.value(), SQLITE_DONE);

    // Read back
    auto sel = db.prepare("SELECT data FROM b WHERE id = 1;");
    ASSERT(sel);

    auto rc_sel = sel.value().step();
    ASSERT(rc_sel);
    ASSERT_EQ(rc_sel.value(), SQLITE_ROW);

    Bytes read_back = sel.value().column_blob(0);
    ASSERT_EQ(read_back.size(), 4);
    ASSERT(read_back[0] == 0xDE);
    ASSERT(read_back[3] == 0xEF);
    return true;
}

static bool test_migration_current_version() {
    TempDir dir;
    DatabaseHandle db;
    ASSERT(db.open(dir.file("mig.db")));

    MigrationRunner mig(db);
    auto v = mig.current_version();
    ASSERT(v);
    ASSERT_EQ(v.value(), 0);

    ASSERT(db.exec("PRAGMA user_version = 5;"));
    auto v2 = mig.current_version();
    ASSERT(v2);
    ASSERT_EQ(v2.value(), 5);
    return true;
}

static bool test_migration_ensure_schema() {
    TempDir dir;
    DatabaseHandle db;
    ASSERT(db.open(dir.file("schema.db")));

    MigrationRunner mig(db);
    auto v = mig.current_version();
    ASSERT(v);
    ASSERT_EQ(v.value(), 0);

    StoreInfo info{ StoreID::Node, "node", "node.db", 1 };
    auto r = mig.ensure_schema(info,
        "CREATE TABLE kv (key BLOB PRIMARY KEY, value BLOB) WITHOUT ROWID;");
    ASSERT(r);

    auto v2 = mig.current_version();
    ASSERT(v2);
    ASSERT_EQ(v2.value(), 1);

    // Second call should be no-op
    r = mig.ensure_schema(info,
        "CREATE TABLE kv (key BLOB PRIMARY KEY, value BLOB) WITHOUT ROWID;");
    ASSERT(r);

    auto v3 = mig.current_version();
    ASSERT(v3);
    ASSERT_EQ(v3.value(), 1);
    return true;
}

static bool test_sqlite_store_open() {
    TempDir dir;
    SqliteStore store(StoreID::Node, dir.path);

    auto r = store.open();
    ASSERT(r);
    ASSERT(store.is_open());
    ASSERT_EQ(store.store_id(), StoreID::Node);

    store.close();
    ASSERT(!store.is_open());
    return true;
}

static bool test_sqlite_store_put_get() {
    TempDir dir;
    SqliteStore store(StoreID::Session, dir.path);
    ASSERT(store.open());

    Bytes key = { 'k', 'e', 'y', '1' };
    Bytes val = { 'v', 'a', 'l', 'u', 'e' };

    auto r = store.put(key, val);
    ASSERT(r);

    auto got = store.get(key);
    ASSERT(got);
    ASSERT(got.value().size() == val.size());
    ASSERT(std::memcmp(got.value().data(), val.data(), val.size()) == 0);
    return true;
}

static bool test_sqlite_store_get_missing() {
    TempDir dir;
    SqliteStore store(StoreID::Trust, dir.path);
    ASSERT(store.open());

    Bytes key = { 'n', 'o', 'n', 'e' };
    auto r = store.get(key);
    ASSERT(!r);
    ASSERT_EQ(r.error().code.code, 902);
    return true;
}

static bool test_sqlite_store_put_overwrite() {
    TempDir dir;
    SqliteStore store(StoreID::DAG, dir.path);
    ASSERT(store.open());

    Bytes key = { 'x' };
    Bytes v1 = { '1' };
    Bytes v2 = { '2' };

    ASSERT(store.put(key, v1));
    ASSERT(store.put(key, v2));

    auto got = store.get(key);
    ASSERT(got);
    ASSERT_EQ(got.value().size(), 1);
    ASSERT_EQ(got.value()[0], '2');
    return true;
}

static bool test_sqlite_store_del() {
    TempDir dir;
    SqliteStore store(StoreID::Peer, dir.path);
    ASSERT(store.open());

    Bytes key = { 'd', 'e', 'l' };
    ASSERT(store.put(key, Bytes{ 'x' }));

    ASSERT(store.del(key));

    auto r = store.get(key);
    ASSERT(!r);
    ASSERT_EQ(r.error().code.code, 902);
    return true;
}

static bool test_sqlite_store_list_prefix() {
    TempDir dir;
    SqliteStore store(StoreID::Audit, dir.path);
    ASSERT(store.open());

    ASSERT(store.put(Bytes{ 'a', 'a', 'p', 'p', 'l', 'e' }, Bytes{ '1' }));
    ASSERT(store.put(Bytes{ 'a', 'p', 'r', 'i', 'c', 'o', 't' }, Bytes{ '2' }));
    ASSERT(store.put(Bytes{ 'b', 'a', 'n', 'a', 'n', 'a' }, Bytes{ '3' }));
    ASSERT(store.put(Bytes{ 'a', 'p' }, Bytes{ '4' }));

    Bytes prefix = { 'a', 'p' };
    auto keys = store.list(prefix);
    ASSERT(keys);
    ASSERT_EQ(keys.value().size(), 2);

    // Check the keys start with "ap"
    for (const auto& k : keys.value()) {
        ASSERT(k.size() >= 2);
        ASSERT(k[0] == 'a');
        ASSERT(k[1] == 'p');
    }
    return true;
}

static bool test_sqlite_store_transaction() {
    TempDir dir;
    SqliteStore store(StoreID::Governance, dir.path);
    ASSERT(store.open());

    Bytes key = { 't', 'x' };
    Bytes val = { 'd', 'a', 't', 'a' };

    ASSERT(store.begin_transaction());
    ASSERT(store.put(key, val));
    ASSERT(store.commit());

    auto got = store.get(key);
    ASSERT(got);
    return true;
}

static bool test_sqlite_store_transaction_rollback() {
    TempDir dir;
    SqliteStore store(StoreID::Mesh, dir.path);
    ASSERT(store.open());

    Bytes key = { 'r', 'b' };
    ASSERT(store.put(key, Bytes{ 'o', 'k' }));

    ASSERT(store.begin_transaction());
    ASSERT(store.put(key, Bytes{ 'n', 'e', 'w' }));
    ASSERT(store.rollback());

    auto got = store.get(key);
    ASSERT(got);
    ASSERT_EQ(got.value().size(), 2);
    ASSERT_EQ(got.value()[0], 'o');
    return true;
}

static bool test_sqlite_store_backup_restore() {
    TempDir dir;
    SqliteStore store(StoreID::Node, dir.path);
    ASSERT(store.open());

    Bytes key = { 'b' };
    ASSERT(store.put(key, Bytes{ 'a', 'c', 'k', 'u', 'p' }));

    std::string backup_path = dir.file("backup.db");

    auto r = store.backup(backup_path);
    ASSERT(r);

    // Write different data
    ASSERT(store.put(key, Bytes{ 'o', 'v', 'e', 'r', 'w', 'r', 'i', 't', 'e' }));

    // Restore
    r = store.restore(backup_path);
    ASSERT(r);

    // Should have original data
    auto got = store.get(key);
    ASSERT(got);
    ASSERT_EQ(got.value().size(), 5);
    ASSERT_EQ(got.value()[0], 'a');
    return true;
}

// ==========================================================================
// Main
// ==========================================================================

int main(int, char*[]) {
    printf("SMO Storage — Unit Tests\n");
    printf("=========================\n\n");

    TEST("StoreID constants")                    END_TEST(test_store_id_constants());
    TEST("StoreInfo lookup")                     END_TEST(test_store_info());
    TEST("DatabaseHandle open/close")            END_TEST(test_database_open_close());
    TEST("DatabaseHandle double open error")     END_TEST(test_database_double_open());
    TEST("DatabaseHandle exec SQL")              END_TEST(test_database_exec());
    TEST("DatabaseHandle prepare + step")        END_TEST(test_database_prepare_and_step());
    TEST("Statement blob round-trip")            END_TEST(test_statement_blob_roundtrip());
    TEST("MigrationRunner current_version")      END_TEST(test_migration_current_version());
    TEST("MigrationRunner ensure_schema")        END_TEST(test_migration_ensure_schema());
    TEST("SqliteStore open")                     END_TEST(test_sqlite_store_open());
    TEST("SqliteStore put/get")                  END_TEST(test_sqlite_store_put_get());
    TEST("SqliteStore get missing key")          END_TEST(test_sqlite_store_get_missing());
    TEST("SqliteStore put overwrite")            END_TEST(test_sqlite_store_put_overwrite());
    TEST("SqliteStore del")                      END_TEST(test_sqlite_store_del());
    TEST("SqliteStore list prefix")              END_TEST(test_sqlite_store_list_prefix());
    TEST("SqliteStore transaction")              END_TEST(test_sqlite_store_transaction());
    TEST("SqliteStore transaction rollback")     END_TEST(test_sqlite_store_transaction_rollback());
    TEST("SqliteStore backup/restore")           END_TEST(test_sqlite_store_backup_restore());

    printf("\n");
    if (failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("%d TEST(S) FAILED\n", failures);
        return 1;
    }
}
