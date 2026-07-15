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
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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

    // ===========================================================================
// Helpers
// ===========================================================================

static smo::NodeID load_local_node_id(const std::string& data_dir) {
    smo::NodeID id;
    std::string id_path = data_dir + "/identity.json";
    std::ifstream f(id_path);
    if (!f) return id;

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    f.close();

    // Find "node_id": "hex..."
    auto pos = content.find("\"node_id\"");
    if (pos == std::string::npos) return id;
    auto start = content.find('"', pos + 9);
    auto end = content.find('"', start + 1);
    if (start == std::string::npos || end == std::string::npos) return id;

    std::string hex = content.substr(start + 1, end - start - 1);
    if (hex.size() != 64) return id;

    for (size_t i = 0; i < 32; ++i) {
        id.value[i] = static_cast<uint8_t>(std::stoi(hex.substr(i * 2, 2), nullptr, 16));
    }
    return id;
}

static void node_id_to_hex(const smo::NodeID& id, std::string& out) {
    std::ostringstream oss;
    for (uint8_t b : id.value) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    }
    out = oss.str();
}

// ===========================================================================
// Main
// ===========================================================================

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

    // Load local NodeID
    smo::NodeID local_id = load_local_node_id(data_dir);
    std::string local_id_hex;
    node_id_to_hex(local_id, local_id_hex);
    std::printf("[smo-node] Local NodeID: %s\n", local_id_hex.c_str());

    // Initialize core components
    smo::MembershipTable membership;
    smo::HealthMonitor health_monitor;
    smo::DiscoveryEngine discovery_engine(membership, health_monitor);

    // Register TCP transport
    smo::TransportRegistry::instance().register_transport(
        std::make_unique<smo::TcpTransport>(), "tcp");

    // Create listening endpoint
    smo::Endpoint listen_ep;
    listen_ep.scheme = "tcp";
    listen_ep.host = "0.0.0.0";
    listen_ep.port = static_cast<uint16_t>(port);

    auto listen_result = smo::TransportRegistry::instance().get("tcp")->listen(listen_ep);
    if (!listen_result) {
        std::fprintf(stderr, "[smo-node] Failed to listen: %s\n",
                     listen_result.error().message.c_str());
        return 1;
    }
    auto& lstnr = listen_result.value();

    std::printf("[smo-node] Listening on tcp://0.0.0.0:%d\n", port);

    // ── Bootstrap: connect to seed ────────────────────────────
    if (!seed_addr.empty()) {
        std::printf("[smo-node] Connecting to seed: %s\n", seed_addr.c_str());

        smo::Endpoint seed_ep;
        auto ep_result = smo::Endpoint::from_string(seed_addr);
        if (!ep_result) {
            std::fprintf(stderr, "[smo-node] Invalid seed address: %s\n", seed_addr.c_str());
        } else {
            seed_ep = ep_result.value();

            // Register transport if not already
            smo::TransportRegistry::instance().register_transport(
                std::make_unique<smo::TcpTransport>(), "tcp");

            auto now = std::chrono::system_clock::to_time_t(
                std::chrono::system_clock::now());

            auto rec_result = smo::Bootstrap::find_seed(
                {seed_ep},
                smo::TransportRegistry::instance(),
                local_id,
                static_cast<int64_t>(now) * 1000000000LL);

            if (rec_result) {
                auto& rec = rec_result.value();
                std::printf("[smo-node] Seed responded: %s (%s)\n",
                            rec.display_name.c_str(),
                            rec.endpoint.to_string().c_str());

                // Process WELCOME through DiscoveryEngine
                auto now_ns = static_cast<int64_t>(now) * 1000000000LL;
                discovery_engine.handle_welcome(
                    smo::WelcomeMsg{local_id, rec}, now_ns);

                // Request full peer table
                smo::DiscoverMsg discover;
                auto disc_data = discover.serialize();
                auto session = smo::TransportRegistry::instance().connect(seed_ep);
                if (session) {
                    session.value()->send(disc_data);
                    auto recv = session.value()->recv(8192);
                    if (recv) {
                        // Parse peer table response (NodeInfoMsg list)
                        // For now just log
                        std::printf("[smo-node] Received peer table (%zu bytes)\n",
                                    recv.value().size());
                    }
                }

                std::printf("[smo-node] Bootstrap complete. Peers: %zu\n",
                            membership.count());
            } else {
                std::printf("[smo-node] Seed connection failed: %s\n",
                            rec_result.error().message.c_str());
                std::printf("[smo-node] Continuing as first node in mesh\n");
            }
        }
    }

    // ── Main loop ──────────────────────────────────────────────
    std::printf("[smo-node] Entering main loop...\n");

    int64_t last_tick = 0;
    while (g_running) {
        int64_t now_ns = static_cast<int64_t>(
            std::chrono::system_clock::now().time_since_epoch().count());

        // Periodic discovery engine tick (every 5s)
        if (now_ns - last_tick > 5000000000LL) {
            discovery_engine.tick(now_ns);
            last_tick = now_ns;
        }

        // Accept connections with timeout
        auto session = lstnr->accept();
        if (!session) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        std::printf("[smo-node] Accepted connection from %s\n",
                    session.value()->remote_endpoint().to_string().c_str());

        // Read message
        auto data = session.value()->recv(4096);
        if (!data) {
            session.value()->close();
            continue;
        }

        // Try to parse as discovery message
        // In real impl: parse header, dispatch to DiscoveryEngine
        // For now: log and respond with basic Hello if it's a HELLO
        std::printf("[smo-node] Received %zu bytes from %s\n",
                    data.value().size(),
                    session.value()->remote_endpoint().to_string().c_str());

        // Echo back for testing
        session.value()->send(data.value());
        session.value()->close();
    }

    lstnr->close();
    std::printf("[smo-node] Shutdown complete.\n");
    return 0;
}
