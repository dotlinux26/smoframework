#include "packet_dispatcher.hpp"
#include "core/transport/framing.hpp"
#include "protocol/packet/packet.h"

namespace smo::network {

// ── SessionTransport — wraps a single TransportSession as hl::Transport ─

class SessionTransport final : public hl::Transport {
public:
    SessionTransport(TransportSession& session, const hl::Endpoint& remote)
        : session_(&session), remote_(remote) {}

    std::error_code listen(const hl::Endpoint&,
                            hl::Transport::PacketHandler,
                            hl::Transport::ErrorHandler) override
    { return {}; } // not used

    std::error_code connect(const hl::Endpoint&) override
    { return {}; } // not used

    std::error_code send(Packet&& pkt, const hl::Endpoint&) override {
        // Serialize packet
        std::vector<uint8_t> buf;
        auto pack_res = packet_to_buffer(pkt, buf);
        if (!pack_res) {
            return std::make_error_code(std::errc::invalid_argument);
        }

        // Frame the data
        Bytes framed;
        frame_write(BytesView(buf.data(), buf.size()), kFrameFlagNone, framed);

        // Send
        auto send_res = session_->send(framed);
        if (!send_res) {
            return std::make_error_code(std::errc::io_error);
        }
        return {};
    }

    void close() noexcept override {}

private:
    TransportSession* session_;
    hl::Endpoint remote_;
};

// ── PacketDispatcher ──────────────────────────────────────────────────

void PacketDispatcher::register_handler(uint32_t opcode_id, HandlerFunc handler) {
    handlers_[opcode_id] = std::move(handler);
}

void PacketDispatcher::unregister_handler(uint32_t opcode_id) {
    handlers_.erase(opcode_id);
}

bool PacketDispatcher::has_handler(uint32_t opcode_id) const {
    return handlers_.find(opcode_id) != handlers_.end();
}

void PacketDispatcher::register_raw_handler(RawHandler handler) {
    raw_handler_ = std::move(handler);
}

Result<void> PacketDispatcher::dispatch(Packet&& pkt, const hl::Endpoint& remote, hl::Transport& transport) {
    // Node lifecycle state check
    if (lifecycle_fsm_) {
        auto state_check = lifecycle_fsm_->check_opcode_allowed(pkt.opcode_id);
        if (!state_check) return state_check;
    }

    auto it = handlers_.find(pkt.opcode_id);
    if (it == handlers_.end()) {
        return SMO_ERR_PROTOCOL(604, Error, NoRetry, None,
                                "No handler registered for opcode 0x" +
                                std::to_string(pkt.opcode_id));
    }
    return it->second(std::move(pkt), remote, transport);
}

Result<void> PacketDispatcher::dispatch_session(TransportSession& session, const hl::Endpoint& remote) {
    // 1. Read raw framed data
    auto data = session.recv(65536);
    if (!data) {
        return SMO_ERR_TRANSPORT(304, Error, RetrySafe, None,
                                "Failed to read from session");
    }
    Bytes raw = std::move(data.value());

    // 2. Try framed Packet format first
    FrameHeader fh;
    BytesView payload;
    size_t frame_sz = frame_read(raw, fh, payload);
    if (frame_sz > 0) {
        // 3. Parse as Packet
        auto pkt = packet_from_buffer(payload);
        if (pkt) {
            // 3a. Node lifecycle state check
            if (lifecycle_fsm_) {
                auto state_check = lifecycle_fsm_->check_opcode_allowed(pkt.value().opcode_id);
                if (!state_check) return state_check;
            }
            // 4. Look up handler
            auto it = handlers_.find(pkt.value().opcode_id);
            if (it == handlers_.end()) {
                return SMO_ERR_PROTOCOL(604, Error, NoRetry, None,
                                        "No handler for opcode 0x" +
                                        std::to_string(pkt.value().opcode_id));
            }
            // 5. Create transport adapter and dispatch
            SessionTransport transport_adapter(session, remote);
            return it->second(std::move(pkt.value()), remote, transport_adapter);
        }
        // framed but not a valid Packet — fall through to raw handler
    }

    // 6. Fallback: raw (non-Packet) protocol handler
    if (raw_handler_) {
        return raw_handler_(raw, session, remote);
    }

    if (frame_sz == 0) {
        return SMO_ERR_TRANSPORT(309, Error, RetrySafe, None,
                                "Failed to unframe data");
    }
    return SMO_ERR_PROTOCOL(600, Error, NoRetry, None,
                            "Failed to parse packet");
}

} // namespace smo::network
