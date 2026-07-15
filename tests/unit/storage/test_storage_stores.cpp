#include <core/storage/sqlite_store.hpp>
#include <storage/session_store/session_store.h>
#include <storage/audit_store/audit_store.h>
#include <storage/node_store/node_store.h>
#include <storage/dag_store/dag_store.h>
#include <storage/trust_store/trust_store.h>
#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace smo;

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
                   "      LHS=%lld  RHS=%lld\n",                        \
                   __FILE__, __LINE__, #a, #b,                          \
                   static_cast<long long>(a),                           \
                   static_cast<long long>(b));                          \
            return false;                                               \
        }                                                               \
    } while (false)

#define ASSERT_STREQ(a, b)                                              \
    do {                                                                \
        const auto& _a = (a);                                           \
        const auto& _b = (b);                                           \
        if (_a != _b) {                                                 \
            printf("\n    ASSERTION FAILED at %s:%d: %s == %s\n"        \
                   "      LHS=\"%s\"  RHS=\"%s\"\n",                     \
                   __FILE__, __LINE__, #a, #b,                          \
                   std::string(_a).c_str(),                             \
                   std::string(_b).c_str());                            \
            return false;                                               \
        }                                                               \
    } while (false)

struct TempDir {
    std::string path;
    TempDir() {
        auto tmp = std::filesystem::temp_directory_path();
        path = (tmp / "smo_stores_XXXXXX").string();
        char buf[256];
        std::strncpy(buf, path.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        ::mkdtemp(buf);
        path = buf;
    }
    ~TempDir() { std::filesystem::remove_all(path); }
};

// ── SessionStore ───────────────────────────────────────────────────────

static bool test_session_store_put_get() {
    TempDir dir;
    SessionStore store(dir.path);
    ASSERT(!store.open());

    SessionId sid;
    sid.bytes.fill(0x42);
    Session session;
    session.id = sid;
    session.requester = "alice";
    session.responder = "bob";
    session.capabilities.set(static_cast<size_t>(0));
    session.created_at = 1000;
    session.expires_at = 5000;
    session.active = true;

    ASSERT(!store.put(session));

    auto got = store.get(sid);
    ASSERT(got.has_value());
    ASSERT(got->active);
    ASSERT_STREQ(got->requester, "alice");
    ASSERT_EQ(got->created_at, 1000);
    ASSERT_EQ(got->expires_at, 5000);

    store.close();
    return true;
}

static bool test_session_store_get_missing() {
    TempDir dir;
    SessionStore store(dir.path);
    ASSERT(!store.open());

    SessionId sid;
    sid.bytes.fill(0xFF);
    auto got = store.get(sid);
    ASSERT(!got.has_value());

    store.close();
    return true;
}

static bool test_session_store_remove() {
    TempDir dir;
    SessionStore store(dir.path);
    ASSERT(!store.open());

    SessionId sid;
    sid.bytes.fill(0x10);
    Session session;
    session.id = sid;
    ASSERT(!store.put(session));
    ASSERT(!store.remove(sid));
    ASSERT(!store.get(sid).has_value());

    store.close();
    return true;
}

// ── AuditStore ─────────────────────────────────────────────────────────

static bool test_audit_store_append_query() {
    TempDir dir;
    AuditStore store(dir.path);
    ASSERT(!store.open());

    AuditStore::Entry e1;
    e1.contract_id = "ctr1";
    e1.from_state = "Init";
    e1.to_state = "Running";
    e1.trigger = "start";
    e1.timestamp = 1000;
    e1.signature = "sig1";
    ASSERT(!store.append(e1));

    AuditStore::Entry e2;
    e2.contract_id = "ctr1";
    e2.from_state = "Running";
    e2.to_state = "Done";
    e2.trigger = "complete";
    e2.timestamp = 2000;
    e2.signature = "sig2";
    ASSERT(!store.append(e2));

    auto entries = store.query("ctr1");
    ASSERT_EQ(entries.size(), 2);
    ASSERT_STREQ(entries[0].trigger, "start");
    ASSERT_STREQ(entries[1].trigger, "complete");

    store.close();
    return true;
}

static bool test_audit_store_query_empty() {
    TempDir dir;
    AuditStore store(dir.path);
    ASSERT(!store.open());

    auto entries = store.query("nonexistent");
    ASSERT_EQ(entries.size(), 0);

    store.close();
    return true;
}

static bool test_audit_store_last() {
    TempDir dir;
    AuditStore store(dir.path);
    ASSERT(!store.open());

    AuditStore::Entry e;
    e.contract_id = "ctr2";
    e.from_state = "Init";
    e.to_state = "Running";
    e.trigger = "start";
    e.timestamp = 42;
    ASSERT(!store.append(e));

    e.from_state = "Running";
    e.to_state = "Done";
    e.trigger = "update";
    e.timestamp = 99;
    ASSERT(!store.append(e));

    auto last = store.last("ctr2");
    ASSERT(last.has_value());
    ASSERT_EQ(last->timestamp, 99);
    ASSERT_STREQ(last->trigger, "update");

    store.close();
    return true;
}

// ── NodeStore ──────────────────────────────────────────────────────────

static bool test_node_store_identity() {
    TempDir dir;
    NodeStore store(dir.path);
    ASSERT(!store.open());

    NodeStore::Identity id;
    id.node_id = "node1";
    id.public_key = {0x01, 0x02};
    id.secret_key = {0xAA, 0xBB};
    id.domain = "mesh.example.com";
    ASSERT(!store.put_identity(id));

    auto got = store.get_identity();
    ASSERT(got.has_value());
    ASSERT_STREQ(got->node_id, "node1");
    ASSERT_EQ(got->public_key.value.size(), 32);
    ASSERT_EQ(got->public_key.value[0], 0x01);
    ASSERT_EQ(got->secret_key.value.size(), 64);
    ASSERT_EQ(got->secret_key.value[1], 0xBB);
    ASSERT_STREQ(got->domain, "mesh.example.com");

    store.close();
    return true;
}

static bool test_node_store_route() {
    TempDir dir;
    NodeStore store(dir.path);
    ASSERT(!store.open());

    NodeStore::RouteEntry rt;
    rt.node_id = "node2";
    rt.address = "10.0.0.1";
    rt.port = 8080;
    rt.last_seen = 12345;
    rt.trust_score = 0.95;
    ASSERT(!store.put_route(rt));

    auto got = store.get_route("node2");
    ASSERT(got.has_value());
    ASSERT_STREQ(got->address, "10.0.0.1");
    ASSERT_EQ(got->port, 8080);

    store.close();
    return true;
}

static bool test_node_store_all_routes() {
    TempDir dir;
    NodeStore store(dir.path);
    ASSERT(!store.open());

    NodeStore::RouteEntry r1;
    r1.node_id = "n1"; r1.address = "a1"; r1.port = 1;
    ASSERT(!store.put_route(r1));

    NodeStore::RouteEntry r2;
    r2.node_id = "n2"; r2.address = "a2"; r2.port = 2;
    ASSERT(!store.put_route(r2));

    auto all = store.all_routes();
    ASSERT_EQ(all.size(), 2);

    store.close();
    return true;
}

// ── DagStore ───────────────────────────────────────────────────────────

static bool test_dag_store_put_get() {
    TempDir dir;
    DagStore store(dir.path);
    ASSERT(!store.open());

    DagStore::DagRecord dag;
    dag.graph_id = "dag1";
    dag.intent_id = "intent1";
    dag.dag_json = R"({"nodes":[]})";
    dag.dag_hash = "abc123";
    dag.compiled_at = 1000;
    ASSERT(!store.put(dag));

    auto got = store.get("dag1");
    ASSERT(got.has_value());
    ASSERT_STREQ(got->intent_id, "intent1");
    ASSERT_STREQ(got->dag_hash, "abc123");
    ASSERT_EQ(got->compiled_at, 1000);

    store.close();
    return true;
}

static bool test_dag_store_get_missing() {
    TempDir dir;
    DagStore store(dir.path);
    ASSERT(!store.open());

    auto got = store.get("no_such_dag");
    ASSERT(!got.has_value());

    store.close();
    return true;
}

// ── TrustStore ─────────────────────────────────────────────────────────

static bool test_trust_store_put_get() {
    TempDir dir;
    TrustStore store(dir.path);
    ASSERT(!store.open());

    TrustStore::Record rec;
    rec.node_id = "trusted_node";
    rec.citizen_score = 0.8;
    rec.execution_score = 0.9;
    rec.witness_score = 0.7;
    rec.consistency_score = 0.85;
    rec.composite = 0.81;
    rec.updated_at = 1000;
    ASSERT(!store.put(rec));

    auto got = store.get("trusted_node");
    ASSERT(got.has_value());
    ASSERT_EQ(got->citizen_score, 0.8);
    ASSERT_EQ(got->composite, 0.81);
    ASSERT_EQ(got->updated_at, 1000);

    store.close();
    return true;
}

static bool test_trust_store_get_missing() {
    TempDir dir;
    TrustStore store(dir.path);
    ASSERT(!store.open());

    auto got = store.get("unknown_node");
    ASSERT(!got.has_value());

    store.close();
    return true;
}

static bool test_trust_store_remove() {
    TempDir dir;
    TrustStore store(dir.path);
    ASSERT(!store.open());

    TrustStore::Record rec;
    rec.node_id = "to_remove";
    rec.citizen_score = 0.5;
    ASSERT(!store.put(rec));
    ASSERT(!store.remove("to_remove"));
    ASSERT(!store.get("to_remove").has_value());

    store.close();
    return true;
}

// ── Main ───────────────────────────────────────────────────────────────

int main(int, char*[]) {
    printf("SMO Storage Stores — Unit Tests\n");
    printf("================================\n\n");

    TEST("SessionStore put/get")                       END_TEST(test_session_store_put_get());
    TEST("SessionStore get missing")                   END_TEST(test_session_store_get_missing());
    TEST("SessionStore remove")                        END_TEST(test_session_store_remove());
    TEST("AuditStore append/query")                    END_TEST(test_audit_store_append_query());
    TEST("AuditStore query empty")                     END_TEST(test_audit_store_query_empty());
    TEST("AuditStore last")                            END_TEST(test_audit_store_last());
    TEST("NodeStore identity")                         END_TEST(test_node_store_identity());
    TEST("NodeStore route")                            END_TEST(test_node_store_route());
    TEST("NodeStore all routes")                       END_TEST(test_node_store_all_routes());
    TEST("DagStore put/get")                           END_TEST(test_dag_store_put_get());
    TEST("DagStore get missing")                       END_TEST(test_dag_store_get_missing());
    TEST("TrustStore put/get")                         END_TEST(test_trust_store_put_get());
    TEST("TrustStore get missing")                     END_TEST(test_trust_store_get_missing());
    TEST("TrustStore remove")                          END_TEST(test_trust_store_remove());

    printf("\n");
    if (failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("%d TEST(S) FAILED\n", failures);
        return 1;
    }
}
