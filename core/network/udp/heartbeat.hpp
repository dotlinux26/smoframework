#pragma once

#include <core/types.hpp>

namespace smo::network::udp {

// Heartbeat — periodic PING/PONG over UDP for liveness detection.
// Designed for NAT keepalive + failure detection.
// Interval: 5s, timeout: 15s (SWIM-inspired suspicion model planned).
// Full implementation in Sprint 4.

struct HeartbeatConfig {
    uint32_t ping_interval_ms = 5000;
    uint32_t ping_timeout_ms  = 3000;
    uint32_t suspicion_count  = 3;
};

} // namespace smo::network::udp
