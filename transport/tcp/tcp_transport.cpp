#include "tcp_transport.h"

#include "../../core/errors/error.hpp"
#include "../../core/types.hpp"
#include "../../core/transport/framing.hpp"
#include "../../protocol/packet/packet.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace smo::hl {

// ── Helpers ─────────────────────────────────────────────────────────────

static int sock_err_to_code() {
    switch (errno) {
        case ECONNREFUSED: return 300;
        case ETIMEDOUT:    return 301;
        case ECONNRESET:   return 302;
        case EADDRINUSE:   return 307;
        default:           return 304;
    }
}

static std::error_code errc_from_code(int code) {
    return std::error_code(code, std::generic_category());
}

static Result<sockaddr_in> resolve(const std::string& host, uint16_t port) {
    sockaddr_in addr{};
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (host.empty() || host == "0.0.0.0" || host == "*") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else if (host == "127.0.0.1" || host == "localhost") {
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;
        int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &result);
        if (rc != 0 || result == nullptr) {
            return SMO_ERR_TRANSPORT(308, Error, NoRetry, Reconnect,
                                     "DNS resolution failed for " + host);
        }
        auto* ai = reinterpret_cast<sockaddr_in*>(result->ai_addr);
        std::memcpy(&addr.sin_addr, &ai->sin_addr, sizeof(addr.sin_addr));
        ::freeaddrinfo(result);
    }
    return addr;
}

static int create_socket() {
    return ::socket(AF_INET, SOCK_STREAM, 0);
}

// ── Impl ──────────────────────────────────────────────────────────────

struct TcpTransport::Impl {
    // Listener state
    int listen_fd_{-1};
    std::thread accept_thread;
    std::atomic<bool> running{false};

    // Callbacks (set by listen)
    Transport::PacketHandler on_packet;
    Transport::ErrorHandler  on_error;

    // Active sessions (key = "address:port")
    struct Session {
        int fd{-1};
        std::string address;
        uint16_t port{0};
    };
    std::mutex sessions_mutex;
    std::unordered_map<std::string, Session> sessions;

    // Per-session reader threads
    std::vector<std::thread> reader_threads;

    ~Impl() { shutdown(); }

    void shutdown() {
        running = false;

        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (accept_thread.joinable())
            accept_thread.join();

        std::lock_guard<std::mutex> lock(sessions_mutex);
        for (auto& [_, s] : sessions) {
            if (s.fd >= 0) {
                ::shutdown(s.fd, SHUT_RDWR);
                ::close(s.fd);
            }
        }
        sessions.clear();
        for (auto& t : reader_threads) {
            if (t.joinable()) t.join();
        }
        reader_threads.clear();
    }

    void accept_loop() {
        while (running && listen_fd_ >= 0) {
            // Poll with timeout so we can check running
            pollfd pfd;
            pfd.fd = listen_fd_;
            pfd.events = POLLIN;
            int prc = ::poll(&pfd, 1, 500);
            if (prc <= 0) continue;
            if (!(pfd.revents & POLLIN)) continue;

            sockaddr_in peer_addr{};
            socklen_t addrlen = sizeof(peer_addr);

            int client_fd = ::accept(listen_fd_,
                                     reinterpret_cast<sockaddr*>(&peer_addr), &addrlen);
            if (client_fd < 0) {
                if (errno == EINTR) continue;
                continue;
            }

            // Version handshake
            auto ver = version_handshake_server(client_fd);
            if (!ver) {
                ::close(client_fd);
                continue;
            }

            char peer_host[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &peer_addr.sin_addr, peer_host, sizeof(peer_host));
            uint16_t peer_port = ntohs(peer_addr.sin_port);

            Endpoint remote;
            remote.address = peer_host;
            remote.port = peer_port;
            auto key = remote.address + ":" + std::to_string(remote.port);

            {
                std::lock_guard<std::mutex> lock(sessions_mutex);
                sessions[key] = Session{client_fd, remote.address, remote.port};
            }

            reader_threads.emplace_back([this, key, remote]() {
                reader_loop(key, remote);
            });
        }
    }

    void reader_loop(const std::string& key, const Endpoint& remote) {
        Session* s = nullptr;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex);
            auto it = sessions.find(key);
            if (it == sessions.end()) return;
            s = &it->second;
        }

        while (running && s->fd >= 0) {
            // Poll with timeout before each read
            pollfd pfd;
            pfd.fd = s->fd;
            pfd.events = POLLIN;
            int prc = ::poll(&pfd, 1, 500);
            if (prc <= 0) continue;
            if (!(pfd.revents & POLLIN)) continue;

            // Read frame header
            FrameHeader hdr;
            size_t hdr_read = 0;
            while (hdr_read < sizeof(FrameHeader)) {
                ssize_t n = ::read(s->fd,
                                   reinterpret_cast<uint8_t*>(&hdr) + hdr_read,
                                   sizeof(FrameHeader) - hdr_read);
                if (n < 0) {
                    if (errno == EINTR) continue;
                    if (on_error) on_error(errc_from_code(sock_err_to_code()), remote);
                    return;
                }
                if (n == 0) return;
                hdr_read += static_cast<size_t>(n);
            }

            if (hdr.magic != 0x534D4F01) {
                if (on_error) on_error(errc_from_code(310), remote);
                return;
            }

            if (hdr.flags & kFrameFlagClose) return;

            // Read payload
            size_t payload_len = hdr.payload_len;
            if (payload_len > 65536) payload_len = 65536;

            Bytes payload(payload_len);
            size_t payload_read = 0;
            while (payload_read < payload_len) {
                ssize_t n = ::read(s->fd,
                                   payload.data() + payload_read,
                                   payload_len - payload_read);
                if (n < 0) {
                    if (errno == EINTR) continue;
                    if (on_error) on_error(errc_from_code(sock_err_to_code()), remote);
                    return;
                }
                if (n == 0) return;
                payload_read += static_cast<size_t>(n);
            }

            // Deserialize packet
            auto pkt = packet_from_buffer(payload);
            if (!pkt) {
                if (on_error)
                    on_error(errc_from_code(
                        static_cast<int>(pkt.error().code.code)), remote);
                continue;
            }

            if (on_packet)
                on_packet(std::move(pkt.value()), remote);
        }
    }
};

// ── TcpTransport ──────────────────────────────────────────────────────

TcpTransport::TcpTransport() noexcept
    : impl_(std::make_unique<Impl>()) {}

TcpTransport::~TcpTransport() noexcept = default;

std::error_code TcpTransport::listen(const Endpoint& ep,
                                     PacketHandler on_packet,
                                     ErrorHandler on_error) {
    impl_->on_packet = std::move(on_packet);
    impl_->on_error  = std::move(on_error);

    int fd = create_socket();
    if (fd < 0) return errc_from_code(306);

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    auto addr = resolve(ep.address, ep.port);
    if (!addr) {
        ::close(fd);
        return errc_from_code(static_cast<int>(addr.error().code.code));
    }

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr.value()), sizeof(addr.value())) < 0) {
        int ec = sock_err_to_code();
        ::close(fd);
        return errc_from_code(ec);
    }

    if (::listen(fd, 128) < 0) {
        ::close(fd);
        return errc_from_code(306);
    }

    impl_->listen_fd_ = fd;
    impl_->running = true;
    impl_->accept_thread = std::thread(&Impl::accept_loop, impl_.get());

    return {};
}

std::error_code TcpTransport::connect(const Endpoint& remote) {
    int fd = create_socket();
    if (fd < 0) return errc_from_code(306);

    auto addr = resolve(remote.address, remote.port);
    if (!addr) {
        ::close(fd);
        return errc_from_code(static_cast<int>(addr.error().code.code));
    }

    // Non-blocking connect with timeout
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr.value()), sizeof(addr.value()));
    if (rc < 0 && errno != EINPROGRESS) {
        int ec = sock_err_to_code();
        ::close(fd);
        return errc_from_code(ec);
    }

    if (rc < 0) {
        pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        int prc = ::poll(&pfd, 1, 2000);
        if (prc <= 0) {
            ::close(fd);
            return errc_from_code(301);
        }
        int so_error = 0;
        socklen_t errlen = sizeof(so_error);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &errlen);
        if (so_error != 0) {
            ::close(fd);
            return errc_from_code(300);
        }
    }

    fcntl(fd, F_SETFL, flags);

    // Version handshake
    auto ver = version_handshake_client(fd);
    if (!ver) {
        ::close(fd);
        return errc_from_code(static_cast<int>(ver.error().code.code));
    }

    auto key = remote.address + ":" + std::to_string(remote.port);
    {
        std::lock_guard<std::mutex> lock(impl_->sessions_mutex);
        impl_->sessions[key] = Impl::Session{fd, remote.address, remote.port};
    }
    return {};
}

std::error_code TcpTransport::send(Packet&& pkt, const Endpoint& to) {
    auto key = to.address + ":" + std::to_string(to.port);

    Impl::Session* s = nullptr;
    {
        std::lock_guard<std::mutex> lock(impl_->sessions_mutex);
        auto it = impl_->sessions.find(key);
        if (it == impl_->sessions.end()) {
            return errc_from_code(500);
        }
        s = &it->second;
    }

    // Serialize packet
    std::vector<uint8_t> buf;
    auto pack_res = packet_to_buffer(pkt, buf);
    if (!pack_res) {
        return errc_from_code(100);
    }

    // Frame the data
    Bytes framed;
    frame_write(BytesView(buf.data(), buf.size()), kFrameFlagNone, framed);

    // Send
    const auto* ptr = framed.data();
    size_t remaining = framed.size();
    while (remaining > 0) {
        ssize_t n = ::write(s->fd, ptr, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            return errc_from_code(sock_err_to_code());
        }
        ptr += n;
        remaining -= static_cast<size_t>(n);
    }

    return {};
}

void TcpTransport::close() noexcept {
    impl_->shutdown();
}

} // namespace smo