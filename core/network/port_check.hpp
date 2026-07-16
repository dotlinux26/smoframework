#pragma once

#include <string>
#include <system_error>
#include <cstdint>

namespace smo {
namespace net {

// Check if a TCP port is available for listening on the given address.
// Uses bind() with SO_REUSEADDR and immediately closes.
// Returns true if port is available, false with ec set on failure.
//
// addr: IP to bind to (e.g. "0.0.0.0", "127.0.0.1")
// port: port number
bool check_port_available(const std::string& addr, uint16_t port, std::error_code& ec);

// Convenience: check port on all interfaces (0.0.0.0)
inline bool check_port_available(uint16_t port, std::error_code& ec) {
    return check_port_available("0.0.0.0", port, ec);
}

// Get the process name/pid using a given port (best-effort, platform-specific).
// Returns a human-readable string like "PID 12345 (nginx)" or empty if not found.
std::string who_is_on_port(uint16_t port);

}} // namespace smo::net
