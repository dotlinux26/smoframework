#include "framing.h"
#include <cstring>

namespace smo::hl {

size_t frame_read(std::span<const uint8_t> buf, std::span<const uint8_t>& out) {
    if (buf.size() < sizeof(FrameHeader)) return 0;

    FrameHeader hdr;
    std::memcpy(&hdr, buf.data(), sizeof(hdr));

    if (hdr.magic != 0x534D4F01) return 0;

    size_t total = sizeof(hdr) + hdr.payload_len;
    if (buf.size() < total) return 0;

    out = buf.subspan(sizeof(hdr), hdr.payload_len);
    return total;
}

void frame_write(std::span<const uint8_t> payload, std::vector<uint8_t>& out) {
    FrameHeader hdr;
    hdr.magic = 0x534D4F01;
    hdr.payload_len = static_cast<uint32_t>(payload.size());
    hdr.flags = 0;

    out.resize(sizeof(hdr) + payload.size());
    std::memcpy(out.data(), &hdr, sizeof(hdr));
    if (!payload.empty()) {
        std::memcpy(out.data() + sizeof(hdr), payload.data(), payload.size());
    }
}

} // namespace smo::hl