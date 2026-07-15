#pragma once

#include "transport/transport.h"

namespace smo::hl {

class TcpTransport final : public Transport {
public:
    TcpTransport() noexcept;
    ~TcpTransport() noexcept override;

    TcpTransport(const TcpTransport&) = delete;
    TcpTransport& operator=(const TcpTransport&) = delete;

    std::error_code listen(const Endpoint& ep,
                           PacketHandler on_packet,
                           ErrorHandler on_error) override;

    std::error_code connect(const Endpoint& remote) override;

    std::error_code send(Packet&& pkt, const Endpoint& to) override;

    void close() noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace smo::hl

