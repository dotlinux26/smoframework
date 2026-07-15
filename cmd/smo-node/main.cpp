// §XIX — CLI Design
// smo-node: actual node daemon.
//
// Responsibilities:
// - receive intents
// - run FSM
// - manage sessions
// - capability enforcement
// - execute DAG tasks

#include <core/crypto/impl.hpp>
#include <core/discovery/discovery.hpp>
#include <core/errors/error.hpp>
#include <core/identity/identity.hpp>
#include <core/transport/transport.hpp>
#include <core/transport/tcp_transport.hpp>
#include <core/select/selector.hpp>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

// Global flag for graceful shutdown
static volatile bool g_running = true;
extern "C" void handle_signal(int) { g_running = false; }

static void print_usage(const char* prog) {
    std::fprintf(stderr, R"(SMO Node Daemon

Usage:
  %s --daemon --port <port> --data <data-dir> [--name <name>]

Options:
  --daemon          Run as daemon
  --port <port>     Listen port (default: 7777)
  --data <dir>      Data directory (default: /var/lib/smo)
  --name <name>     Display name
  --seed <addr>     Seed node address (e.g. node-a:7777)
  --help            Show this help
)",
        prog);
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    // Parse args
    bool daemon_mode = false;
    int port = 7777;
    std::string data_dir = "/var/lib/smo";
    std::string node_name;
    std::string seed_addr;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--daemon") daemon_mode = true;
        else if (arg == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (arg == "--data" && i + 1 < argc) data_dir = argv[++i];
        else if (arg == "--name" && i + 1 < argc) node_name = argv[++i];
        else if (arg == "--seed" && i + 1 < argc) seed_addr = argv[++i];
        else if (arg == "--help") { print_usage(argv[0]); return 0; }
    }

    if (!daemon_mode) {
        // Without --daemon, just init and print info
        std::printf("SMO Node\n");
        std::printf("  Data dir: %s\n", data_dir.c_str());
        std::printf("  Port:     %d\n", port);
        if (!node_name.empty()) std::printf("  Name:     %s\n", node_name.c_str());
        if (!seed_addr.empty()) std::printf("  Seed:     %s\n", seed_addr.c_str());
        std::printf("  Mode:     standalone (use --daemon to run as service)\n");
        return 0;
    }

    // ── Initialize ────────────────────────────────────────────
    std::printf("[smo-node] Starting daemon...\n");
    std::printf("[smo-node] Data: %s, Port: %d\n", data_dir.c_str(), port);
    if (!node_name.empty()) std::printf("[smo-node] Name: %s\n", node_name.c_str());
    if (!seed_addr.empty()) std::printf("[smo-node] Seed: %s\n", seed_addr.c_str());

    // Register TCP transport
    smo::TransportRegistry::instance().register_transport(
        std::make_unique<smo::TcpTransport>(), "tcp");

    // Create listening endpoint
    smo::Endpoint listen_ep;
    listen_ep.scheme = "tcp";
    listen_ep.host = "0.0.0.0";
    listen_ep.port = static_cast<uint16_t>(port);

    auto listener = smo::TransportRegistry::instance().connect(listen_ep);
    if (!listener) {
        std::fprintf(stderr, "[smo-node] Failed to create listener: %s\n",
                     listener.error().message.c_str());
        return 1;
    }

    // Wait using listener
    auto listen_result = smo::TransportRegistry::instance().get("tcp")->listen(listen_ep);
    if (!listen_result) {
        std::fprintf(stderr, "[smo-node] Failed to listen: %s\n",
                     listen_result.error().message.c_str());
        return 1;
    }
    auto& lstnr = listen_result.value();

    std::printf("[smo-node] Listening on tcp://0.0.0.0:%d\n", port);

    // If seed provided, connect and send HELLO
    if (!seed_addr.empty()) {
        smo::Endpoint seed_ep;
        auto ep_result = smo::Endpoint::from_string(seed_addr);
        if (ep_result) {
            seed_ep = ep_result.value();
            std::printf("[smo-node] Connecting to seed: %s\n", seed_addr.c_str());
            auto session = smo::TransportRegistry::instance().connect(seed_ep);
            if (session) {
                smo::HelloMsg hello;
                hello.protocol_version = 1;
                auto hello_data = hello.serialize();
                session.value()->send(hello_data);

                auto recv_data = session.value()->recv(4096);
                if (recv_data) {
                    std::printf("[smo-node] Received %zu bytes from seed\n",
                                recv_data.value().size());
                }
            } else {
                std::printf("[smo-node] Seed connection failed (ok for first node)\n");
            }
        }
    }

    // ── Main loop ──────────────────────────────────────────────
    std::printf("[smo-node] Entering main loop...\n");

    while (g_running) {
        // Accept connections with timeout via non-blocking poll
        auto session = lstnr->accept();
        if (!session) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        std::printf("[smo-node] Accepted connection from %s\n",
                    session.value()->remote_endpoint().to_string().c_str());

        auto data = session.value()->recv(4096);
        if (data) {
            std::printf("[smo-node] Received %zu bytes\n", data.value().size());
        }

        session.value()->close();
    }

    lstnr->close();
    std::printf("[smo-node] Shutdown complete.\n");
    return 0;
}
