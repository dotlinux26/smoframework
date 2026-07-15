#include "core/discovery/gossip.hpp"

#include <algorithm>
#include <chrono>
#include <random>

namespace smo {

GossipEngine::GossipEngine(MembershipTable& table) : table_(table) {}

void GossipEngine::tick(int64_t now_ns) {
    if (now_ns - last_gossip_ < gossip_interval_ns_) return;
    last_gossip_ = now_ns;
    incarnation_++;
}

std::vector<GossipEngine::GossipMessage> GossipEngine::pending_updates() const {
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
            // New peer from gossip
            PeerRecord rec;
            rec.node_id = msg.node_id;
            rec.state = msg.state;
            rec.last_seen = msg.last_seen;
            rec.ping_misses = 0;
            table_.upsert(std::move(rec));
        } else {
            // Update existing peer with newer info
            auto rec = existing.value();
            if (msg.incarnation > incarnation_) {
                rec.state = msg.state;
                rec.last_seen = msg.last_seen;
                table_.upsert(std::move(rec));
            }
        }
    }
}

} // namespace smo