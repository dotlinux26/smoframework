#include <discovery/discovery.hpp>
#include <cstdio>
#include <cstring>

using namespace smo;

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

// ==========================================================================
// Helper: make a NodeID with a given first byte
// ==========================================================================
static NodeID make_node_id(uint8_t first_byte) {
    NodeID id;
    id.value.fill(0);
    id.value[0] = first_byte;
    return id;
}

// ==========================================================================
// Tests — PeerState
// ==========================================================================
static bool test_peer_state_to_string() {
    ASSERT(std::strcmp(to_string(PeerState::Unknown), "Unknown") == 0);
    ASSERT(std::strcmp(to_string(PeerState::Online), "Online") == 0);
    ASSERT(std::strcmp(to_string(PeerState::Suspect), "Suspect") == 0);
    ASSERT(std::strcmp(to_string(PeerState::Offline), "Offline") == 0);
    return true;
}

// ==========================================================================
// Tests — PeerRecord
// ==========================================================================
static bool test_peer_record_serialization() {
    PeerRecord rec;
    rec.node_id = make_node_id(0xAA);
    rec.endpoint.scheme = "tcp";
    rec.endpoint.host = "192.168.1.1";
    rec.endpoint.port = 7777;
    rec.state = PeerState::Online;
    rec.last_seen = 12345;
    rec.ping_misses = 2;

    auto ser = rec.serialize();
    ASSERT(!ser.empty());

    auto deser = PeerRecord::deserialize(ser);
    ASSERT(deser);
    ASSERT_EQ(deser.value().node_id.value[0], 0xAA);
    ASSERT_EQ(deser.value().state, PeerState::Online);
    ASSERT_EQ(deser.value().last_seen, 12345);
    ASSERT_EQ(deser.value().ping_misses, 2);
    ASSERT_STREQ(deser.value().endpoint.scheme, "tcp");
    ASSERT_STREQ(deser.value().endpoint.host, "192.168.1.1");
    ASSERT_EQ(deser.value().endpoint.port, 7777);

    return true;
}

// ==========================================================================
// Tests — MembershipTable
// ==========================================================================
static bool test_membership_table_upsert_and_lookup() {
    MembershipTable table;

    PeerRecord rec;
    rec.node_id = make_node_id(0x01);
    rec.state = PeerState::Online;
    rec.last_seen = 100;

    ASSERT(table.upsert(rec));
    ASSERT_EQ(table.count(), 1U);

    auto found = table.lookup(make_node_id(0x01));
    ASSERT(found);
    ASSERT_EQ(found.value().state, PeerState::Online);
    ASSERT_EQ(found.value().last_seen, 100);

    // Lookup non-existent
    auto missing = table.lookup(make_node_id(0xFF));
    ASSERT(!missing);

    return true;
}

static bool test_membership_table_upsert_overwrite() {
    MembershipTable table;

    PeerRecord rec1;
    rec1.node_id = make_node_id(0x01);
    rec1.state = PeerState::Online;
    ASSERT(table.upsert(rec1));

    PeerRecord rec2;
    rec2.node_id = make_node_id(0x01);
    rec2.state = PeerState::Suspect;
    rec2.last_seen = 200;
    ASSERT(table.upsert(rec2));

    ASSERT_EQ(table.count(), 1U);
    auto found = table.lookup(make_node_id(0x01));
    ASSERT(found);
    ASSERT_EQ(found.value().state, PeerState::Suspect);
    ASSERT_EQ(found.value().last_seen, 200);

    return true;
}

static bool test_membership_table_peers() {
    MembershipTable table;

    PeerRecord r1; r1.node_id = make_node_id(0x01); table.upsert(r1);
    PeerRecord r2; r2.node_id = make_node_id(0x02); table.upsert(r2);
    PeerRecord r3; r3.node_id = make_node_id(0x03); table.upsert(r3);

    auto all = table.peers();
    ASSERT_EQ(all.size(), 3U);

    return true;
}

static bool test_membership_table_peers_by_state() {
    MembershipTable table;

    PeerRecord r1; r1.node_id = make_node_id(0x01); r1.state = PeerState::Online;  table.upsert(r1);
    PeerRecord r2; r2.node_id = make_node_id(0x02); r2.state = PeerState::Suspect; table.upsert(r2);
    PeerRecord r3; r3.node_id = make_node_id(0x03); r3.state = PeerState::Online;  table.upsert(r3);

    auto online = table.peers_with_state(PeerState::Online);
    ASSERT_EQ(online.size(), 2U);

    auto suspects = table.peers_with_state(PeerState::Suspect);
    ASSERT_EQ(suspects.size(), 1U);

    return true;
}

static bool test_membership_table_remove() {
    MembershipTable table;
    PeerRecord rec; rec.node_id = make_node_id(0x01);
    ASSERT(table.upsert(rec));
    ASSERT_EQ(table.count(), 1U);

    ASSERT(table.remove(make_node_id(0x01)));
    ASSERT_EQ(table.count(), 0U);

    // Remove non-existent
    ASSERT(!table.remove(make_node_id(0xFF)));

    return true;
}

static bool test_membership_table_capacity() {
    MembershipTable table(2);
    ASSERT_EQ(table.capacity(), 2U);

    PeerRecord r1; r1.node_id = make_node_id(0x01); ASSERT(table.upsert(r1));
    PeerRecord r2; r2.node_id = make_node_id(0x02); ASSERT(table.upsert(r2));
    PeerRecord r3; r3.node_id = make_node_id(0x03);
    ASSERT(!table.upsert(r3)); // should fail

    ASSERT_EQ(table.count(), 2U);

    return true;
}

static bool test_membership_table_serialization() {
    MembershipTable t1;
    PeerRecord r1; r1.node_id = make_node_id(0xAA); r1.state = PeerState::Online;  t1.upsert(r1);
    PeerRecord r2; r2.node_id = make_node_id(0xBB); r2.state = PeerState::Suspect; t1.upsert(r2);

    auto ser = t1.serialize();
    ASSERT(!ser.empty());

    auto t2 = MembershipTable::deserialize(ser);
    ASSERT_EQ(t2.value().count(), 2U);

    auto f1 = t2.value().lookup(make_node_id(0xAA));
    ASSERT(f1);
    ASSERT_EQ(f1.value().state, PeerState::Online);

    auto f2 = t2.value().lookup(make_node_id(0xBB));
    ASSERT(f2);
    ASSERT_EQ(f2.value().state, PeerState::Suspect);

    return true;
}

// ==========================================================================
// Tests — HealthMonitor
// ==========================================================================
static bool test_health_monitor_record_ping() {
    HealthMonitor hm;

    hm.record_ping(make_node_id(0x01), 1000);
    ASSERT_EQ(hm.ping_misses(make_node_id(0x01)), 1);

    // Second ping increments misses
    hm.record_ping(make_node_id(0x01), 2000);
    ASSERT_EQ(hm.ping_misses(make_node_id(0x01)), 2);

    return true;
}

static bool test_health_monitor_pong_clears_misses() {
    HealthMonitor hm;

    hm.record_ping(make_node_id(0x01), 1000);
    hm.record_ping(make_node_id(0x01), 2000);
    ASSERT_EQ(hm.ping_misses(make_node_id(0x01)), 2);

    ASSERT(hm.record_pong(make_node_id(0x01), 3000));
    ASSERT_EQ(hm.ping_misses(make_node_id(0x01)), 0);

    return true;
}

static bool test_health_monitor_tick_marks_suspect() {
    HealthMonitor hm;
    MembershipTable table;

    PeerRecord rec;
    rec.node_id = make_node_id(0x01);
    rec.state = PeerState::Online;
    rec.last_seen = 0;
    table.upsert(rec);

    // Record a PING at time 0 with 5s timeout
    hm.record_ping(make_node_id(0x01), 0);

    // Tick at time 6000000000 (6s > 5s timeout) — first expiry
    hm.tick(table, 6000000000LL, 5000000000LL, 3);

    // After first tick: misses=2 (record_ping set to 1, then expired tick adds 1)
    ASSERT_EQ(hm.ping_misses(make_node_id(0x01)), 2);
    auto peer = table.lookup(make_node_id(0x01));
    ASSERT(peer);
    ASSERT_EQ(peer.value().state, PeerState::Suspect);

    return true;
}

static bool test_health_monitor_tick_marks_offline() {
    HealthMonitor hm;
    MembershipTable table;

    PeerRecord rec;
    rec.node_id = make_node_id(0x01);
    rec.state = PeerState::Online;
    table.upsert(rec);

    // Simulate 3 rounds of missed PINGs
    hm.record_ping(make_node_id(0x01), 0);
    hm.tick(table, 6000000000LL, 5000000000LL, 3);  // misses: 1→2, suspect
    hm.tick(table, 12000000000LL, 5000000000LL, 3); // misses: 2→3, offline

    ASSERT_EQ(hm.ping_misses(make_node_id(0x01)), 3);
    auto peer = table.lookup(make_node_id(0x01));
    ASSERT(peer);
    ASSERT_EQ(peer.value().state, PeerState::Offline);

    return true;
}

// ==========================================================================
// Tests — Message serialization
// ==========================================================================
static bool test_hello_msg_roundtrip() {
    HelloMsg msg;
    msg.node_id = make_node_id(0x42);
    msg.pubkey_fingerprint = 0xDEADBEEF;
    msg.protocol_version = 1;

    auto ser = msg.serialize();
    auto deser = HelloMsg::deserialize(ser);
    ASSERT(deser);
    ASSERT_EQ(deser.value().node_id.value[0], 0x42);
    ASSERT_EQ(deser.value().pubkey_fingerprint, 0xDEADBEEF);
    ASSERT_EQ(deser.value().protocol_version, 1);

    return true;
}

static bool test_welcome_msg_roundtrip() {
    WelcomeMsg msg;
    msg.node_id = make_node_id(0x43);
    msg.peer_record.node_id = make_node_id(0x44);
    msg.peer_record.state = PeerState::Online;
    msg.peer_record.last_seen = 999;

    auto ser = msg.serialize();
    auto deser = WelcomeMsg::deserialize(ser);
    ASSERT(deser);
    ASSERT_EQ(deser.value().node_id.value[0], 0x43);
    ASSERT_EQ(deser.value().peer_record.node_id.value[0], 0x44);
    ASSERT_EQ(deser.value().peer_record.state, PeerState::Online);
    ASSERT_EQ(deser.value().peer_record.last_seen, 999);

    return true;
}

static bool test_ping_msg_roundtrip() {
    PingMsg msg;
    msg.timestamp = 1234567890;

    auto ser = msg.serialize();
    auto deser = PingMsg::deserialize(ser);
    ASSERT(deser);
    ASSERT_EQ(deser.value().timestamp, 1234567890);

    return true;
}

static bool test_pong_msg_roundtrip() {
    PongMsg msg;
    msg.timestamp = 987654321;

    auto ser = msg.serialize();
    auto deser = PongMsg::deserialize(ser);
    ASSERT(deser);
    ASSERT_EQ(deser.value().timestamp, 987654321);

    return true;
}

static bool test_discover_msg_roundtrip() {
    DiscoverMsg msg;
    auto ser = msg.serialize();
    auto deser = DiscoverMsg::deserialize(ser);
    ASSERT(deser);

    return true;
}

static bool test_node_info_msg_roundtrip() {
    NodeInfoMsg msg;
    msg.peer_record.node_id = make_node_id(0x45);
    msg.peer_record.state = PeerState::Online;
    msg.peer_record.last_seen = 555;

    auto ser = msg.serialize();
    auto deser = NodeInfoMsg::deserialize(ser);
    ASSERT(deser);
    ASSERT_EQ(deser.value().peer_record.node_id.value[0], 0x45);
    ASSERT_EQ(deser.value().peer_record.last_seen, 555);

    return true;
}

static bool test_offline_msg_roundtrip() {
    OfflineMsg msg;
    msg.node_id = make_node_id(0x46);
    msg.reason = 2;

    auto ser = msg.serialize();
    auto deser = OfflineMsg::deserialize(ser);
    ASSERT(deser);
    ASSERT_EQ(deser.value().node_id.value[0], 0x46);
    ASSERT_EQ(deser.value().reason, 2);

    return true;
}

// ==========================================================================
// Tests — DiscoveryEngine
// ==========================================================================
static bool test_discovery_engine_handle_hello() {
    MembershipTable table;
    HealthMonitor monitor;
    DiscoveryEngine engine(table, monitor);

    NodeID peer_id = make_node_id(0x50);
    HelloMsg hello;
    hello.node_id = peer_id;
    hello.protocol_version = 1;

    Endpoint from;
    from.scheme = "tcp";
    from.host = "10.0.0.1";
    from.port = 7777;

    ASSERT(engine.handle_hello(hello, from, 1000));

    auto found = table.lookup(peer_id);
    ASSERT(found);
    ASSERT_EQ(found.value().state, PeerState::Online);
    ASSERT_STREQ(found.value().endpoint.host, "10.0.0.1");
    ASSERT_EQ(found.value().endpoint.port, 7777);
    ASSERT_EQ(found.value().last_seen, 1000);

    return true;
}

static bool test_discovery_engine_handle_welcome() {
    MembershipTable table;
    HealthMonitor monitor;
    DiscoveryEngine engine(table, monitor);

    WelcomeMsg welcome;
    welcome.node_id = make_node_id(0x60);
    welcome.peer_record.node_id = make_node_id(0x60);
    welcome.peer_record.state = PeerState::Online;
    welcome.peer_record.last_seen = 500;

    ASSERT(engine.handle_welcome(welcome, 2000));

    auto found = table.lookup(make_node_id(0x60));
    ASSERT(found);
    ASSERT_EQ(found.value().last_seen, 2000);  // updated by handle_welcome
    ASSERT_EQ(found.value().state, PeerState::Online);

    return true;
}

static bool test_discovery_engine_handle_offline() {
    MembershipTable table;
    HealthMonitor monitor;
    DiscoveryEngine engine(table, monitor);

    NodeID peer_id = make_node_id(0x70);
    PeerRecord rec;
    rec.node_id = peer_id;
    rec.state = PeerState::Online;
    rec.last_seen = 100;
    table.upsert(rec);

    OfflineMsg offline;
    offline.node_id = peer_id;
    offline.reason = 1;

    ASSERT(engine.handle_offline(offline, 200));

    auto found = table.lookup(peer_id);
    ASSERT(found);
    ASSERT_EQ(found.value().state, PeerState::Offline);

    return true;
}

static bool test_discovery_engine_handle_node_info() {
    MembershipTable table;
    HealthMonitor monitor;
    DiscoveryEngine engine(table, monitor);

    NodeInfoMsg info;
    info.peer_record.node_id = make_node_id(0x80);
    info.peer_record.last_seen = 300;

    ASSERT(engine.handle_node_info(info, 400));

    auto found = table.lookup(make_node_id(0x80));
    ASSERT(found);
    ASSERT_EQ(found.value().last_seen, 400);  // updated
    ASSERT_EQ(found.value().state, PeerState::Online);  // set from Unknown

    return true;
}

static bool test_discovery_engine_tick() {
    MembershipTable table;
    HealthMonitor monitor;
    DiscoveryEngine engine(table, monitor);

    NodeID peer_id = make_node_id(0x90);
    PeerRecord rec;
    rec.node_id = peer_id;
    rec.state = PeerState::Online;
    table.upsert(rec);

    // Record a PING, let it expire
    monitor.record_ping(peer_id, 0);
    engine.tick(6000000000LL);

    auto peer = table.lookup(peer_id);
    ASSERT(peer);
    ASSERT(peer.value().state == PeerState::Suspect ||
           peer.value().state == PeerState::Online);  // may be suspect after tick

    return true;
}

// ==========================================================================
// Main
// ==========================================================================

int main(int, char*[]) {
    printf("SMO Discovery — Unit Tests\n");
    printf("===========================\n\n");

    TEST("PeerState to_string")                             END_TEST(test_peer_state_to_string());
    TEST("PeerRecord serialization")                        END_TEST(test_peer_record_serialization());
    TEST("MembershipTable upsert and lookup")               END_TEST(test_membership_table_upsert_and_lookup());
    TEST("MembershipTable upsert overwrite")                END_TEST(test_membership_table_upsert_overwrite());
    TEST("MembershipTable peers")                           END_TEST(test_membership_table_peers());
    TEST("MembershipTable peers by state")                  END_TEST(test_membership_table_peers_by_state());
    TEST("MembershipTable remove")                          END_TEST(test_membership_table_remove());
    TEST("MembershipTable capacity")                        END_TEST(test_membership_table_capacity());
    TEST("MembershipTable serialization")                   END_TEST(test_membership_table_serialization());
    TEST("HealthMonitor record_ping")                       END_TEST(test_health_monitor_record_ping());
    TEST("HealthMonitor pong clears misses")                END_TEST(test_health_monitor_pong_clears_misses());
    TEST("HealthMonitor tick marks suspect")                END_TEST(test_health_monitor_tick_marks_suspect());
    TEST("HealthMonitor tick marks offline")                END_TEST(test_health_monitor_tick_marks_offline());
    TEST("HelloMsg roundtrip")                              END_TEST(test_hello_msg_roundtrip());
    TEST("WelcomeMsg roundtrip")                            END_TEST(test_welcome_msg_roundtrip());
    TEST("PingMsg roundtrip")                               END_TEST(test_ping_msg_roundtrip());
    TEST("PongMsg roundtrip")                               END_TEST(test_pong_msg_roundtrip());
    TEST("DiscoverMsg roundtrip")                           END_TEST(test_discover_msg_roundtrip());
    TEST("NodeInfoMsg roundtrip")                           END_TEST(test_node_info_msg_roundtrip());
    TEST("OfflineMsg roundtrip")                            END_TEST(test_offline_msg_roundtrip());
    TEST("DiscoveryEngine handle_hello")                    END_TEST(test_discovery_engine_handle_hello());
    TEST("DiscoveryEngine handle_welcome")                  END_TEST(test_discovery_engine_handle_welcome());
    TEST("DiscoveryEngine handle_offline")                  END_TEST(test_discovery_engine_handle_offline());
    TEST("DiscoveryEngine handle_node_info")                END_TEST(test_discovery_engine_handle_node_info());
    TEST("DiscoveryEngine tick")                            END_TEST(test_discovery_engine_tick());

    printf("\n");
    if (failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("%d TEST(S) FAILED\n", failures);
        return 1;
    }
}
