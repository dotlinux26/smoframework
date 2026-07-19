#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <vector>
#include "core/discovery/discovery.hpp"
#include "core/errors/error.hpp"
#include "core/identity/identity.hpp"
#include "core/recovery/crl.hpp"
#include "runtime/event_bus.hpp"

namespace smo::network::sync {
    class MembershipSync;
}

namespace smo {

class GossipEngine {
public:
    static constexpr uint32_t kGossipOpcode = 0x010006;  // GOSSIP_SYNC in DISCOVERY namespace

    struct Config {
        uint32_t interval_ms = 5000;
        uint32_t fanout = 3;
        uint32_t max_payload = 65536;
    };

    explicit GossipEngine(MembershipTable& table, const Config& cfg);

    void set_crl(recovery::CRL* crl) { crl_ = crl; }

    // Set MembershipSync for rich event-based gossip payloads
    void set_membership_sync(network::sync::MembershipSync* ms) { membership_sync_ = ms; }

    static Config default_config();

    ~GossipEngine();

    GossipEngine(const GossipEngine&) = delete;
    GossipEngine& operator=(const GossipEngine&) = delete;

    void start();
    void stop();

    // Periodic tick — fanout gossip to random peers
    void tick(int64_t now_ns);

    // EventBus listener for RecoveryApproved events — gossips CRL updates
    void on_recovery_approved(const runtime::Event& ev);

    // Get pending events since a sequence number (delta sync)
    Bytes pending_updates(uint64_t since_sequence) const;

    // Apply incoming gossip data (serialized MembershipEvents)
    Result<void> apply_gossip(BytesView data);

    // Handle an incoming gossip message from a peer
    static Result<void> handle_gossip_message(BytesView payload, GossipEngine& engine);

    // Get current sequence/incarnation
    uint64_t current_sequence() const noexcept { return incarnation_; }

    void set_gossip_interval(int64_t ns) { gossip_interval_ns_ = ns; }

private:
    void send_gossip_to_peer(const Endpoint& target);
    std::vector<Endpoint> select_fanout_peers();

    MembershipTable& table_;
    network::sync::MembershipSync* membership_sync_ = nullptr;
    Config config_;
    std::mt19937_64 rng_;
    uint64_t incarnation_ = 1;
    uint64_t local_sequence_ = 0;
    int64_t gossip_interval_ns_{5'000'000'000};
    int64_t last_gossip_{0};
    std::atomic<bool> running_{false};

    recovery::CRL* crl_ = nullptr;

    // TCP connect helper
    Result<int> tcp_connect_to(const Endpoint& ep) const;
    static Result<bool> tcp_send(int fd, BytesView data);
};

} // namespace smo