#include "heartbeat_service.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace smo::network::udp {

HeartbeatService::Config HeartbeatService::default_config() {
    return Config{};
}

HeartbeatService::HeartbeatService(const Config& cfg)
    : config_(cfg) {}

HeartbeatService::HeartbeatService()
    : config_(default_config()) {}

Result<void> HeartbeatService::start(UdpTransport& udp_transport,
                                     smo::MembershipTable& membership,
                                     smo::HealthMonitor& health) {
    udp_ = &udp_transport;
    membership_ = &membership;
    health_ = &health;

    // Listen on local UDP port
    smo::Endpoint local_ep;
    local_ep.scheme = "udp";
    local_ep.host = "0.0.0.0";
    local_ep.port = config_.local_port;

    auto listen_result = udp_transport.listen(local_ep);
    if (!listen_result) {
        return listen_result.error();
    }
    listener_ = std::move(listen_result.value());

    running_ = true;
    last_ping_ns_ = std::chrono::system_clock::now().time_since_epoch().count();

    return {};
}

void HeartbeatService::stop() {
    running_ = false;
    if (listener_) {
        listener_->close();
        listener_.reset();
    }
}

void HeartbeatService::tick(int64_t now_ns) {
    if (!running_) return;

    // Send periodic PINGs
    if (now_ns - last_ping_ns_ >= static_cast<int64_t>(config_.ping_interval_ms) * 1'000'000) {
        send_ping_to_all(now_ns);
        last_ping_ns_ = now_ns;
    }

    // Check peer health (timeout detection)
    check_peer_health(now_ns);
}

Result<void> HeartbeatService::handle_pong(const smo::PongMsg& msg,
                                           int64_t now_ns,
                                           const smo::Endpoint& from) {
    (void)msg;
    // Find peer in membership table by endpoint
    auto peers = membership_->peers_with_state(smo::PeerState::Online);
    for (auto& rec : peers) {
        if (rec.endpoint.host == from.host && rec.endpoint.port == from.port) {
            // Record PONG, calculate RTT
            int64_t rtt_ns = now_ns - msg.timestamp;
            health_->record_pong(rec.node_id, now_ns);

            // Update RTT in membership (moving average)
            auto rec_opt = membership_->lookup(rec.node_id);
            if (rec_opt) {
                auto updated = rec_opt.value();
                updated.rtt_ms = static_cast<double>(rtt_ns) / 1'000'000.0;
                membership_->upsert(std::move(updated));
            }
            return {};
        }
    }
    return SMO_ERR_DISCOVERY(400, Info, RetrySafe, None, "unknown peer sent PONG");
}

Result<void> HeartbeatService::handle_ping(const smo::PingMsg& msg,
                                           int64_t now_ns,
                                           const smo::Endpoint& from,
                                           UdpTransport& udp_transport) {
    (void)msg;
    // Respond with PONG echoing the timestamp
    smo::PongMsg pong;
    pong.timestamp = msg.timestamp;

    auto pong_data = pong.serialize();

    // Send PONG back
    auto session = udp_transport.connect(from);
    if (!session) return session.error();

    return session.value()->send(pong_data);
}

void HeartbeatService::send_ping_to_all(int64_t now_ns) {
    if (!udp_ || !membership_) return;

    auto peers = membership_->peers_with_state(smo::PeerState::Online);
    for (auto& rec : peers) {
        // Skip self
        if (rec.endpoint.port == 0) continue;

        // Send PING
        smo::PingMsg ping;
        ping.timestamp = now_ns;
        ++ping_sequence_;

        auto ping_data = ping.serialize();

        smo::Endpoint target;
        target.scheme = "udp";
        target.host = rec.endpoint.host;
        target.port = rec.endpoint.port;

        auto session = udp_->connect(target);
        if (session) {
            session.value()->send(ping.serialize()).ignore();
        }

        // Record ping sent
        health_->record_ping(rec.node_id, now_ns);
    }
}

void HeartbeatService::check_peer_health(int64_t now_ns) {
    if (!membership_ || !health_) return;

    health_->tick(*membership_, now_ns,
                  static_cast<int64_t>(config_.ping_timeout_ms) * 1'000'000,
                  config_.max_misses);
}

} // namespace smo::network::udp