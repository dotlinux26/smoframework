#pragma once

#include "protocol/packet/packet.h"
#include "transport/transport.h"
#include "core/transport/transport.hpp"
#include "core/errors/error.hpp"
#include "core/types.hpp"

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
    // RawHandler receives raw unframed bytes for protocols that don't use the Packet format
    using RawHandler = std::function<Result<void>(BytesView, TcpSession&, const hl::Endpoint&)>;

    PacketDispatcher() = default;

    void register_handler(uint32_t opcode_id, HandlerFunc handler);
    void unregister_handler(uint32_t opcode_id);
    bool has_handler(uint32_t opcode_id) const;

    // Register a fallback handler for raw (non-Packet) data.
    // Called when data cannot be unframed or parsed as a Packet.
    void register_raw_handler(RawHandler handler);

    // Dispatch a packet to its registered handler (high-level transport).
    Result<void> dispatch(Packet&& pkt, const hl::Endpoint& remote, hl::Transport& transport);

    // Dispatch a frame from a low-level TcpSession.
    // First tries framed Packet format; if that fails, falls back to raw handler.
    // Returns error if both fail or read/write fails.
    Result<void> dispatch_session(TcpSession& session, const hl::Endpoint& remote);

private:
    std::unordered_map<uint32_t, HandlerFunc> handlers_;
    RawHandler raw_handler_;
};

} // namespace smo::network
