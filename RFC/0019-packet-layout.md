# RFC 0019 — Packet Layout

## Status
DRAFT — pending review.

## Problem
Every SMO node communicates over the wire using a binary packet format. This format must be frozen at the protocol level — changing it later requires a protocol version bump and affects all nodes. The layout must be fixed-width header, minimal parsing overhead, forward-compatible, and independent of transport.

## Decisions

### 1. Fixed 37-byte header, variable payload, 64-byte signature
Total minimum: 101 bytes (37 header + 0 payload + 64 signature). Maximum payload: 65,507 bytes (16-bit length field).

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| PROTOCOL_VERSION |    SUITE_ID    |   NAMESPACE   |           |
|       (1)        |      (1)       |      (1)      |  MESSAGE_ID (2)   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        SESSION ID                              |
|                          (16 bytes)                            |
|                                                               |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       TIMESTAMP (8 bytes)                      |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        NONCE (8 bytes)                         |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|        PAYLOAD LENGTH (2)      |                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               +
|                                                               |
|                       PAYLOAD (variable)                       |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                       SIGNATURE (64 bytes)                     |
|                                                               |
|                                                               |
|                                                               |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### 2. Field definitions

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 1 | protocol_version | Wire protocol version (currently 0x03) |
| 1 | 1 | suite_id | Crypto Suite ID used for signing |
| 2 | 1 | namespace | 0x01=DISCOVERY, 0x02=CONTROL, 0x03=EXECUTION, 0x04=DATA |
| 3 | 2 | message_id | Opcode within namespace (big-endian) |
| 5 | 16 | session_id | 128-bit session identifier |
| 21 | 8 | timestamp | Unix nanoseconds (big-endian) |
| 29 | 8 | nonce | Random nonce for replay protection |
| 37 | 2 | payload_length | Payload size in bytes (big-endian, max 65535) |
| 39 | N | payload | Message payload (0 to 65535 bytes) |
| 39+N | 64 | signature | Suite-specific signature over bytes 0 to 38+N |

### 3. Wire protocol rules (frozen)

1. Every packet MUST carry a nonce. Zero-filled nonces are REJECTED.
2. Every packet MUST be signed. Zero-filled signatures are REJECTED.
3. Timestamp is Unix nanoseconds. Receivers REJECT packets with |now - timestamp| > configured window (default 300 seconds).
4. Payload MAY be encrypted at the session level. Header fields are never encrypted.
5. `protocol_version` is set at connection handshake, not per-packet. All packets in a session use the same version.
6. `suite_id` is set at session negotiation. All packets in a session use the same Suite ID.

### 4. Zero-copy parsing
The header is designed for in-place parsing from a receive buffer:
```cpp
struct PacketHeader {
    uint8_t  protocol_version;
    uint8_t  suite_id;
    uint8_t  namespace;
    uint16_t message_id;      // big-endian
    uint8_t  session_id[16];
    uint64_t timestamp;        // big-endian
    uint64_t nonce;            // big-endian
    uint16_t payload_length;   // big-endian
} __attribute__((packed));
```
`PacketHeader` + `payload_length` bytes = complete packet. The signature starts at offset `39 + payload_length`.

### 5. Future extensibility
- `protocol_version` bump allows complete format change.
- Within the same version, new `namespace` values can be allocated (0x05-0xFF reserved).
- Within the same namespace, new `message_id` values can be allocated.
- Payload schema is per-message-type, not per-packet-format. The packet format never changes when a new message type is added.

## Consequences
- Packet layout is frozen at 37-byte header. No future version of SMO 3.x will change this layout.
- Zero-copy parsing from wire buffer — no per-packet heap allocation.
- 16-byte session ID is large enough for random collision resistance.
- 8-byte nonce is sufficient for replay protection (combined with per-sender sequence tracking).
- 64-byte signature slot covers all Suite 1-3 signing algorithms (Ed25519=64, ML-DSA-65=3309 but post-MVP).
