#include "port_check.hpp"

#include <cstring>
#include <cstdio>
#include <array>

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
#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket(fd) ::close(fd)
#endif

namespace smo { namespace net {

bool check_port_available(const std::string& addr, uint16_t port, std::error_code& ec) {
    ec.clear();

    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) {
        ec = std::make_error_code(std::errc::address_family_not_supported);
        return false;
    }

    int optval = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&optval), sizeof(optval));

    sockaddr_in bind_addr;
    std::memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(port);
    inet_pton(AF_INET, addr.c_str(), &bind_addr.sin_addr);

    int rc = bind(s, (sockaddr*)&bind_addr, sizeof(bind_addr));
    closesocket(s);

    if (rc == 0) return true;  // port is free

    // EADDRINUSE means port is taken
#if defined(_WIN32) || defined(_WIN64)
    if (WSAGetLastError() == WSAEADDRINUSE)
#else
    if (errno == EADDRINUSE)
#endif
    {
        ec = std::make_error_code(std::errc::address_in_use);
        return false;
    }

    ec = std::make_error_code(std::errc::io_error);
    return false;
}

std::string who_is_on_port(uint16_t port) {
    // Best-effort: try lsof or ss on Linux, netstat on macOS/Windows
    std::array<char, 256> result;
    result.fill(0);

#if defined(__linux__)
    // Try ss first (modern, faster), fallback to lsof
    std::string cmd = "ss -tlnp sport = :" + std::to_string(port) + " 2>/dev/null | tail -1";
    FILE* fp = popen(cmd.c_str(), "r");
    if (fp) {
        if (fgets(result.data(), static_cast<int>(result.size()) - 1, fp)) {
            pclose(fp);
            std::string out(result.data());
            // Extract process name from ss output: users:(("nginx",pid=12345,...))
            auto pid_start = out.find("pid=");
            auto proc_start = out.find("(\"");
            if (pid_start != std::string::npos && proc_start != std::string::npos) {
                auto pid_end = out.find(',', pid_start);
                auto proc_end = out.find('"', proc_start + 2);
                std::string proc = out.substr(proc_start + 2, proc_end - proc_start - 2);
                std::string pid = out.substr(pid_start + 4, pid_end - pid_start - 4);
                return "PID " + pid + " (" + proc + ")";
            }
        }
        pclose(fp);
    }
#elif defined(__APPLE__)
    std::string cmd = "lsof -i TCP:" + std::to_string(port) + " 2>/dev/null | tail -1";
    FILE* fp = popen(cmd.c_str(), "r");
    if (fp) {
        if (fgets(result.data(), static_cast<int>(result.size()) - 1, fp)) {
            pclose(fp);
            std::string out(result.data());
            // lsof output: COMMAND PID ... -> extract first two fields
            auto space1 = out.find(' ');
            if (space1 != std::string::npos) {
                auto space2 = out.find(' ', space1 + 1);
                if (space2 != std::string::npos) {
                    return out.substr(0, space2);
                }
            }
        }
        pclose(fp);
    }
#endif

    return {};
}

}} // namespace smo::net
