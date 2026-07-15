#pragma once

#include "transport.hpp"
#include "framing.hpp"

namespace smo {

// ── TcpSession ──────────────────────────────────────────────────────────

class TcpSession final : public TransportSession {
public:
    TcpSession(int fd, Endpoint remote);
    ~TcpSession() noexcept override;

    TcpSession(const TcpSession&) = delete;
    TcpSession& operator=(const TcpSession&) = delete;

    Result<void> send(BytesView data) override;
    Result<Bytes> recv(size_t max_bytes) override;
    Result<void> close() override;
    Endpoint remote_endpoint() const override;
    bool is_open() const override;

private:
    int fd_;
    Endpoint remote_;
    bool open_;
};

// ── TcpListener ─────────────────────────────────────────────────────────

class TcpListener final : public TransportListener {
public:
    TcpListener(int fd, Endpoint local);
    ~TcpListener() noexcept override;

    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    Result<SessionPtr> accept() override;
    Result<void> close() override;
    Endpoint local_endpoint() const override;

private:
    int fd_;
    Endpoint local_;
};

// ── TcpTransport ────────────────────────────────────────────────────────

class TcpTransport final : public Transport {
public:
    TcpTransport() = default;

    std::string_view name() const override { return "TCP"; }

    Result<ListenerPtr> listen(const Endpoint& ep) override;
    Result<SessionPtr> connect(const Endpoint& ep) override;
};

} // namespace smo
