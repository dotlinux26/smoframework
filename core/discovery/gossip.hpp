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

namespace smo {

class GossipEngine {
public:
    struct GossipMessage {
        NodeID      node_id;
        PeerState   state;
        int64_t     last_seen;
        uint32_t    incarnation;
    };

    struct Config {
        uint32_t interval_ms = 5000;
        uint32_t fanout = 3;
        uint32_t max_payload = 4096;
    };

    explicit GossipEngine(MembershipTable& table, const Config& cfg = default_config());

    static Config default_config();
    GossipEngine(MembershipTable& table);

    ~GossipEngine();

    GossipEngine(const GossipEngine&) = delete;
    GossipEngine& operator=(const GossipEngine&) = delete;

    void start();
    void stop();

    void tick(int64_t now_ns);

    // Get updates since a given sequence number (for delta sync)
    std::vector<GossipMessage> pending_updates(uint64_t since_sequence) const;

    // Apply incoming gossip updates
    void apply_updates(const std::vector<GossipMessage>& updates);

    // Get current sequence/incarnation
    uint64_t current_sequence() const noexcept { return incarnation_; }

    void set_gossip_interval(int64_t ns) { gossip_interval_ns_ = ns; }

private:
    void send_gossip_to_peer(const Endpoint& target);

    std::vector<Endpoint> select_fanout_peers();

    MembershipTable& table_;
    Config config_;
    std::mt19937_64 rng_;
    uint64_t incarnation_ = 1;
    uint64_t local_sequence_ = 0;
    int64_t gossip_interval_ns_{5'000'000'000};
    int64_t last_gossip_{0};
    std::atomic<bool> running_{false};
};

} // namespace smo