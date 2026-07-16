#pragma once

#include <string>

namespace smo {
namespace net {

struct NatStatus {
    bool behind_nat = false;
    std::string private_ip;
    std::string public_ip;
    bool port_forwarding_required = false;
};

// Detect NAT by comparing private and public IP.
// private_ip: detected from local interface
// public_ip: detected via external service (or empty if unavailable)
NatStatus detect_nat(const std::string& private_ip, const std::string& public_ip);

}} // namespace smo::net
