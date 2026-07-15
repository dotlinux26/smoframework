#pragma once

#include <core/types.hpp>
#include <string>

namespace smo::network::bootstrap {

// Seed resolver — parses and resolves seed node addresses.
// Supports: host:port, ip:port, DNS-based seed discovery.
// Seed priority fallback: try seed list in order, skip unreachable.

struct SeedEndpoint {
    std::string host;
    uint16_t port = 7777;
    bool tls = false;
};

// Resolve a seed address string into a list of candidate endpoints.
// For DNS seeds, resolves all A/AAAA records.
// Returns empty list on parse failure.

} // namespace smo::network::bootstrap
