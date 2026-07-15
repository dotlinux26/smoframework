#include <packet/packet.h>
#include <signing/signing.h>
#include <encryption/encryption.h>
#include <replay/replay.h>
#include <schema/schema.h>

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
// Tests — Packet
// ==========================================================================
static bool test_packet_to_buffer_roundtrip() {
    Packet pkt;
    pkt.header.version = 1;
    pkt.header.flags = 0x42;
    pkt.header.payload_len = 0;
    pkt.session_id.fill(0xAA);
    pkt.intent_id.fill(0xBB);
    pkt.opcode_id = 0x01020304;
    pkt.timestamp = 1234567890;
    pkt.nonce.fill(0xCC);
    pkt.payload = {0x01, 0x02, 0x03};
    pkt.signature.fill(0xDD);

    std::vector<uint8_t> buf;
    auto res = packet_to_buffer(pkt, buf);
    ASSERT(res);
    ASSERT(buf.size() > 100);

    auto parsed = packet_from_buffer(buf);
    ASSERT(parsed);
    ASSERT_EQ(parsed.value().header.version, 1);
    ASSERT_EQ(parsed.value().header.flags, 0x42);
    ASSERT_EQ(parsed.value().session_id[0], 0xAA);
    ASSERT_EQ(parsed.value().intent_id[0], 0xBB);
    ASSERT_EQ(parsed.value().opcode_id, 0x01020304);
    ASSERT_EQ(parsed.value().timestamp, 1234567890);
    ASSERT_EQ(parsed.value().nonce[0], 0xCC);
    ASSERT_EQ(parsed.value().payload.size(), 3U);
    ASSERT_EQ(parsed.value().payload[0], 0x01);
    ASSERT_EQ(parsed.value().payload[2], 0x03);
    ASSERT_EQ(parsed.value().signature[0], 0xDD);

    return true;
}

static bool test_packet_from_buffer_too_short() {
    std::vector<uint8_t> buf(10, 0);
    auto res = packet_from_buffer(buf);
    ASSERT(!res);

    return true;
}

static bool test_packet_from_buffer_bad_version() {
    Packet pkt;
    pkt.header.version = 99;

    std::vector<uint8_t> buf;
    auto res = packet_to_buffer(pkt, buf);
    ASSERT(!res);  // serialize rejects bad version too

    // Manually make a buffer with wrong version
    buf.assign(200, 0);
    buf[0] = 99;  // version
    auto res2 = packet_from_buffer(buf);
    ASSERT(!res2);

    return true;
}

static bool test_packet_empty_payload() {
    Packet pkt;
    pkt.header.version = 1;

    std::vector<uint8_t> buf;
    auto res = packet_to_buffer(pkt, buf);
    ASSERT(res);

    auto parsed = packet_from_buffer(buf);
    ASSERT(parsed);
    ASSERT(parsed.value().payload.empty());

    return true;
}

static bool test_packet_payload_length_mismatch() {
    Packet pkt;
    pkt.header.version = 1;
    pkt.payload = {0x01, 0x02};

    std::vector<uint8_t> buf;
    auto res = packet_to_buffer(pkt, buf);
    ASSERT(res);

    // Payload length starts at byte 58 (6 header + 16 session + 16 intent
    // + 4 opcode + 8 timestamp + 8 nonce)
    buf[58] = 0xFF;
    buf[59] = 0xFF;
    buf[60] = 0xFF;
    buf[61] = 0xFF;

    auto parsed = packet_from_buffer(buf);
    ASSERT(!parsed);

    return true;
}

// ==========================================================================
// Tests — Schema
// ==========================================================================
static bool test_schema_message_type_to_string() {
    ASSERT(std::strcmp(to_string(MessageType::CONTRACT_PROPOSAL), "CONTRACT_PROPOSAL") == 0);
    ASSERT(std::strcmp(to_string(MessageType::HEARTBEAT), "HEARTBEAT") == 0);
    ASSERT(std::strcmp(to_string(MessageType::SESSION_OPEN), "SESSION_OPEN") == 0);
    ASSERT(std::strcmp(to_string(static_cast<MessageType>(0xFF)), "UNKNOWN") == 0);

    return true;
}

static bool test_schema_message_classification() {
    ASSERT(is_control_message(MessageType::HEARTBEAT));
    ASSERT(is_control_message(MessageType::SESSION_OPEN));
    ASSERT(is_control_message(MessageType::SESSION_CLOSE));
    ASSERT(!is_control_message(MessageType::CONTRACT_PROPOSAL));

    ASSERT(is_contract_message(MessageType::CONTRACT_PROPOSAL));
    ASSERT(is_contract_message(MessageType::CONTRACT_RESULT));
    ASSERT(!is_contract_message(MessageType::WITNESS_REQUEST));

    ASSERT(is_witness_message(MessageType::WITNESS_REQUEST));
    ASSERT(is_witness_message(MessageType::WITNESS_RESPONSE));
    ASSERT(!is_witness_message(MessageType::HEARTBEAT));

    return true;
}

static bool test_schema_protocol_version() {
    auto v = current_protocol_version();
    ASSERT_EQ(v.major, 1);
    ASSERT_EQ(v.minor, 0);

    ASSERT(is_compatible({1, 5}, v));   // same major
    ASSERT(!is_compatible({2, 0}, v));  // different major

    return true;
}

// ==========================================================================
// Tests — ReplayProtector
// ==========================================================================
static bool test_replay_accept_fresh() {
    ReplayProtector rp({5000, 100});

    std::array<uint8_t, 8> nonce = {0, 0, 0, 0, 0, 0, 0, 1};
    ASSERT(rp.accept(nonce, 1000, 1500));  // delta 500ms < 5s
    ASSERT_EQ(rp.size(), 1U);

    return true;
}

static bool test_replay_reject_duplicate() {
    ReplayProtector rp({5000, 100});

    std::array<uint8_t, 8> nonce = {0, 0, 0, 0, 0, 0, 0, 1};
    ASSERT(rp.accept(nonce, 1000, 1500));
    ASSERT(!rp.accept(nonce, 1000, 1500));  // same nonce → reject

    return true;
}

static bool test_replay_reject_outside_window() {
    ReplayProtector rp({5000, 100});

    std::array<uint8_t, 8> nonce = {0, 0, 0, 0, 0, 0, 0, 1};
    ASSERT(!rp.accept(nonce, 1000, 20000));  // delta 19s > 5s

    return true;
}

static bool test_replay_eviction() {
    ReplayProtector rp({50000, 3});  // capacity 3

    for (uint8_t i = 0; i < 5; ++i) {
        std::array<uint8_t, 8> nonce = {0, 0, 0, 0, 0, 0, 0, i};
        ASSERT(rp.accept(nonce, 1000, 1500));
    }

    ASSERT_EQ(rp.size(), 3U);  // evicted 2

    // First nonce (0) should be evicted and re-usable
    std::array<uint8_t, 8> old = {0, 0, 0, 0, 0, 0, 0, 0};
    ASSERT(rp.accept(old, 1000, 1500));  // was evicted, so accepted again

    return true;
}

static bool test_replay_clear() {
    ReplayProtector rp({5000, 100});

    std::array<uint8_t, 8> nonce = {0, 0, 0, 0, 0, 0, 0, 1};
    ASSERT(rp.accept(nonce, 1000, 1500));
    ASSERT_EQ(rp.size(), 1U);

    rp.clear();
    ASSERT_EQ(rp.size(), 0U);
    ASSERT(rp.accept(nonce, 1000, 1500));  // accepted again after clear

    return true;
}

// ==========================================================================
// Main
// ==========================================================================

int main(int, char*[]) {
    printf("SMO Protocol — Unit Tests\n");
    printf("==========================\n\n");

    TEST("Packet roundtrip")                                END_TEST(test_packet_to_buffer_roundtrip());
    TEST("Packet from_buffer too short")                    END_TEST(test_packet_from_buffer_too_short());
    TEST("Packet bad version")                              END_TEST(test_packet_from_buffer_bad_version());
    TEST("Packet empty payload")                            END_TEST(test_packet_empty_payload());
    TEST("Packet payload length mismatch")                  END_TEST(test_packet_payload_length_mismatch());
    TEST("Schema MessageType to_string")                    END_TEST(test_schema_message_type_to_string());
    TEST("Schema message classification")                   END_TEST(test_schema_message_classification());
    TEST("Schema protocol version")                         END_TEST(test_schema_protocol_version());
    TEST("Replay accept fresh")                             END_TEST(test_replay_accept_fresh());
    TEST("Replay reject duplicate")                         END_TEST(test_replay_reject_duplicate());
    TEST("Reject outside time window")                      END_TEST(test_replay_reject_outside_window());
    TEST("Replay eviction")                                 END_TEST(test_replay_eviction());
    TEST("Replay clear")                                    END_TEST(test_replay_clear());

    printf("\n");
    if (failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("%d TEST(S) FAILED\n", failures);
        return 1;
    }
}
