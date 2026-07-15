# RFC 0013 — Transport Abstraction

## Status
ACCEPTED — Transport interface frozen. TCP + UDP implementations deferred to Stage 2.

## Problem
SMO must support TCP, UDP, and future transports (QUIC, relay, Unix socket) without the protocol or runtime layers knowing which transport is in use. Transport selection must be a deployment-time or session-time decision, not a compile-time one.

## Decisions

### 1. `Transport` is an abstract base class
The runtime sends and receives opaque byte sequences through the `Transport` interface. Transport handles connection management, framing, and addressing. The runtime never sees file descriptors, socket addresses, or transport-specific errors.

### 2. Two transport roles: Listener and Connector
- **Listener**: binds to an endpoint, accepts incoming connections, produces connected `TransportSession` objects.
- **Connector**: initiates outbound connections, returns a connected `TransportSession`.

### 3. `TransportSession` is a bidirectional byte stream
Once connected, all communication happens through `TransportSession::send(span)`, `TransportSession::recv()`. The session handles framing (length-prefixed for TCP, datagram-aware for UDP). The runtime never manages framing manually.

### 4. Transport attachment at session open
When `Session::open()` is called, the caller specifies which transport to use (by name or by endpoint address scheme). The transport registry maps `tcp://192.168.1.1:8443` to the TCP transport, `udp://10.0.0.5:9000` to UDP, etc.

### 5. FrameHeader is transport-independent but transport-owned
Every transport implements its own framing, but the wire format for SMO packets is identical across transports. A FrameHeader prepends every application message with: total_length (4 bytes), protocol_version (2), flags (2), session_id (16). The transport layer strips this before delivering bytes to the protocol layer.

## Interfaces

```cpp
struct Endpoint {
    std::string scheme;       // "tcp", "udp", "unix", "quic"
    std::string host;
    uint16_t port;
    std::string path;         // for Unix sockets
};

class TransportSession {
    virtual Result<void> send(std::span<const uint8_t> data) = 0;
    virtual Result<std::vector<uint8_t>> recv() = 0;
    virtual Result<void> close() = 0;
    virtual Endpoint remote_endpoint() const = 0;
};

class Transport {
    virtual std::string_view name() const = 0;  // "tcp", "udp"
    virtual Result<std::unique_ptr<TransportSession>> connect(
        const Endpoint& endpoint) = 0;
};

class TransportListener {
    virtual Result<void> listen(const Endpoint& endpoint) = 0;
    virtual Result<std::unique_ptr<TransportSession>> accept() = 0;
    virtual Result<void> close() = 0;
};

class TransportRegistry {
    static TransportRegistry& global();
    void register_transport(std::unique_ptr<Transport> transport);
    Result<Transport*> get(const std::string& scheme);
    Result<std::unique_ptr<TransportSession>> connect(const Endpoint& ep);
};
```

## Consequences
- Adding a new transport (QUIC, relay, Bluetooth) requires implementing 2 classes and registering. Zero changes to protocol, runtime, or FSM code.
- Every transport test can use a loopback TCP transport without starting a mesh. Unit tests never need real network interfaces.
- The protocol layer receives already-framed byte sequences. Frame parsing and reassembly are transport concerns.
- Transport errors are wrapped into SMO error codes (ErrorCategory::Transport) at the transport boundary. The runtime never sees `ECONNREFUSED`.
- Connection retry, backoff, and reconnect logic lives in the transport implementation, not in the session or runtime.
