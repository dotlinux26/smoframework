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
#include <core/transport/udp_transport.hpp>
#include <core/select/selector.hpp>
#include <core/network/udp/heartbeat_service.hpp>
#include <core/discovery/gossip.hpp>
#include <core/network/sync/membership_sync.hpp>
#include <core/network/transport/address_resolver.hpp>

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
                 [--seed <host:port>]

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
    smo::GossipEngine gossip_engine(membership);
    smo::network::sync::MembershipSync membership_sync(membership, health_monitor);

    // Register transports
    smo::TransportRegistry::instance().register_transport(
        std::make_unique<smo::TcpTransport>(), "tcp");
    smo::TransportRegistry::instance().register_transport(
        std::make_unique<smo::network::udp::UdpTransport>(), "udp");

    // Initialize PeerStore and sync with MembershipTable
    smo::PeerStore peer_store;
    if (auto r = peer_store.open(data_dir); !r) {
        std::fprintf(stderr, "[smo-node] Failed to open PeerStore: %s\n",
                     r.error().message.c_str());
    } else {
        peer_store.sync_to_membership(membership);
    }

    // Initialize AddressResolver
    smo::network::transport::AddressResolver address_resolver;

    // Register UDP transport and start UDP listener
    smo::Endpoint udp_listen_ep;
    udp_listen_ep.scheme = "udp";
    udp_listen_ep.host = "0.0.0.0";
    udp_listen_ep.port = static_cast<uint16_t>(port);

    auto udp_transport = std::make_unique<smo::network::udp::UdpTransport>();
    auto udp_listen_result = udp_transport->listen(udp_listen_ep);
    if (!udp_listen_result) {
        std::fprintf(stderr, "[smo-node] Failed to listen UDP: %s\n",
                     udp_listen_result.error().message.c_str());
    } else {
        std::printf("[smo-node] Listening on udp://0.0.0.0:%d\n", port);
    }

    // Initialize HeartbeatService
    smo::network::udp::HeartbeatService::Config hb_config;
    hb_config.ping_interval_ms = 5000;
    hb_config.ping_timeout_ms = 3000;
    hb_config.max_misses = 3;
    hb_config.local_port = port; // same port for UDP

    smo::network::udp::HeartbeatService heartbeat_service(hb_config);
    auto hb_start = heartbeat_service.start(*static_cast<smo::network::udp::UdpTransport*>(udp_transport.get()),
                                            membership, health_monitor);
    if (!hb_start) {
        std::fprintf(stderr, "[smo-node] Failed to start heartbeat: %s\n",
                     hb_start.error().message.c_str());
    } else {
        std::printf("[smo-node] Heartbeat service started (interval=%ums, timeout=%ums, max_misses=%u)\n",
                    hb_config.ping_interval_ms, hb_config.ping_timeout_ms, hb_config.max_misses);
    }

    // Create TCP listening endpoint
    smo::Endpoint listen_ep;
    listen_ep.scheme = "tcp";
    listen_ep.host = "0.0.0.0";
    listen_ep.port = static_cast<uint16_t>(port);

    auto listen_result = smo::TransportRegistry::instance().get("tcp")->listen(listen_ep);
    if (!listen_result) {
        std::fprintf(stderr, "[smo-node] Failed to listen TCP: %s\n",
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
                        std::printf("[smo-node] Received peer table (%zu bytes)\n",
                                    recv.value().size());
                        // TODO: Parse NodeInfoMsg list and populate membership
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

    // Subscribe to membership events for gossip
    membership_sync.subscribe([&](const smo::network::sync::MembershipEvent& ev) {
        // Forward to GossipEngine for propagation
        // For now just log
        std::printf("[smo-node] Membership event: type=%d node=%s\n",
                    static_cast<int>(ev.type), ev.node_id.to_string().c_str());
    });

    // ── Main loop ──────────────────────────────────────────────
    std::printf("[smo-node] Entering main loop...\n");

    int64_t last_tick = 0;
    int64_t last_gossip = 0;
    int64_t last_peerstore_sync = 0;

    while (g_running) {
        int64_t now_ns = static_cast<int64_t>(
            std::chrono::system_clock::now().time_since_epoch().count());

        // Periodic ticks
        if (now_ns - last_tick > 5000000000LL) { // 5s
            discovery_engine.tick(now_ns);
            heartbeat_service.tick(now_ns);
            last_tick = now_ns;
        }

        if (now_ns - last_gossip > 5000000000LL) { // 5s
            gossip_engine.tick(now_ns);
            // Propagate membership changes via gossip
            auto events = membership_sync.pending_events(gossip_engine.current_sequence());
            if (!events.empty()) {
                // TODO: Send gossip messages to fanout peers
            }
            last_gossip = now_ns;
        }

        // Periodic PeerStore sync
        if (now_ns - last_peerstore_sync > 30000000000LL) { // 30s
            peer_store.sync_from_membership(membership);
            last_peerstore_sync = now_ns;
        }

        // Accept TCP connections
        auto tcp_session = lstnr->accept();
        if (tcp_session) {
            std::printf("[smo-node] Accepted TCP connection from %s\n",
                        tcp_session.value()->remote_endpoint().to_string().c_str());

            auto data = tcp_session.value()->recv(4096);
            if (data) {
                // Parse discovery message and dispatch
                // For now: echo back
                std::printf("[smo-node] Received %zu bytes from %s\n",
                            data.value().size(),
                            tcp_session.value()->remote_endpoint().to_string().c_str());
                tcp_session.value()->send(data.value());
            }
            tcp_session.value()->close();
        }

        // Accept UDP packets
        // Note: UDP listener is in heartbeat_service's listener_
        // For now, we poll the UDP socket manually
        // TODO: Integrate UDP listener into main loop properly

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Cleanup
    heartbeat_service.stop();
    lstnr->close();
    peer_store.sync_from_membership(membership);
    peer_store.close();

    std::printf("[smo-node] Shutdown complete.\n");
    return 0;
}