#include <transport/transport.hpp>
#include <transport/framing.hpp>
#include <transport/tcp_transport.hpp>
#include <cstdio>
#include <cstring>
#include <thread>

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
// Tests
// ==========================================================================

static bool test_endpoint_to_string() {
    Endpoint ep;
    ep.scheme = "tcp";
    ep.host = "127.0.0.1";
    ep.port = 7777;
    ASSERT_STREQ(ep.to_string(), "tcp://127.0.0.1:7777");

    Endpoint ep2;
    ep2.scheme = "unix";
    ep2.path = "/tmp/smo.sock";
    ASSERT_STREQ(ep2.to_string(), "unix:///tmp/smo.sock");

    return true;
}

static bool test_endpoint_from_string() {
    auto ep = Endpoint::from_string("tcp://127.0.0.1:7777");
    ASSERT(ep);
    ASSERT_STREQ(ep.value().scheme, "tcp");
    ASSERT_STREQ(ep.value().host, "127.0.0.1");
    ASSERT_EQ(ep.value().port, 7777);

    auto ep2 = Endpoint::from_string("tcp://192.168.1.1:9000");
    ASSERT(ep2);
    ASSERT_STREQ(ep2.value().scheme, "tcp");
    ASSERT_STREQ(ep2.value().host, "192.168.1.1");
    ASSERT_EQ(ep2.value().port, 9000);

    return true;
}

static bool test_endpoint_from_string_unix() {
    auto ep = Endpoint::from_string("unix:///var/run/smo.sock");
    ASSERT(ep);
    ASSERT_STREQ(ep.value().scheme, "unix");
    ASSERT_STREQ(ep.value().path, "/var/run/smo.sock");
    return true;
}

static bool test_endpoint_from_string_host_only() {
    auto ep = Endpoint::from_string("127.0.0.1");
    ASSERT(ep);
    ASSERT(ep.value().scheme.empty());
    ASSERT_STREQ(ep.value().host, "127.0.0.1");
    ASSERT_EQ(ep.value().port, 0);
    return true;
}

static bool test_frame_write_read() {
    Bytes payload = {0x01, 0x02, 0x03, 0x04};
    Bytes framed;
    frame_write(payload, kFrameFlagNone, framed);

    ASSERT_EQ(framed.size(), sizeof(FrameHeader) + payload.size());

    BytesView buf(framed);
    FrameHeader hdr;
    BytesView out_payload;
    size_t n = frame_read(buf, hdr, out_payload);

    ASSERT_EQ(n, framed.size());
    ASSERT_EQ(hdr.magic, 0x534D4F01);
    ASSERT_EQ(hdr.payload_len, payload.size());
    ASSERT_EQ(hdr.flags, kFrameFlagNone);
    ASSERT_EQ(out_payload.size(), payload.size());
    ASSERT_EQ(out_payload[0], 0x01);
    ASSERT_EQ(out_payload[3], 0x04);

    return true;
}

static bool test_frame_read_incomplete() {
    Bytes buf(4, 0);
    FrameHeader hdr;
    BytesView payload;
    size_t n = frame_read(buf, hdr, payload);
    ASSERT_EQ(n, 0U);
    return true;
}

static bool test_frame_write_close() {
    Bytes framed;
    frame_write(BytesView{}, kFrameFlagClose, framed);

    ASSERT_EQ(framed.size(), sizeof(FrameHeader));

    FrameHeader hdr;
    BytesView payload;
    size_t n = frame_read(framed, hdr, payload);
    ASSERT_EQ(n, sizeof(FrameHeader));
    ASSERT_EQ(hdr.flags, kFrameFlagClose);
    ASSERT_EQ(hdr.payload_len, 0U);

    return true;
}

static bool test_registry_register_and_get() {
    TransportRegistry reg;
    auto transport = std::make_unique<TcpTransport>();
    reg.register_transport(std::move(transport), "tcp");

    auto* got = reg.get("tcp");
    ASSERT(got != nullptr);
    ASSERT_STREQ(got->name(), "TCP");

    auto* missing = reg.get("udp");
    ASSERT(missing == nullptr);

    return true;
}

static bool test_registry_singleton() {
    auto& inst1 = TransportRegistry::instance();
    auto& inst2 = TransportRegistry::instance();
    ASSERT(&inst1 == &inst2);
    return true;
}

// ── Loopback TCP test ──────────────────────────────────────────────────

static bool test_tcp_connect_and_send() {
    bool ok = true;

    TcpTransport transport;
    Endpoint listen_ep;
    listen_ep.scheme = "tcp";
    listen_ep.host = "127.0.0.1";
    listen_ep.port = 0;

    auto listener = transport.listen(listen_ep);
    if (!listener) return false;

    auto local = listener.value()->local_endpoint();

    std::thread acceptor([&listener, &ok]() {
        auto session = listener.value()->accept();
        if (!session) { ok = false; return; }
        auto data = session.value()->recv(4096);
        if (!data) { ok = false; return; }
        if (data.value().size() != 5 || data.value()[0] != 0x48) { ok = false; return; }
        session.value()->close();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    Endpoint remote;
    remote.scheme = "tcp";
    remote.host = "127.0.0.1";
    remote.port = local.port;

    auto session = transport.connect(remote);
    if (!session) {
        listener.value()->close();
        acceptor.join();
        return false;
    }

    Bytes data = {'H', 'e', 'l', 'l', 'o'};
    if (!session.value()->send(data)) {
        listener.value()->close();
        acceptor.join();
        return false;
    }

    acceptor.join();
    listener.value()->close();
    session.value()->close();
    return ok;
}

static bool test_tcp_connect_refused() {
    TcpTransport transport;

    Endpoint remote;
    remote.scheme = "tcp";
    remote.host = "127.0.0.1";
    remote.port = 19999;

    auto session = transport.connect(remote);
    return !session;
}

// ==========================================================================
// Main
// ==========================================================================

int main(int, char*[]) {
    printf("SMO Transport — Unit Tests\n");
    printf("===========================\n\n");

    TEST("Endpoint to_string")                    END_TEST(test_endpoint_to_string());
    TEST("Endpoint from_string")                  END_TEST(test_endpoint_from_string());
    TEST("Endpoint from_string unix")             END_TEST(test_endpoint_from_string_unix());
    TEST("Endpoint from_string host only")        END_TEST(test_endpoint_from_string_host_only());
    TEST("Frame write/read")                      END_TEST(test_frame_write_read());
    TEST("Frame read incomplete")                 END_TEST(test_frame_read_incomplete());
    TEST("Frame write close")                     END_TEST(test_frame_write_close());
    TEST("Registry register and get")             END_TEST(test_registry_register_and_get());
    TEST("Registry singleton")                    END_TEST(test_registry_singleton());
    TEST("TCP connect and send")                  END_TEST(test_tcp_connect_and_send());
    TEST("TCP connect refused")                   END_TEST(test_tcp_connect_refused());

    printf("\n");
    if (failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("%d TEST(S) FAILED\n", failures);
        return 1;
    }
}
