#include "packet.h"

#include "../../core/types.hpp"
#include <cstring>

namespace smo {

namespace {

void write_u32(Bytes& out, uint32_t v) {
    for (int i = 3; i >= 0; --i)
        out.push_back(static_cast<uint8_t>(v >> (i * 8)));
}

void write_u64(Bytes& out, uint64_t v) {
    for (int i = 7; i >= 0; --i)
        out.push_back(static_cast<uint8_t>(v >> (i * 8)));
}

uint32_t read_u32(BytesView data, size_t& off) {
    uint32_t v = 0;
    for (int i = 0; i < 4 && off < data.size(); ++i)
        v = (v << 8) | data[off++];
    return v;
}

uint64_t read_u64(BytesView data, size_t& off) {
    uint64_t v = 0;
    for (int i = 0; i < 8 && off < data.size(); ++i)
        v = (v << 8) | data[off++];
    return v;
}

} // anonymous namespace

static constexpr uint8_t kExpectedVersion = 1;
static constexpr size_t kFixedSize = sizeof(PacketHeader) + 16 + 16 + 4 + 8 + 8 + 4 + 64;

Result<Packet> packet_from_buffer(std::span<const uint8_t> wire) {
    if (wire.size() < kFixedSize) {
        return SMO_ERR_PROTOCOL(600, Error, NoRetry, None,
                                "wire buffer too short for packet header");
    }

    Packet pkt{};
    size_t off = 0;

    if (off + sizeof(PacketHeader) > wire.size()) {
        return SMO_ERR_PROTOCOL(600, Error, NoRetry, None,
                                "truncated packet header");
    }
    std::memcpy(&pkt.header, wire.data() + off, sizeof(PacketHeader));
    off += sizeof(PacketHeader);

    if (pkt.header.version != kExpectedVersion) {
        return SMO_ERR_PROTOCOL(601, Error, NoRetry, None,
                                "unsupported packet version");
    }

    std::memcpy(pkt.session_id.data(), wire.data() + off, 16);
    off += 16;
    std::memcpy(pkt.intent_id.data(), wire.data() + off, 16);
    off += 16;

    pkt.opcode_id = read_u32(wire, off);
    pkt.timestamp  = static_cast<int64_t>(read_u64(wire, off));
    std::memcpy(pkt.nonce.data(), wire.data() + off, 8);
    off += 8;

    uint32_t payload_len = read_u32(wire, off);
    if (off + payload_len + 64 > wire.size()) {
        return SMO_ERR_PROTOCOL(602, Error, NoRetry, None,
                                "payload length exceeds wire buffer");
    }

    pkt.payload.assign(wire.begin() + static_cast<ptrdiff_t>(off),
                       wire.begin() + static_cast<ptrdiff_t>(off + payload_len));
    off += payload_len;

    std::memcpy(pkt.signature.data(), wire.data() + off, 64);
    off += 64;

    return pkt;
}

Result<void> packet_to_buffer(const Packet& pkt, std::vector<uint8_t>& out) {
    out.clear();

    if (pkt.header.version != kExpectedVersion) {
        return SMO_ERR_PROTOCOL(601, Error, NoRetry, None,
                                "unsupported packet version to serialize");
    }
    if (pkt.payload.size() > 65535) {
        return SMO_ERR_PROTOCOL(602, Error, NoRetry, None,
                                "payload too large");
    }

    out.insert(out.end(), reinterpret_cast<const uint8_t*>(&pkt.header),
               reinterpret_cast<const uint8_t*>(&pkt.header) + sizeof(PacketHeader));

    out.insert(out.end(), pkt.session_id.begin(), pkt.session_id.end());
    out.insert(out.end(), pkt.intent_id.begin(), pkt.intent_id.end());
    write_u32(out, pkt.opcode_id);
    write_u64(out, static_cast<uint64_t>(pkt.timestamp));
    out.insert(out.end(), pkt.nonce.begin(), pkt.nonce.end());

    write_u32(out, static_cast<uint32_t>(pkt.payload.size()));
    out.insert(out.end(), pkt.payload.begin(), pkt.payload.end());

    out.insert(out.end(), pkt.signature.begin(), pkt.signature.end());

    return {};
}

} // namespace smo
