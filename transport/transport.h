#pragma once

#include <cstdint>
#include <memory>
#include <functional>
#include <span>
#include <string>
#include "core/errors/errors.h"
#include "protocol/packet/packet.h"

namespace smo::hl {

// §IV Layer 7 — Transport Abstraction
//
// SMF NEVER depends on TCP directly. Transport is interchangeable.
// (Implementations: tcp/, quic/, relay/)

struct Endpoint {
    std::string address;
    uint16_t    port{0};
};

class Transport {
public:
    virtual ~Transport() = default;

    using PacketHandler = std::function<void(Packet&&, Endpoint)>;
    using ErrorHandler  = std::function<void(std::error_code, Endpoint)>;

    virtual std::error_code listen(const Endpoint& ep,
                                   PacketHandler on_packet,
                                   ErrorHandler on_error) = 0;

    virtual std::error_code connect(const Endpoint& remote) = 0;

    virtual std::error_code send(Packet&& pkt, const Endpoint& to) = 0;

    virtual void close() noexcept = 0;
};

} // namespace smo::hl
