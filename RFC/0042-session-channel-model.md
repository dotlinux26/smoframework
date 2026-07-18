# RFC 0042 — Transport Session & Channel Model

**Status:** DRAFT — Proposed for Sprint 37  
**Date:** 2026-07-18  
**Extends:** RFC 0014 (Session Lifecycle), RFC 0035 (Runtime Architecture)

---

## 1. Motivation

Current session model is a single flat FSM:

```
TCP Connection → Session FSM → single packet stream
```

This has three limitations:
1. **No multiplexing** — one session cannot carry simultaneous governance, file transfer, and heartbeat traffic
2. **No channel isolation** — a slow file transfer blocks governance packets
3. **No lifecycle hierarchy** — session establishment is conflated with individual operation lifecycle

**What we need:**

```
TCP Connection → Session → Channel 1
                          → Channel 2
                          → Channel 3
                          → Channel 4
```

Each channel is an independent logical stream within a session. A channel carries **any** contract invocation — Join, Governance, File, Recovery — all can run on any channel. Channel is purely a transport concern; contracts are in `RuntimeRequest`.

---

## 2. Design

### 2.1 Four-Layer Hierarchy

```
┌─────────────────────────────────────────────┐
│                Connection                    │
│  TCP socket, UDP association, QUIC stream   │
│  Responsible for: byte transport, keepalive │
├─────────────────────────────────────────────┤
│                 Session                      │
│  Authenticated peer relationship             │
│  Responsible for: identity, crypto, caps     │
├─────────────────────────────────────────────┤
│                 Channel                      │
│  Logical stream within a session            │
│  Responsible for: sequencing, flow control  │
├─────────────────────────────────────────────┤
│               Invocation                     │
│  Single contract execution unit             │
│  Responsible for: request/response matching │
└─────────────────────────────────────────────┘
```

### 2.2 Connection

A `Connection` represents a raw transport link:

```cpp
enum class ConnectionType : uint8_t {
    TCP,
    UDP,
    QUIC,
    Unix
};

struct Connection {
    ConnectionType type;
    std::string remote_addr;
    uint16_t remote_port;
    uint32_t stream_id = 0;     // for QUIC multi-stream
    int64_t established_at;
    int64_t last_activity_at;
};
```

**Connection lifecycle:**
1. `listen()` / `connect()` → established
2. Keepalive (TCP built-in, UDP app-level)
3. `close()` / timeout → closed

Connections are managed by `TransportRegistry`. Sessions are layered on top.

### 2.3 Session (extended)

The existing `Session` class remains, extended with channel support:

```cpp
class Session {
    // Existing fields (from session.hpp):
    SessionId id_;
    SessionState state_;
    NodeID peer_id_;
    Certificate peer_cert_;
    CapabilitySet capabilities_;

    // New fields:
    std::unordered_map<uint16_t, Channel> channels_;
    Connection connection_;
    uint64_t session_timeout_ns = 300'000'000'000;  // 5 min default
    uint64_t rekey_interval_ns = 86400'000'000'000;  // 24h
};
```

**Extended FSM transitions:**

```
Closed → Handshake → Established → Active → Renewing → Established
                       ↓
                   (can open channels)
                       ↓
                 Channel 1 (Open)
                 Channel 2 (Open)
```

Session remains in `Established` or `Active` state as long as at least one channel is active. When all channels close, session may transition to `Closed`.

### 2.4 Channel — Pure Logical Stream

A `Channel` is a logical stream within a session. It does NOT carry contract binding — any contract can run on any channel:

```cpp
enum class ChannelState : uint8_t {
    Closed,
    Opening,
    Open,
    Closing,
};

struct Channel {
    uint16_t channel_id;            // unique within session
    ChannelState state = ChannelState::Open;

    // Ordering: Transport guarantees in-order delivery (TCP by nature, QUIC via
    // stream ID). No runtime-level sequence numbers — the transport layer
    // handles reassembly. Channel only ensures frames are dispatched in the
    // order they arrive within this logical stream.

    // Flow control
    uint64_t window_size = 65536;
    uint64_t bytes_in_flight = 0;

    // Timeouts
    int64_t opened_at;
    int64_t last_activity_at;
    uint64_t idle_timeout_ns = 300'000'000'000;  // 5 min

    // Back-reference
    Session* session = nullptr;
};
```

**Key design: Channel has no `type`, no `purpose`, no `contract_id`.** It is a pure stream. Contracts are carried inside `RuntimeRequest` on any channel. This decouples transport from application logic.

Example of channel reuse:
```
Channel 5
  → Invocation 1: Join
  → Invocation 2: Governance (vote)
  → Invocation 3: Recovery
  → Invocation 4: File (put)
```

**Channel lifecycle (lazy creation):**

```
New packet with unknown channel_id
       ↓
Create Channel(channel_id, state = Open)
       ↓
Dispatch packet through channel

Channel close:
  idle_timeout → Close
  session.close() → Close all channels
```

Channels are created implicitly on the first packet with a given `channel_id` — no CHANNEL_OPEN/ACK handshake. This matches HTTP/2 stream semantics: a receiver sees `STREAM_ID=N` and creates the stream if it does not exist. There is no need for explicit open frames; the presence of data on a new channel IS the open signal.

### 2.5 Channel Multiplexing

Multiple channels share a single TCP connection:

```
TCP Stream:
┌──────────┬──────────┬──────────┬──────────┐
│ Channel 1│ Channel 2│ Channel 1│ Channel 3│ ...
│ request  │ request  │ response │ request  │
└──────────┴──────────┴──────────┴──────────┘
```

**Wire format (per frame):**

```
┌───────────────────────────────────────────────┐
│ Frame Header (9 bytes) : len + flags + type   │
├───────────────────────────────────────────────┤
│ Channel ID (2 bytes)                          │
│ Payload (variable) — CBOR-encoded Packet     │
└───────────────────────────────────────────────┘
```

No sequence number on the wire — the transport (TCP or QUIC) guarantees in-order delivery within the byte stream. Channel dispatch preserves arrival order per channel_id.

This reuses the existing `frame_write`/`frame_read` from `core/transport/framing.hpp` with a new `kFrameFlagChannel` flag.

### 2.6 Flow Control

Each channel implements a simple sliding window:

```cpp
struct ChannelFlowControl {
    uint64_t window_size;
    uint64_t window_remaining;

    bool can_send(uint64_t size) const {
        return size <= window_remaining;
    }
    void on_send(uint64_t size) {
        window_remaining -= size;
    }
    void on_window_update(uint64_t increment) {
        window_remaining += increment;
    }
};
```

Window updates are sent as special control frames (WINDOW_UPDATE) on the same channel.

---

## 3. Session Manager (Extended)

The existing `SessionManager` is extended to manage channels:

```cpp
class SessionManager {
    // Existing:
    Result<Session*> open(Session session);
    Session* lookup(const SessionId& id);
    Result<void> close(const SessionId& id, int64_t now);
    void tick(int64_t now);
    void collect_garbage();

    // New:
    Result<Channel*> open_channel(SessionId session_id);
    Result<void> close_channel(SessionId session_id, uint16_t channel_id);
    Channel* lookup_channel(SessionId session_id, uint16_t channel_id);
    std::vector<Channel*> list_channels(SessionId session_id);

    // GC for idle channels
    void tick_channels(int64_t now);

private:
    std::unordered_map<uint64_t, Session> sessions_;
};
```

---

## 4. Wire Protocol

### 4.1 Channel Creation (Lazy)

No explicit CHANNEL_OPEN/ACK frames. A channel is created when the first data frame arrives with an unknown `channel_id`:

```
Node A → Node B:
  Frame { channel_id=7, payload = ... }  // channel 7 doesn't exist → auto-create
```

Both sides independently create the channel on first encounter. No handshake needed.

### 4.2 Channel Close

Channels close implicitly on idle timeout (default 5 min). Explicit close is optional via a control frame:

```
Node A → Node B:
  Frame { CHANNEL_CLOSE, channel_id=7, payload = { reason_code } }
```

The recipient removes the channel state. No CLOSE_ACK needed — channels are cheap to recreate.

### 4.3 Window Update

```
Node A → Node B:
  Frame { WINDOW_UPDATE, channel_id, payload = { increment } }
```

---

## 5. Invocation: Request/Response over Channel

An invocation is a single request-response exchange over a channel:

```

Channel 5:
  → Frame { channel_id=5, seq=1, payload = REQUEST (Packet) }
  ← Frame { channel_id=5, seq=2, payload = RESPONSE (Packet) }
```

Multiple invocations can be pipelined on the same channel. Correlation is done via `intent_id` + `session_id` inside the Packet (see RFC 0041).

---

## 6. Consequences

### Positive
- **Multiplexed** — governance, file, and heartbeat share one TCP connection
- **Isolated** — a slow file transfer does not block governance
- **Lifecycle hierarchy** — connection outlives session, session outlives channel
- **Flow control per channel** — prevents one channel from starving others
- **Channel is pure stream** — no contract binding, fully decoupled from application
- **Lazy creation** — no explicit open/close handshake; channels are created on first data frame (like HTTP/2)
- **No runtime sequencing overhead** — transport handles ordering; no per-channel sequence tracking

### Negative
- **Protocol complexity** — channel open/close/flow control add wire overhead
- **Backward incompatible** — existing sessions do not support channels; requires negotiation

---

## 7. Migration Path

1. Add `Channel` struct (no seq_in/seq_out, lazy creation) to `session.hpp`
2. Extend `Session` with `channels_` map and `connection_` field
3. Extend `SessionManager` with channel CRUD operations (auto-create on unknown channel_id)
4. Add channel frame header to `framing.hpp` (channel_id field, no sequence number)
5. Wire lazy channel creation into TCP accept loop: on first frame with unknown channel_id → auto-create Channel
6. Wire channels into PacketDispatcher → RuntimeBridge pipeline

---

## 8. Files Affected

| File | Change |
|------|--------|
| `core/session/session.hpp` | Add Channel struct, extended Session, Connection |
| `core/session/session.cpp` | Channel FSM, extended SessionManager |
| `core/transport/framing.hpp` | Add channel_id field to frame header, WINDOW_UPDATE frame type |
| `core/network/packet_dispatcher.cpp` | Route packets through channels |
| `core/runtime/runtime_bridge.hpp` | (unchanged — bridge sees Packets, not channels) |

---

## References

- [RFC 0014 — Session Lifecycle](../RFC/0014-session-lifecycle.md)
- [RFC 0035 — Runtime Architecture](../RFC/0035-runtime-architecture.md)
- [RFC 0041 — Runtime Bridge](../RFC/0041-runtime-bridge.md)
- [Discussion 0037 — Wiring Bridge](../docs/discussions/DISCUSSION_0037_WIRING_BRIDGE.md)
