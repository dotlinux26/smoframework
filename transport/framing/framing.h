#pragma once

#include <cstdint>
#include <vector>
#include <span>

namespace smo::hl {

// §VI — Message framing over stream transports.
//
// Zero-copy invariant: frames are parsed in-place from a contiguous buffer.

struct FrameHeader {
    uint32_t magic{0x534D4F01};     // "SMO\1"
    uint32_t payload_len{0};
    uint8_t  flags{0};
} __attribute__((packed));

static_assert(sizeof(FrameHeader) == 9);

// Read one frame from a stream buffer.
// Returns the number of bytes consumed, or 0 if incomplete.
// On success, out references the payload inside the input span.
size_t frame_read(std::span<const uint8_t> buf, std::span<const uint8_t>& out);

// Prepend a frame header to a payload.
void frame_write(std::span<const uint8_t> payload, std::vector<uint8_t>& out);

} // namespace smo::hl
