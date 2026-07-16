#include "nat_detect.hpp"

namespace smo { namespace net {

NatStatus detect_nat(const std::string& private_ip, const std::string& public_ip) {
    NatStatus status;
    status.private_ip = private_ip;
    status.public_ip = public_ip;

    if (public_ip.empty() || private_ip.empty()) {
        // Can't determine NAT
        status.behind_nat = false;
        status.port_forwarding_required = false;
        return status;
    }

    // If private and public are different, we're behind NAT
    status.behind_nat = (private_ip != public_ip);

    // Port forwarding is required if:
    // - Behind NAT (private != public)
    // - AND private IP is RFC 1918
    status.port_forwarding_required = status.behind_nat;

    return status;
}

}} // namespace smo::net
