#include "transport.hpp"

#include <charconv>
#include <cstring>

namespace smo {

// ── Endpoint ───────────────────────────────────────────────────────────

std::string Endpoint::to_string() const {
    std::string result;
    if (!scheme.empty()) {
        result = scheme;
        result += "://";
    }
    if (!host.empty()) {
        result += host;
    }
    if (port != 0) {
        result += ":";
        result += std::to_string(port);
    }
    if (!path.empty()) {
        if (port == 0 && !host.empty()) result += ":";
        result += path;
    }
    return result;
}

Result<Endpoint> Endpoint::from_string(std::string_view s) {
    Endpoint ep;

    // Parse scheme://...
    auto scheme_pos = s.find("://");
    if (scheme_pos != std::string_view::npos) {
        ep.scheme = std::string(s.substr(0, scheme_pos));
        s.remove_prefix(scheme_pos + 3);
    }

    // Parse host:port or host:path or just path
    if (s.empty()) {
        return ep;
    }

    // Check for Unix socket (path starting with /)
    if (s[0] == '/') {
        ep.path = std::string(s);
        return ep;
    }

    // Parse host part (up to ':')
    auto colon_pos = s.find(':');
    if (colon_pos != std::string_view::npos) {
        ep.host = std::string(s.substr(0, colon_pos));
        s.remove_prefix(colon_pos + 1);
        // Try to parse port (number)
        uint16_t port_val = 0;
        auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), port_val);
        if (ec == std::errc()) {
            ep.port = port_val;
        } else {
            ep.path = std::string(s);
        }
    } else {
        ep.host = std::string(s);
    }

    return ep;
}

// ── TransportRegistry ───────────────────────────────────────────────────

void TransportRegistry::register_transport(TransportPtr transport, std::string scheme) {
    transports_[std::move(scheme)] = std::move(transport);
}

Transport* TransportRegistry::get(std::string_view scheme) const {
    auto key = std::string(scheme);
    auto it = transports_.find(key);
    if (it != transports_.end()) {
        return it->second.get();
    }
    return nullptr;
}

Result<SessionPtr> TransportRegistry::connect(const Endpoint& ep) {
    auto* transport = get(ep.scheme);
    if (!transport) {
        return SMO_ERR_TRANSPORT(308, Error, NoRetry, Reconnect,
                                 "no transport registered for scheme: " + ep.scheme);
    }
    return transport->connect(ep);
}

TransportRegistry& TransportRegistry::instance() {
    static TransportRegistry registry;
    return registry;
}

} // namespace smo
