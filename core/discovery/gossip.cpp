#include "core/discovery/gossip.hpp"

#include <algorithm>
#include <chrono>

namespace smo {

GossipEngine::Config GossipEngine::default_config() {
    return Config{};
}

GossipEngine::GossipEngine(MembershipTable& table, const Config& cfg)
    : table_(table), config_(cfg), rng_(std::random_device{}()) {}

GossipEngine::GossipEngine(MembershipTable& table)
    : table_(table), rng_(std::random_device{}()) {}

GossipEngine::~GossipEngine() = default;

void GossipEngine::tick(int64_t now_ns) {
    if (now_ns - last_gossip_ < gossip_interval_ns_) return;
    last_gossip_ = now_ns;
    incarnation_++;

    auto peers = select_fanout_peers();
    for (auto& ep : peers) {
        send_gossip_to_peer(ep);
    }
}

std::vector<GossipEngine::GossipMessage> GossipEngine::pending_updates(uint64_t since_sequence) const {
    std::vector<GossipMessage> updates;
    auto peers = table_.peers();
    for (auto& rec : peers) {
        if (rec.state == PeerState::Offline) continue;

        GossipMessage msg;
        msg.node_id = rec.node_id;
        msg.state = rec.state;
        msg.last_seen = rec.last_seen;
        msg.incarnation = incarnation_;
        updates.push_back(std::move(msg));
    }
    return updates;
}

void GossipEngine::apply_updates(const std::vector<GossipMessage>& updates) {
    for (auto& msg : updates) {
        auto existing = table_.lookup(msg.node_id);
        if (!existing) {
            PeerRecord rec;
            rec.node_id = msg.node_id;
            rec.state = msg.state;
            rec.last_seen = msg.last_seen;
            rec.ping_misses = 0;
            table_.upsert(std::move(rec));
        } else {
            auto rec = existing.value();
            if (msg.incarnation > incarnation_) {
                rec.state = msg.state;
                rec.last_seen = msg.last_seen;
                table_.upsert(std::move(rec));
            }
        }
    }
}

void GossipEngine::send_gossip_to_peer(const Endpoint& target) {
    // TODO: Implement UDP send via transport
}

std::vector<Endpoint> GossipEngine::select_fanout_peers() {
    std::vector<Endpoint> result;
    auto peers = table_.peers_with_state(PeerState::Online);

    if (peers.empty()) return result;

    std::shuffle(peers.begin(), peers.end(), rng_);
    size_t count = std::min<size_t>(config_.fanout, peers.size());

    for (size_t i = 0; i < count; ++i) {
        Endpoint ep;
        ep.scheme = "udp";
        ep.host = peers[i].endpoint.host;
        ep.port = peers[i].endpoint.port;
        result.push_back(ep);
    }
    return result;
}

} // namespace smo