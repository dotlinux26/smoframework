#include "interface.hpp"

#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <cstring>
#endif

#include <cstdlib>

namespace smo { namespace net {

// RFC 1918 private ranges
static bool is_rfc1918(const std::string& addr) {
    if (addr.rfind("10.", 0) == 0) return true;
    if (addr.rfind("172.", 0) == 0 && addr.size() > 5) {
        int second = std::atoi(addr.substr(5).c_str());
        return second >= 16 && second <= 31;
    }
    if (addr.rfind("192.168.", 0) == 0) return true;
    return false;
}

std::vector<InterfaceInfo> enumerate_interfaces(std::error_code& ec) {
    ec.clear();
    std::vector<InterfaceInfo> result;

#if defined(_WIN32) || defined(_WIN64)
    // Windows: use GetAdaptersAddresses
    ULONG buf_len = 15000;
    std::vector<char> buf(buf_len);
    PIP_ADAPTER_ADDRESSES adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
    DWORD rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapters, &buf_len);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buf.resize(buf_len);
        adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
        rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapters, &buf_len);
    }
    if (rc != NO_ERROR) {
        ec = std::make_error_code(std::errc::no_such_device);
        return {};
    }
    for (PIP_ADAPTER_ADDRESSES a = adapters; a; a = a->Next) {
        for (PIP_ADAPTER_UNICAST_ADDRESS u = a->FirstUnicastAddress; u; u = u->Next) {
            sockaddr_in* sa = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
            char buf4[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &sa->sin_addr, buf4, sizeof(buf4))) {
                InterfaceInfo info;
                info.name = a->AdapterName;
                info.address = buf4;
                info.is_loopback = (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK);
                info.is_private = is_rfc1918(info.address);
                result.push_back(std::move(info));
            }
        }
    }
#else
    // POSIX: use getifaddrs
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
        ec = std::make_error_code(std::errc::no_such_device);
        return {};
    }
    for (struct ifaddrs* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        sockaddr_in* sa = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
        char addr_buf[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &sa->sin_addr, addr_buf, sizeof(addr_buf))) {
            InterfaceInfo info;
            info.name = ifa->ifa_name ? ifa->ifa_name : "unknown";
            info.address = addr_buf;
            info.is_loopback = (ifa->ifa_flags & IFF_LOOPBACK) != 0;
            info.is_private = is_rfc1918(info.address);
            result.push_back(std::move(info));
        }
    }
    freeifaddrs(ifaddr);
#endif

    return result;
}

}} // namespace smo::net
