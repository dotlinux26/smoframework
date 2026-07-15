#include <session/session.hpp>
#include <crypto/registry.hpp>
#include <crypto/impl.hpp>
#include <cstdio>
#include <cstring>

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
// Mock crypto for SessionId derivation
// ==========================================================================

static Result<Bytes> mock_hash(BytesView data) {
    Bytes out(16, 0);
    out[0] = static_cast<uint8_t>(data.size());
    for (size_t i = 0; i < data.size(); ++i) out[(i % 15) + 1] ^= data[i];
    return out;
}

static Result<Bytes> mock_hmac(BytesView key, BytesView data) {
    (void)key;
    return mock_hash(data);
}

static Result<bool> mock_verify(BytesView msg, BytesView sig, BytesView pk) {
    (void)msg; (void)sig; (void)pk;
    return true;
}

static Result<Bytes> mock_sign(BytesView msg, BytesView sk, RngRef& rng) {
    (void)msg; (void)sk;
    Bytes sig(64);
    rng.fill(sig);
    return sig;
}

static const HashImpl kHash{mock_hash, mock_hmac};
static const SignerImpl kSigner{nullptr, mock_sign, mock_verify};

// ==========================================================================
// Tests
// ==========================================================================

// ── SessionId ──────────────────────────────────────────────────────────

static bool test_session_id_derive() {
    Bytes seed = {0x01, 0x02, 0x03};
    auto id = SessionId::derive(seed, kHash);
    ASSERT(id);
    // Mock hash: first byte = seed length (3)
    ASSERT_EQ(id.value().bytes[0], 3);
    return true;
}

static bool test_session_id_null_hash() {
    HashImpl empty{};
    auto id = SessionId::derive(Bytes{1, 2, 3}, empty);
    ASSERT(!id);
    return true;
}

static bool test_session_id_to_from_bytes() {
    SessionId id;
    id.bytes = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    auto ser = id.to_bytes();
    auto deser = SessionId::from_bytes(ser);
    ASSERT(deser);
    ASSERT_EQ(deser.value().bytes[0], 0);
    ASSERT_EQ(deser.value().bytes[15], 15);
    return true;
}

static bool test_session_id_from_bytes_truncated() {
    Bytes bad = {1,2,3};
    auto id = SessionId::from_bytes(bad);
    ASSERT(!id);
    return true;
}

// ── FSM transitions ──────────────────────────────────────────────────

static bool test_fsm_valid_transitions() {
    ASSERT(is_valid_transition(SessionState::Closed, SessionEvent::OpenRequest));

    ASSERT(is_valid_transition(SessionState::Handshake, SessionEvent::Established));
    ASSERT(is_valid_transition(SessionState::Handshake, SessionEvent::Close));
    ASSERT(is_valid_transition(SessionState::Handshake, SessionEvent::Timeout));
    ASSERT(is_valid_transition(SessionState::Handshake, SessionEvent::Error));

    ASSERT(is_valid_transition(SessionState::Established, SessionEvent::Activate));
    ASSERT(is_valid_transition(SessionState::Established, SessionEvent::Renew));
    ASSERT(is_valid_transition(SessionState::Established, SessionEvent::Close));
    ASSERT(is_valid_transition(SessionState::Established, SessionEvent::Timeout));
    ASSERT(is_valid_transition(SessionState::Established, SessionEvent::Error));

    ASSERT(is_valid_transition(SessionState::Active, SessionEvent::CompleteContract));
    ASSERT(is_valid_transition(SessionState::Active, SessionEvent::Close));
    ASSERT(is_valid_transition(SessionState::Active, SessionEvent::Error));

    ASSERT(is_valid_transition(SessionState::Renewing, SessionEvent::Established));
    ASSERT(is_valid_transition(SessionState::Renewing, SessionEvent::Close));
    ASSERT(is_valid_transition(SessionState::Renewing, SessionEvent::Timeout));
    ASSERT(is_valid_transition(SessionState::Renewing, SessionEvent::Error));

    return true;
}

static bool test_fsm_invalid_transitions() {
    // Closed → anything except OpenRequest
    ASSERT(!is_valid_transition(SessionState::Closed, SessionEvent::Activate));
    ASSERT(!is_valid_transition(SessionState::Closed, SessionEvent::Established));
    ASSERT(!is_valid_transition(SessionState::Closed, SessionEvent::Renew));

    // Handshake → not established/close/timeout/error
    ASSERT(!is_valid_transition(SessionState::Handshake, SessionEvent::Activate));
    ASSERT(!is_valid_transition(SessionState::Handshake, SessionEvent::Renew));
    ASSERT(!is_valid_transition(SessionState::Handshake, SessionEvent::OpenRequest));

    // Established → not renew/close/timeout/error/activate
    ASSERT(!is_valid_transition(SessionState::Established, SessionEvent::OpenRequest));
    ASSERT(!is_valid_transition(SessionState::Established, SessionEvent::Established));

    // Active → only CompleteContract/Close/Error
    ASSERT(!is_valid_transition(SessionState::Active, SessionEvent::Renew));
    ASSERT(!is_valid_transition(SessionState::Active, SessionEvent::OpenRequest));
    ASSERT(!is_valid_transition(SessionState::Active, SessionEvent::Timeout));

    // Renewing → only Established/Close/Timeout/Error
    ASSERT(!is_valid_transition(SessionState::Renewing, SessionEvent::Activate));
    ASSERT(!is_valid_transition(SessionState::Renewing, SessionEvent::OpenRequest));

    return true;
}

static bool test_fsm_apply_transition() {
    ASSERT_EQ(apply_transition(SessionState::Closed, SessionEvent::OpenRequest),
              SessionState::Handshake);

    ASSERT_EQ(apply_transition(SessionState::Handshake, SessionEvent::Established),
              SessionState::Established);
    ASSERT_EQ(apply_transition(SessionState::Handshake, SessionEvent::Close),
              SessionState::Closed);

    ASSERT_EQ(apply_transition(SessionState::Established, SessionEvent::Activate),
              SessionState::Active);
    ASSERT_EQ(apply_transition(SessionState::Established, SessionEvent::Renew),
              SessionState::Renewing);
    ASSERT_EQ(apply_transition(SessionState::Established, SessionEvent::Close),
              SessionState::Closed);

    ASSERT_EQ(apply_transition(SessionState::Active, SessionEvent::CompleteContract),
              SessionState::Established);
    ASSERT_EQ(apply_transition(SessionState::Active, SessionEvent::Close),
              SessionState::Closed);

    ASSERT_EQ(apply_transition(SessionState::Renewing, SessionEvent::Established),
              SessionState::Established);
    ASSERT_EQ(apply_transition(SessionState::Renewing, SessionEvent::Close),
              SessionState::Closed);
    ASSERT_EQ(apply_transition(SessionState::Renewing, SessionEvent::Timeout),
              SessionState::Closed);

    // Invalid: should stay in current state
    ASSERT_EQ(apply_transition(SessionState::Closed, SessionEvent::Activate),
              SessionState::Closed);

    return true;
}

static bool test_session_state_to_string() {
    ASSERT(std::strcmp(to_string(SessionState::Closed), "Closed") == 0);
    ASSERT(std::strcmp(to_string(SessionState::Handshake), "Handshake") == 0);
    ASSERT(std::strcmp(to_string(SessionState::Established), "Established") == 0);
    ASSERT(std::strcmp(to_string(SessionState::Active), "Active") == 0);
    ASSERT(std::strcmp(to_string(SessionState::Renewing), "Renewing") == 0);
    ASSERT(std::strcmp(to_string(static_cast<SessionState>(99)), "Unknown") == 0);
    return true;
}

// ── Session lifecycle ─────────────────────────────────────────────────

static bool test_session_create() {
    SessionId id;
    id.bytes = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};

    NodeID peer_id;
    peer_id.value.fill(0xAB);

    Certificate cert;
    cert.subject_pubkey = {0x01, 0x02};
    cert.mesh_id = {0xAA, 0xBB};
    cert.epoch = 1;

    CapabilitySet caps;
    caps.set(static_cast<size_t>(Capability::FS_READ));
    caps.set(static_cast<size_t>(Capability::HEARTBEAT));

    auto session = Session::create(id, peer_id, cert, caps, 1000, 3600000000000ULL); // 1h TTL
    ASSERT(session);
    ASSERT_EQ(session.value().state(), SessionState::Handshake);
    ASSERT_EQ(session.value().id().bytes[0], 1);
    ASSERT_EQ(session.value().peer_id().value[0], 0xAB);
    ASSERT_EQ(session.value().created_at(), 1000);
    ASSERT_EQ(session.value().expires_at(), 1000 + 3600000000000LL);

    return true;
}

static bool test_session_full_lifecycle() {
    SessionId id;
    id.bytes.fill(0x42);
    NodeID peer_id;
    peer_id.value.fill(0x01);
    Certificate cert;
    cert.subject_pubkey = {0xAA};
    cert.mesh_id = {0xBB};
    CapabilitySet caps;

    auto session = Session::create(id, peer_id, cert, caps, 0, 1000000);
    ASSERT(session);

    // Handshake → Established
    ASSERT(session.value().on_event(SessionEvent::Established, 100));
    ASSERT_EQ(session.value().state(), SessionState::Established);

    // Established → Active
    ASSERT(session.value().on_event(SessionEvent::Activate, 200));
    ASSERT_EQ(session.value().state(), SessionState::Active);

    // Active → Established (contract complete)
    ASSERT(session.value().on_event(SessionEvent::CompleteContract, 300));
    ASSERT_EQ(session.value().state(), SessionState::Established);

    // Established → Closed (close)
    ASSERT(session.value().on_event(SessionEvent::Close, 400));
    ASSERT_EQ(session.value().state(), SessionState::Closed);
    ASSERT(!session.value().is_valid_at(500));

    return true;
}

static bool test_session_invalid_transition() {
    SessionId id;
    id.bytes.fill(0x01);
    NodeID peer_id;
    peer_id.value.fill(0x02);
    Certificate cert;
    CapabilitySet caps;

    auto session = Session::create(id, peer_id, cert, caps, 0, 1000000);
    ASSERT(session);

    // Handshake → Activate should fail (not allowed)
    auto r = session.value().on_event(SessionEvent::Activate, 100);
    ASSERT(!r);
    // State should remain Handshake
    ASSERT_EQ(session.value().state(), SessionState::Handshake);

    return true;
}

static bool test_session_renew() {
    SessionId id;
    id.bytes.fill(0x03);
    NodeID peer_id;
    peer_id.value.fill(0x04);
    Certificate cert;
    CapabilitySet caps;

    auto session = Session::create(id, peer_id, cert, caps, 0, 1000000);
    ASSERT(session);
    session.value().on_event(SessionEvent::Established, 100);

    // Renew in Established state
    ASSERT(session.value().renew(500, 2000000));
    ASSERT_EQ(session.value().expires_at(), 500 + 2000000);

    // Renew in Closed state should fail
    session.value().on_event(SessionEvent::Close, 600);
    ASSERT(!session.value().renew(700, 1000000));

    return true;
}

static bool test_session_is_valid_at() {
    SessionId id;
    id.bytes.fill(0x05);
    NodeID peer_id;
    peer_id.value.fill(0x06);
    Certificate cert;
    CapabilitySet caps;

    auto session = Session::create(id, peer_id, cert, caps, 0, 1000);
    ASSERT(session);
    session.value().on_event(SessionEvent::Established, 10);

    ASSERT(session.value().is_valid_at(500));
    ASSERT(!session.value().is_valid_at(2000)); // past TTL

    return true;
}

static bool test_session_has_capability() {
    SessionId id;
    id.bytes.fill(0x07);
    NodeID peer_id;
    peer_id.value.fill(0x08);
    Certificate cert;
    CapabilitySet caps;
    caps.set(static_cast<size_t>(Capability::FS_READ));
    caps.set(static_cast<size_t>(Capability::HEARTBEAT));

    auto session = Session::create(id, peer_id, cert, caps, 0, 1000);
    ASSERT(session);

    ASSERT(session.value().has_capability(Capability::FS_READ));
    ASSERT(session.value().has_capability(Capability::HEARTBEAT));
    ASSERT(!session.value().has_capability(Capability::FS_WRITE));
    ASSERT(!session.value().has_capability(Capability::PROC_EXEC));

    return true;
}

// ── Session serialization ─────────────────────────────────────────────

static bool test_session_serialization_roundtrip() {
    SessionId id;
    id.bytes = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    NodeID peer_id;
    peer_id.value.fill(0xAA);
    Certificate cert;
    cert.subject_pubkey = {0xBB, 0xCC};
    cert.issuer_pubkey = {0xDD};
    cert.mesh_id = {0xEE, 0xFF};
    cert.role = Role::Contributor;
    cert.epoch = 5;
    cert.not_before = 100;
    cert.not_after = 200;

    CapabilitySet caps;
    caps.set(static_cast<size_t>(Capability::FS_READ));
    caps.set(static_cast<size_t>(Capability::NET_BIND));

    auto s1 = Session::create(id, peer_id, cert, caps, 1000, 5000);
    ASSERT(s1);
    s1.value().on_event(SessionEvent::Established, 1000);

    auto ser = s1.value().serialize();
    ASSERT(!ser.empty());

    auto s2 = Session::deserialize(ser);
    ASSERT(s2);
    ASSERT_EQ(s2.value().state(), s1.value().state());
    ASSERT_EQ(s2.value().id().bytes[0], s1.value().id().bytes[0]);
    ASSERT_EQ(s2.value().id().bytes[15], s1.value().id().bytes[15]);
    ASSERT_EQ(s2.value().peer_id().value[0], s1.value().peer_id().value[0]);
    ASSERT_EQ(s2.value().created_at(), s1.value().created_at());
    ASSERT_EQ(s2.value().expires_at(), s1.value().expires_at());
    ASSERT(s2.value().has_capability(Capability::FS_READ));
    ASSERT(s2.value().has_capability(Capability::NET_BIND));
    ASSERT(!s2.value().has_capability(Capability::FS_WRITE));

    return true;
}

// ── SessionManager ─────────────────────────────────────────────────────

static bool test_manager_open_and_lookup() {
    SessionManager mgr;
    SessionId id;
    id.bytes.fill(0x10);
    NodeID peer_id;
    peer_id.value.fill(0x20);
    Certificate cert;
    CapabilitySet caps;

    auto session = Session::create(id, peer_id, cert, caps, 0, 1000);
    ASSERT(session);

    auto ptr_result = mgr.open(std::move(session.value()));
    ASSERT(ptr_result);
    ASSERT(ptr_result.value() != nullptr);
    ASSERT_EQ(mgr.active_count(), 1U);

    auto* found = mgr.lookup(id);
    ASSERT(found != nullptr);
    ASSERT_EQ(found->id().bytes[0], 0x10);

    return true;
}

static bool test_manager_close_and_garbage_collect() {
    SessionManager mgr;
    SessionId id;
    id.bytes.fill(0x30);
    NodeID peer_id;
    peer_id.value.fill(0x40);
    Certificate cert;
    CapabilitySet caps;

    auto session = Session::create(id, peer_id, cert, caps, 0, 1000);
    ASSERT(session);
    mgr.open(std::move(session.value()));

    ASSERT(mgr.close(id, 100));
    ASSERT_EQ(mgr.active_count(), 1U); // still in map, just Closed state

    auto* found = mgr.lookup(id);
    ASSERT(found);
    ASSERT_EQ(found->state(), SessionState::Closed);

    mgr.collect_garbage();
    ASSERT_EQ(mgr.active_count(), 0U); // removed

    return true;
}

static bool test_manager_duplicate_open_fails() {
    SessionManager mgr;
    SessionId id;
    id.bytes.fill(0x50);
    NodeID peer_id;
    peer_id.value.fill(0x60);
    Certificate cert;
    CapabilitySet caps;

    auto s1 = Session::create(id, peer_id, cert, caps, 0, 1000);
    ASSERT(s1);
    ASSERT(mgr.open(std::move(s1.value())));

    auto s2 = Session::create(id, peer_id, cert, caps, 0, 1000);
    ASSERT(s2);
    auto result = mgr.open(std::move(s2.value()));
    ASSERT(!result); // should fail

    return true;
}

static bool test_manager_tick_expires_sessions() {
    SessionManager mgr;

    // Create two sessions
    SessionId id1; id1.bytes.fill(0x70);
    SessionId id2; id2.bytes.fill(0x71);
    NodeID peer_id;
    Certificate cert;
    CapabilitySet caps;

    auto s1 = Session::create(id1, peer_id, cert, caps, 0, 500);
    ASSERT(s1);
    s1.value().on_event(SessionEvent::Established, 10);
    ASSERT(mgr.open(std::move(s1.value())));

    auto s2 = Session::create(id2, peer_id, cert, caps, 0, 100000);
    ASSERT(s2);
    s2.value().on_event(SessionEvent::Established, 10);
    ASSERT(mgr.open(std::move(s2.value())));

    // Tick at time 1000 — s1 should expire, s2 should not
    mgr.tick(1000);

    auto* f1 = mgr.lookup(id1);
    ASSERT(f1);
    ASSERT_EQ(f1->state(), SessionState::Closed); // timed out

    auto* f2 = mgr.lookup(id2);
    ASSERT(f2);
    ASSERT_EQ(f2->state(), SessionState::Established); // still active

    return true;
}

static bool test_manager_serialize_all() {
    SessionManager mgr;

    SessionId id1; id1.bytes.fill(0x80);
    SessionId id2; id2.bytes.fill(0x81);
    NodeID peer_id;
    Certificate cert;
    CapabilitySet caps;

    auto s1 = Session::create(id1, peer_id, cert, caps, 0, 5000);
    ASSERT(s1);
    ASSERT(mgr.open(std::move(s1.value())));

    auto s2 = Session::create(id2, peer_id, cert, caps, 100, 10000);
    ASSERT(s2);
    ASSERT(mgr.open(std::move(s2.value())));

    auto ser = mgr.serialize_all();
    ASSERT(!ser.empty());

    return true;
}

// ── SessionOpenMsg / SessionCloseMsg ───────────────────────────────────

static bool test_session_open_msg_roundtrip() {
    SessionOpenMsg msg;
    msg.nonce = Bytes(32, 0xAA);
    msg.signature = Bytes(64, 0xBB);
    msg.cert_data = {0xCC, 0xDD};

    auto ser = msg.serialize();
    auto deser = SessionOpenMsg::deserialize(ser);
    ASSERT(deser);
    ASSERT_EQ(deser.value().nonce.size(), 32U);
    ASSERT_EQ(deser.value().nonce[0], 0xAA);
    ASSERT_EQ(deser.value().signature.size(), 64U);
    ASSERT_EQ(deser.value().signature[0], 0xBB);
    ASSERT_EQ(deser.value().cert_data.size(), 2U);
    ASSERT_EQ(deser.value().cert_data[0], 0xCC);

    return true;
}

static bool test_session_close_msg_roundtrip() {
    SessionCloseMsg msg;
    msg.reason = 1;
    msg.signature = Bytes(64, 0xDD);

    auto ser = msg.serialize();
    auto deser = SessionCloseMsg::deserialize(ser);
    ASSERT(deser);
    ASSERT_EQ(deser.value().reason, 1);
    ASSERT_EQ(deser.value().signature.size(), 64U);
    ASSERT_EQ(deser.value().signature[0], 0xDD);

    return true;
}

// ==========================================================================
// Main
// ==========================================================================

int main(int, char*[]) {
    printf("SMO Session — Unit Tests\n");
    printf("=========================\n\n");

    TEST("SessionId derive")                         END_TEST(test_session_id_derive());
    TEST("SessionId null hash fails")                END_TEST(test_session_id_null_hash());
    TEST("SessionId to/from bytes")                  END_TEST(test_session_id_to_from_bytes());
    TEST("SessionId truncated fails")                END_TEST(test_session_id_from_bytes_truncated());
    TEST("FSM valid transitions")                    END_TEST(test_fsm_valid_transitions());
    TEST("FSM invalid transitions")                  END_TEST(test_fsm_invalid_transitions());
    TEST("FSM apply transition")                     END_TEST(test_fsm_apply_transition());
    TEST("SessionState to_string")                   END_TEST(test_session_state_to_string());
    TEST("Session create")                           END_TEST(test_session_create());
    TEST("Session full lifecycle")                   END_TEST(test_session_full_lifecycle());
    TEST("Session invalid transition")               END_TEST(test_session_invalid_transition());
    TEST("Session renew")                            END_TEST(test_session_renew());
    TEST("Session is_valid_at")                      END_TEST(test_session_is_valid_at());
    TEST("Session has_capability")                   END_TEST(test_session_has_capability());
    TEST("Session serialization roundtrip")          END_TEST(test_session_serialization_roundtrip());
    TEST("Manager open and lookup")                  END_TEST(test_manager_open_and_lookup());
    TEST("Manager close and garbage collect")        END_TEST(test_manager_close_and_garbage_collect());
    TEST("Manager duplicate open fails")             END_TEST(test_manager_duplicate_open_fails());
    TEST("Manager tick expires sessions")            END_TEST(test_manager_tick_expires_sessions());
    TEST("Manager serialize all")                    END_TEST(test_manager_serialize_all());
    TEST("SessionOpenMsg roundtrip")                 END_TEST(test_session_open_msg_roundtrip());
    TEST("SessionCloseMsg roundtrip")                END_TEST(test_session_close_msg_roundtrip());

    printf("\n");
    if (failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("%d TEST(S) FAILED\n", failures);
        return 1;
    }
}
