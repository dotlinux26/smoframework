#pragma once

#include <string>
#include <vector>
#include <system_error>

namespace smo {
namespace net {

struct InterfaceInfo {
    std::string name;
    std::string address;  // IPv4 string
    bool is_loopback = false;
    bool is_private  = false;  // RFC 1918
};

// Enumerate all local network interfaces.
// Returns a vector of InterfaceInfo, or error-code on system failure.
std::vector<InterfaceInfo> enumerate_interfaces(std::error_code& ec);

// Convenience: enumerate and split into categories.
inline std::vector<InterfaceInfo> loopback_interfaces(std::error_code& ec) {
    auto all = enumerate_interfaces(ec);
    if (ec) return {};
    std::vector<InterfaceInfo> out;
    for (auto& iface : all)
        if (iface.is_loopback) out.push_back(std::move(iface));
    return out;
}

inline std::vector<InterfaceInfo> private_interfaces(std::error_code& ec) {
    auto all = enumerate_interfaces(ec);
    if (ec) return {};
    std::vector<InterfaceInfo> out;
    for (auto& iface : all)
        if (!iface.is_loopback && iface.is_private) out.push_back(std::move(iface));
    return out;
}

inline std::vector<InterfaceInfo> public_interfaces(std::error_code& ec) {
    auto all = enumerate_interfaces(ec);
    if (ec) return {};
    std::vector<InterfaceInfo> out;
    for (auto& iface : all)
        if (!iface.is_loopback && !iface.is_private) out.push_back(std::move(iface));
    return out;
}

}} // namespace smo::net
