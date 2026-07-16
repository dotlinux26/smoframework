#include "dns.hpp"

#include <cstring>
#include <cstdlib>

#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

namespace smo { namespace net {

std::string resolve_hostname(const std::string& hostname, std::error_code& ec) {
    ec.clear();

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;      // IPv4 only
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* result = nullptr;
    int rc = getaddrinfo(hostname.c_str(), nullptr, &hints, &result);
    if (rc != 0 || !result) {
        ec = std::make_error_code(std::errc::host_unreachable);
        return {};
    }

    sockaddr_in* sa = reinterpret_cast<sockaddr_in*>(result->ai_addr);
    char addr_buf[INET_ADDRSTRLEN];
    const char* ok = inet_ntop(AF_INET, &sa->sin_addr, addr_buf, sizeof(addr_buf));
    freeaddrinfo(result);

    if (!ok) {
        ec = std::make_error_code(std::errc::bad_address);
        return {};
    }

    return addr_buf;
}

bool looks_like_dns_name(const std::string& s) {
    if (s.empty()) return false;
    // If it contains only digits and dots, it's an IP
    bool has_alpha = false;
    for (char c : s) {
        if (std::isalpha(c) || c == '-') { has_alpha = true; break; }
    }
    return has_alpha;
}

}} // namespace smo::net
