#pragma once

#include <string>
#include <system_error>
#include <chrono>

namespace smo {
namespace net {

// Detect public IP by connecting to a public STUN-like echo service.
// Falls back to HTTP-based detection if UDP fails.
// Returns the public IP as a string, or empty on failure.
//
// Implementation:
//   1. Send a UDP datagram to a public echo server (e.g. 8.8.8.8:19302)
//      and read the socket's bound address to learn the public IP.
//   2. Fallback: HTTP GET to https://api.ipify.org?format=text
std::string detect_public_ip(std::error_code& ec,
                             std::chrono::seconds timeout = std::chrono::seconds(5));

// Quick check: does the given address look like a public IP?
bool is_public_address(const std::string& addr);

}} // namespace smo::net
