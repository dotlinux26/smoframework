#pragma once

#include "protocol/packet/packet.h"
#include "transport/transport.h"
#include "core/transport/transport.hpp"
#include "core/errors/error.hpp"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <memory>

namespace smo { class TcpSession; }
namespace smo::network {

// PacketDispatcher routes incoming Packets by opcode_id.
//
// Usage with high-level Transport:
//   PacketDispatcher dispatcher;
//   dispatcher.register_handler(0x0105, handler);
//   transport.listen(ep, [&](Packet&& pkt, hl::Endpoint ep) {
//       dispatcher.dispatch(std::move(pkt), ep, transport);
//   }, on_error);
//
// Usage with low-level TcpSession (node daemon):
//   auto result = dispatcher.dispatch_over_session(session);
//   // reads frame, parses packet, dispatches, sends response back

class PacketDispatcher {
public:
    using HandlerFunc = std::function<Result<void>(Packet&&, const hl::Endpoint&, hl::Transport&)>;

    PacketDispatcher() = default;

    void register_handler(uint32_t opcode_id, HandlerFunc handler);
    void unregister_handler(uint32_t opcode_id);
    bool has_handler(uint32_t opcode_id) const;

    // Dispatch a packet to its registered handler (high-level transport).
    Result<void> dispatch(Packet&& pkt, const hl::Endpoint& remote, hl::Transport& transport);

    // Dispatch a frame from a low-level TcpSession.
    // Reads one frame, parses as Packet, dispatches, sends response back.
    // Returns error if no handler registered or read/write fails.
    Result<void> dispatch_session(TcpSession& session, const hl::Endpoint& remote);

private:
    std::unordered_map<uint32_t, HandlerFunc> handlers_;
};

} // namespace smo::network
