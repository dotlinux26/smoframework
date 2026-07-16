#include "public_ip.hpp"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <thread>

#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket(fd) ::close(fd)
#endif

namespace smo { namespace net {

// Simple check if addr looks like a public (non-private, non-loopback) IP
bool is_public_address(const std::string& addr) {
    if (addr.empty()) return false;
    // Remove port suffix if present
    std::string host = addr;
    auto colon = host.rfind(':');
    if (colon != std::string::npos && colon > 0) {
        // Check if colon is port separator (has digits after)
        bool all_digits = true;
        for (size_t i = colon + 1; i < host.size(); ++i)
            if (!std::isdigit(host[i])) { all_digits = false; break; }
        if (all_digits) host = host.substr(0, colon);
    }
    // Check loopback
    if (host == "127.0.0.1" || host == "localhost" || host == "::1") return false;
    // Check RFC 1918 private
    if (host.rfind("10.", 0) == 0) return false;
    if (host.rfind("172.", 0) == 0 && host.size() > 5) {
        int second = std::atoi(host.substr(5).c_str());
        if (second >= 16 && second <= 31) return false;
    }
    if (host.rfind("192.168.", 0) == 0) return false;
    // Check link-local
    if (host.rfind("169.254.", 0) == 0) return false;
    // Check CGNAT (100.64.0.0/10)
    if (host.rfind("100.", 0) == 0 && host.size() > 4) {
        int second = std::atoi(host.substr(4).c_str());
        if (second >= 64 && second <= 127) return false;
    }
    return true;
}

// Platform-independent socket close helper
static void close_sock(SOCKET s) {
    if (s != INVALID_SOCKET) closesocket(s);
}

// Set socket to non-blocking mode
static bool set_nonblocking(SOCKET s) {
#if defined(_WIN32) || defined(_WIN64)
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

std::string detect_public_ip(std::error_code& ec, std::chrono::seconds timeout) {
    ec.clear();

    // Method 1: UDP connect to Google DNS (8.8.8.8:53) to learn our source IP
    SOCKET udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock != INVALID_SOCKET) {
        sockaddr_in peer;
        std::memset(&peer, 0, sizeof(peer));
        peer.sin_family = AF_INET;
        peer.sin_port = htons(53);
        inet_pton(AF_INET, "8.8.8.8", &peer.sin_addr);

        if (connect(udp_sock, (sockaddr*)&peer, sizeof(peer)) == 0) {
            sockaddr_in local;
            socklen_t local_len = sizeof(local);
            if (getsockname(udp_sock, (sockaddr*)&local, &local_len) == 0) {
                char buf[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf))) {
                    std::string ip(buf);
                    if (is_public_address(ip)) {
                        closesocket(udp_sock);
                        return ip;
                    }
                }
            }
        }
        closesocket(udp_sock);
    }

    // Method 2: HTTP GET to ipify.org (simple TCP request)
    SOCKET tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sock == INVALID_SOCKET) {
        ec = std::make_error_code(std::errc::address_family_not_supported);
        return {};
    }

    // Lookup api.ipify.org
    struct hostent* server = gethostbyname("api.ipify.org");
    if (!server) {
        closesocket(tcp_sock);
        ec = std::make_error_code(std::errc::host_unreachable);
        return {};
    }

    sockaddr_in dest;
    std::memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(80);
    std::memcpy(&dest.sin_addr, server->h_addr, server->h_length);

    // Set non-blocking for timeout
    set_nonblocking(tcp_sock);
    connect(tcp_sock, (sockaddr*)&dest, sizeof(dest));

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(tcp_sock, &fds);
    struct timeval tv;
    tv.tv_sec = static_cast<long>(timeout.count());
    tv.tv_usec = 0;

    if (select(static_cast<int>(tcp_sock) + 1, nullptr, &fds, nullptr, &tv) <= 0) {
        closesocket(tcp_sock);
        ec = std::make_error_code(std::errc::timed_out);
        return {};
    }

    // Send HTTP request
    const char* request = "GET / HTTP/1.0\r\nHost: api.ipify.org\r\n\r\n";
    send(tcp_sock, request, static_cast<int>(std::strlen(request)), 0);

    // Receive response
    std::string response;
    std::array<char, 512> buf;
    int n;
    while ((n = static_cast<int>(recv(tcp_sock, buf.data(), static_cast<int>(buf.size() - 1), 0))) > 0) {
        buf[n] = 0;
        response.append(buf.data());
    }
    closesocket(tcp_sock);

    if (response.empty()) {
        ec = std::make_error_code(std::errc::no_message);
        return {};
    }

    // Parse body (after header \r\n\r\n)
    auto body_start = response.find("\r\n\r\n");
    if (body_start == std::string::npos) {
        ec = std::make_error_code(std::errc::protocol_error);
        return {};
    }
    std::string body = response.substr(body_start + 4);
    // Trim whitespace
    while (!body.empty() && (body.back() == '\n' || body.back() == '\r' || body.back() == ' '))
        body.pop_back();

    if (body.empty() || !is_public_address(body)) {
        ec = std::make_error_code(std::errc::no_link);
        return {};
    }

    return body;
}

}} // namespace smo::net
