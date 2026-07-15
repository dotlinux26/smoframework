#pragma once

#include <core/types.hpp>
#include <string>
#include <vector>

namespace smo::network::bootstrap {

// Peer cache — stores discovered peer table from seed response.
// Used to reconnect without re-contacting seed after bootstrap.
// Serialized to disk for persistence across restarts.

struct CachedPeer {
    std::string node_id;
    std::string host;
    uint16_t port = 7777;
    int64_t last_seen = 0; // Unix ms
};

} // namespace smo::network::bootstrap
