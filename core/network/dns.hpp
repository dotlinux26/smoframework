#pragma once

#include <string>
#include <system_error>
#include <cstdint>

namespace smo {
namespace net {

// Resolve a hostname to an IPv4 address string.
// Returns the first A record, or empty on failure.
std::string resolve_hostname(const std::string& hostname, std::error_code& ec);

// Check if a string is a valid DNS name (vs. IP address).
bool looks_like_dns_name(const std::string& s);

// Extract host part from "host:port" string.
inline std::string parse_host(const std::string& endpoint) {
    auto colon = endpoint.rfind(':');
    if (colon == std::string::npos) return endpoint;
    // Check if there's an IPv6 address bracket
    if (colon > 0 && endpoint[colon - 1] == ']') {
        auto bracket = endpoint.rfind('[');
        return bracket != std::string::npos ? endpoint.substr(bracket, colon - bracket) : endpoint.substr(0, colon);
    }
    return endpoint.substr(0, colon);
}

// Extract port from "host:port" string. Returns 0 if not found.
inline uint16_t parse_port(const std::string& endpoint) {
    auto colon = endpoint.rfind(':');
    if (colon == std::string::npos) return 0;
    try {
        int p = std::stoi(endpoint.substr(colon + 1));
        return static_cast<uint16_t>(p > 0 && p <= 65535 ? p : 0);
    } catch (...) { return 0; }
}

}} // namespace smo::net
