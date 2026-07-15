#include "tcp_transport.hpp"

#include <cstring>
#include <memory>
#include <string>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace smo {

// ── Helpers ─────────────────────────────────────────────────────────────

static int socket_error_to_code() {
    switch (errno) {
        case ECONNREFUSED: return 300;
        case ETIMEDOUT:    return 301;
        case ECONNRESET:   return 302;
        case EADDRINUSE:   return 307;
        case EMFILE:
        case ENFILE:       return 314;
        default:           return 304;
    }
}

static Result<int> create_tcp_socket() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return SMO_ERR_TRANSPORT(306, Error, NoRetry, RestartFSM,
                                 "failed to create TCP socket");
    }
    return fd;
}

static Result<sockaddr_in> resolve_endpoint(const Endpoint& ep) {
    sockaddr_in addr{};
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ep.port);

    if (ep.host.empty() || ep.host == "0.0.0.0" || ep.host == "*") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else if (ep.host == "127.0.0.1" || ep.host == "localhost") {
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else {
        // Attempt DNS resolution
        struct addrinfo hints{};
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo* result = nullptr;

        int rc = ::getaddrinfo(ep.host.c_str(), nullptr, &hints, &result);
        if (rc != 0 || result == nullptr) {
            return SMO_ERR_TRANSPORT(308, Error, NoRetry, Reconnect,
                                     "DNS resolution failed for " + ep.host);
        }

        std::memcpy(&addr.sin_addr, &((struct sockaddr_in*)result->ai_addr)->sin_addr,
                    sizeof(addr.sin_addr));
        ::freeaddrinfo(result);
    }

    return addr;
}

// ── TcpSession ──────────────────────────────────────────────────────────

TcpSession::TcpSession(int fd, Endpoint remote)
    : fd_(fd), remote_(std::move(remote)), open_(true) {}

TcpSession::~TcpSession() noexcept {
    if (open_) {
        ::close(fd_);
    }
}

Result<void> TcpSession::send(BytesView data) {
    if (!open_) {
        return SMO_ERR_TRANSPORT(303, Error, NoRetry, None,
                                 "session is closed");
    }

    // Frame the data before sending
    Bytes framed;
    frame_write(data, kFrameFlagNone, framed);

    const auto* ptr = framed.data();
    size_t remaining = framed.size();

    while (remaining > 0) {
        ssize_t n = ::write(fd_, ptr, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            int ec = socket_error_to_code();
            if (ec == 302) open_ = false; // connection reset
            return SMO_ERR_TRANSPORT(ec, Error, RetrySafe, Reconnect,
                                     "TCP send failed");
        }
        ptr += n;
        remaining -= static_cast<size_t>(n);
    }

    return {};
}

Result<Bytes> TcpSession::recv(size_t max_bytes) {
    if (!open_) {
        return SMO_ERR_TRANSPORT(303, Error, NoRetry, None,
                                 "session is closed");
    }

    // First read the frame header
    FrameHeader hdr;
    size_t hdr_read = 0;
    while (hdr_read < sizeof(FrameHeader)) {
        ssize_t n = ::read(fd_,
                           reinterpret_cast<uint8_t*>(&hdr) + hdr_read,
                           sizeof(FrameHeader) - hdr_read);
        if (n < 0) {
            if (errno == EINTR) continue;
            int ec = socket_error_to_code();
            if (ec == 302) open_ = false;
            return SMO_ERR_TRANSPORT(ec, Error, RetrySafe, Reconnect,
                                     "TCP recv header failed");
        }
        if (n == 0) {
            open_ = false;
            return SMO_ERR_TRANSPORT(303, Info, NoRetry, None,
                                     "connection closed by peer");
        }
        hdr_read += static_cast<size_t>(n);
    }

    if (hdr.magic != 0x534D4F01) {
        return SMO_ERR_TRANSPORT(310, Warn, NoRetry, RestartFSM,
                                 "invalid frame magic");
    }

    // Check for close frame
    if (hdr.flags & kFrameFlagClose) {
        open_ = false;
        return Bytes{};
    }

    // Clamp payload size
    size_t payload_len = hdr.payload_len;
    if (payload_len > max_bytes) {
        payload_len = max_bytes;
    }

    // Read the payload
    Bytes payload(payload_len);
    size_t payload_read = 0;
    while (payload_read < payload_len) {
        ssize_t n = ::read(fd_,
                           payload.data() + payload_read,
                           payload_len - payload_read);
        if (n < 0) {
            if (errno == EINTR) continue;
            int ec = socket_error_to_code();
            if (ec == 302) open_ = false;
            return SMO_ERR_TRANSPORT(ec, Error, RetrySafe, Reconnect,
                                     "TCP recv payload failed");
        }
        if (n == 0) {
            open_ = false;
            return SMO_ERR_TRANSPORT(303, Info, NoRetry, None,
                                     "connection closed during payload read");
        }
        payload_read += static_cast<size_t>(n);
    }

    return payload;
}

Result<void> TcpSession::close() {
    if (!open_) return {};

    // Send close frame
    Bytes framed;
    frame_write(BytesView{}, kFrameFlagClose, framed);
    ::write(fd_, framed.data(), framed.size());

    open_ = false;
    ::shutdown(fd_, SHUT_RDWR);
    ::close(fd_);
    return {};
}

Endpoint TcpSession::remote_endpoint() const {
    return remote_;
}

bool TcpSession::is_open() const {
    return open_;
}

// ── TcpListener ─────────────────────────────────────────────────────────

TcpListener::TcpListener(int fd, Endpoint local)
    : fd_(fd), local_(std::move(local)) {}

TcpListener::~TcpListener() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

Result<SessionPtr> TcpListener::accept() {
    sockaddr_in peer_addr{};
    socklen_t addrlen = sizeof(peer_addr);

    int client_fd = ::accept(fd_, (struct sockaddr*)&peer_addr, &addrlen);
    if (client_fd < 0) {
        if (errno == EINTR) {
            return accept(); // retry on signal
        }
        return SMO_ERR_TRANSPORT(304, Error, RetrySafe, RestartFSM,
                                 "TCP accept failed");
    }

    // Version handshake
    auto ver = version_handshake_server(client_fd);
    if (!ver) {
        ::close(client_fd);
        return std::move(ver.error());
    }

    char peer_host[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer_addr.sin_addr, peer_host, sizeof(peer_host));

    Endpoint remote;
    remote.scheme = "tcp";
    remote.host = peer_host;
    remote.port = ntohs(peer_addr.sin_port);

    return std::unique_ptr<TransportSession>(
        new TcpSession(client_fd, std::move(remote)));
}

Result<void> TcpListener::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    return {};
}

Endpoint TcpListener::local_endpoint() const {
    return local_;
}

// ── TcpTransport ────────────────────────────────────────────────────────

Result<ListenerPtr> TcpTransport::listen(const Endpoint& ep) {
    SMO_TRY_VAL(int fd, create_tcp_socket());

    // Reuse address
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    auto addr = resolve_endpoint(ep);
    if (!addr) {
        ::close(fd);
        return std::move(addr.error());
    }

    if (::bind(fd, (struct sockaddr*)&addr.value(), sizeof(addr.value())) < 0) {
        int ec = socket_error_to_code();
        ::close(fd);
        return SMO_ERR_TRANSPORT(ec, Error, NoRetry, RestartFSM,
                                 "TCP bind failed");
    }

    if (::listen(fd, 128) < 0) {
        ::close(fd);
        return SMO_ERR_TRANSPORT(306, Error, NoRetry, RestartFSM,
                                 "TCP listen failed");
    }

    // Query actual bound address (port may be ephemeral)
    Endpoint actual = ep;
    sockaddr_in bound_addr{};
    socklen_t bound_len = sizeof(bound_addr);
    if (::getsockname(fd, (struct sockaddr*)&bound_addr, &bound_len) == 0) {
        char host[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &bound_addr.sin_addr, host, sizeof(host));
        actual.host = host;
        actual.port = ntohs(bound_addr.sin_port);
    }

    return std::unique_ptr<TransportListener>(
        new TcpListener(fd, std::move(actual)));
}

Result<SessionPtr> TcpTransport::connect(const Endpoint& ep) {
    SMO_TRY_VAL(int fd, create_tcp_socket());

    auto addr = resolve_endpoint(ep);
    if (!addr) {
        ::close(fd);
        return std::move(addr.error());
    }

    // Non-blocking connect with timeout
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = ::connect(fd, (struct sockaddr*)&addr.value(), sizeof(addr.value()));
    if (rc < 0 && errno != EINPROGRESS) {
        int ec = socket_error_to_code();
        ::close(fd);
        return SMO_ERR_TRANSPORT(ec, Error, NoRetry, Reconnect,
                                 "TCP connect failed");
    }

    if (rc < 0) {
        // Wait for connection with poll
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        int prc = ::poll(&pfd, 1, 2000); // 2s timeout
        if (prc <= 0) {
            ::close(fd);
            return SMO_ERR_TRANSPORT(301, Error, RetryBackoff, Reconnect,
                                     "TCP connect timed out");
        }
        // Check for socket error
        int so_error = 0;
        socklen_t errlen = sizeof(so_error);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &errlen);
        if (so_error != 0) {
            ::close(fd);
            return SMO_ERR_TRANSPORT(300, Error, RetryBackoff, Reconnect,
                                     "TCP connection refused");
        }
    }

    // Restore blocking mode
    fcntl(fd, F_SETFL, flags);

    // Version handshake
    auto ver = version_handshake_client(fd);
    if (!ver) {
        ::close(fd);
        return std::move(ver.error());
    }

    Endpoint remote = ep;
    return std::unique_ptr<TransportSession>(
        new TcpSession(fd, std::move(remote)));
}

} // namespace smo
