#include <transport/transport.h>
#include <transport/tcp/tcp_transport.h>
#include <transport/framing/framing.h>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace smo::hl;
using smo::Packet;

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

// ── Framing tests ─────────────────────────────────────────────────────

static bool test_hl_frame_write_read() {
    std::vector<uint8_t> payload = {0x01, 0x02, 0x03, 0x04};
    std::vector<uint8_t> framed;
    frame_write(payload, framed);

    ASSERT_EQ(framed.size(), sizeof(FrameHeader) + payload.size());

    std::span<const uint8_t> out_payload;
    size_t n = frame_read(framed, out_payload);

    ASSERT_EQ(n, framed.size());
    ASSERT_EQ(out_payload.size(), payload.size());
    ASSERT_EQ(out_payload[0], 0x01);
    ASSERT_EQ(out_payload[3], 0x04);

    return true;
}

static bool test_hl_frame_read_incomplete() {
    std::vector<uint8_t> buf(4, 0);
    std::span<const uint8_t> payload;
    size_t n = frame_read(buf, payload);
    ASSERT_EQ(n, 0U);
    return true;
}

static bool test_hl_frame_write_empty() {
    std::vector<uint8_t> framed;
    frame_write({}, framed);
    ASSERT_EQ(framed.size(), sizeof(FrameHeader));
    return true;
}

// ── TcpTransport tests ────────────────────────────────────────────────

static bool test_tcp_connect_refused() {
    TcpTransport client;
    Endpoint ep;
    ep.address = "127.0.0.1";
    ep.port = 19999;
    auto err = client.connect(ep);
    return static_cast<bool>(err);
}

static bool test_tcp_listen_close() {
    TcpTransport server;
    Endpoint ep;
    ep.address = "127.0.0.1";
    ep.port = 18790;

    auto err = server.listen(ep,
        [](Packet&&, Endpoint) {},
        [](std::error_code, Endpoint) {});
    if (err) { printf("  listen err=%d\n", err.value()); return false; }

    server.close();
    return true;
}

static bool test_tcp_connect_send_receive() {
    Endpoint ep;
    ep.address = "127.0.0.1";
    ep.port = 18791;

    Packet received;
    bool got_packet = false;
    bool got_error = false;

    TcpTransport server;
    auto err = server.listen(ep,
        [&](Packet&& pkt, Endpoint) {
            received = std::move(pkt);
            got_packet = true;
        },
        [&](std::error_code ec, Endpoint) {
            printf("  server error: %d\n", ec.value());
            got_error = true;
        });
    if (err) { printf("  listen err=%d\n", err.value()); return false; }

    // Wait for server to be ready
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    TcpTransport client;
    auto cerr = client.connect(ep);
    if (cerr) { printf("  connect err=%d\n", cerr.value()); server.close(); return false; }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Create and send a packet
    Packet sent;
    sent.header.version = 1;
    sent.session_id.fill(0xAA);
    sent.intent_id.fill(0xBB);
    sent.opcode_id = 1;
    sent.timestamp = 1000;
    sent.nonce = {1,2,3,4,5,6,7,8};
    sent.signature.fill(0xCC);

    auto serr = client.send(std::move(sent), ep);
    if (serr) { printf("  send err=%d\n", serr.value()); server.close(); client.close(); return false; }

    // Wait for async delivery
    int waited = 0;
    while (!got_packet && !got_error && waited < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        waited++;
    }

    client.close();
    server.close();

    if (!got_packet) { printf("  no packet received (waited %dms)\n", waited * 20); return false; }
    if (got_error) return false;

    ASSERT_EQ(received.header.version, 1);
    ASSERT_EQ(received.session_id[0], 0xAA);
    ASSERT_EQ(received.intent_id[0], 0xBB);
    ASSERT_EQ(received.opcode_id, 1);
    ASSERT_EQ(received.timestamp, 1000);

    return true;
}

// ── Main ──────────────────────────────────────────────────────────────

int main(int, char*[]) {
    printf("SMO Transport High-Level — Unit Tests\n");
    printf("======================================\n\n");

    TEST("Frame write/read")                       END_TEST(test_hl_frame_write_read());
    TEST("Frame read incomplete")                  END_TEST(test_hl_frame_read_incomplete());
    TEST("Frame write empty")                      END_TEST(test_hl_frame_write_empty());
    TEST("TCP connect refused")                    END_TEST(test_tcp_connect_refused());
    TEST("TCP listen close")                       END_TEST(test_tcp_listen_close());
    TEST("TCP connect send receive")               END_TEST(test_tcp_connect_send_receive());

    printf("\n");
    if (failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("%d TEST(S) FAILED\n", failures);
        return 1;
    }
}
