#pragma once

#include "../errors/error.hpp"
#include "../types.hpp"

#include <cstdint>

namespace smo {

// FrameHeader: magic(4) + payload_len(4) + flags(1) = 9 bytes
struct FrameHeader {
    uint32_t magic{0x534D4F01};     // "SMO\1"
    uint32_t payload_len{0};
    uint8_t  flags{0};
} __attribute__((packed));

static_assert(sizeof(FrameHeader) == 9, "FrameHeader must be 9 bytes");

// Flags
inline constexpr uint8_t kFrameFlagNone     = 0x00;
inline constexpr uint8_t kFrameFlagVersion  = 0x01;  // version handshake frame
inline constexpr uint8_t kFrameFlagClose    = 0x02;  // graceful close

// Write a frame: header + payload. Allocates and returns the framed bytes.
void frame_write(BytesView payload, uint8_t flags, Bytes& out);

// Read a frame from buf. On success returns the frame size (header + payload).
// Returns 0 if the buffer is too small to contain a complete frame.
// Fills out_header and out_payload with the parsed frame.
// out_payload references the input buffer (zero-copy).
size_t frame_read(BytesView buf, FrameHeader& out_header, BytesView& out_payload);

// Version handshake constants
inline constexpr uint32_t kTransportVersion = 1;

// Perform a version handshake on an already-connected socket fd.
// Returns the negotiated version on success.
Result<uint32_t> version_handshake_client(int fd);
Result<uint32_t> version_handshake_server(int fd);

} // namespace smo
