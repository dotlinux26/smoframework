// Minimal debug test to find where the hang is
#include <transport/transport.h>
#include <transport/tcp/tcp_transport.h>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace smo::hl;
using smo::Packet;

int main() {
    printf("Step 1: creating server...\n"); fflush(stdout);
    TcpTransport server;

    Endpoint ep;
    ep.address = "127.0.0.1";
    ep.port = 18992;

    bool got_packet = false;
    auto err = server.listen(ep,
        [&](Packet&&, Endpoint) { got_packet = true; printf("  got packet callback\n"); fflush(stdout); },
        [&](std::error_code ec, Endpoint) { printf("  error: %d\n", ec.value()); fflush(stdout); });
    if (err) {
        printf("listen failed: %d\n", err.value());
        return 1;
    }
    printf("Step 2: listen OK\n"); fflush(stdout);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    printf("Step 3: creating client...\n"); fflush(stdout);
    TcpTransport client;
    auto cerr = client.connect(ep);
    if (cerr) {
        printf("connect failed: %d\n", cerr.value());
        server.close();
        return 1;
    }
    printf("Step 4: connect OK\n"); fflush(stdout);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    printf("Step 5: sending packet...\n"); fflush(stdout);
    Packet sent;
    sent.header.version = 1;
    sent.session_id.fill(0xAA);
    sent.intent_id.fill(0xBB);
    sent.opcode_id = 1;
    sent.timestamp = 1000;
    sent.nonce = {1,2,3,4,5,6,7,8};
    sent.signature.fill(0xCC);

    auto serr = client.send(std::move(sent), ep);
    if (serr) {
        printf("send failed: %d\n", serr.value());
        server.close();
        client.close();
        return 1;
    }
    printf("Step 6: send OK\n"); fflush(stdout);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    printf("Step 7: checking result... got_packet=%d\n", got_packet); fflush(stdout);

    printf("Step 8: closing...\n"); fflush(stdout);
    client.close();
    server.close();
    printf("Step 9: close OK\n"); fflush(stdout);

    return got_packet ? 0 : 1;
}
