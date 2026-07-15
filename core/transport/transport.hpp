#pragma once

#include "../errors/error.hpp"
#include "../types.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace smo {

// ---------------------------------------------------------------------------
// Transport error codes (300-323 per SPEC §XIV)
// ---------------------------------------------------------------------------
namespace TransportErrc {
    inline constexpr ErrorCode
    ConnectionRefused(ErrorCategory::Transport, 300, Severity::Error, RetryClass::RetryBackoff, Recovery::Reconnect);
    inline constexpr ErrorCode
    ConnectionTimeout(ErrorCategory::Transport, 301, Severity::Error, RetryClass::RetryBackoff, Recovery::Reconnect);
    inline constexpr ErrorCode
    ConnectionReset(ErrorCategory::Transport, 302, Severity::Warn, RetryClass::RetrySafe, Recovery::Reconnect);
    inline constexpr ErrorCode
    ConnectionClosed(ErrorCategory::Transport, 303, Severity::Info, RetryClass::NoRetry, Recovery::None);
    inline constexpr ErrorCode
    WriteFailed(ErrorCategory::Transport, 304, Severity::Error, RetryClass::RetrySafe, Recovery::Reconnect);
    inline constexpr ErrorCode
    ReadFailed(ErrorCategory::Transport, 305, Severity::Error, RetryClass::RetrySafe, Recovery::Reconnect);
    inline constexpr ErrorCode
    ListenFailed(ErrorCategory::Transport, 306, Severity::Error, RetryClass::NoRetry, Recovery::RestartFSM);
    inline constexpr ErrorCode
    AddressInUse(ErrorCategory::Transport, 307, Severity::Warn, RetryClass::NoRetry, Recovery::RestartFSM);
    inline constexpr ErrorCode
    DnsResolveFailed(ErrorCategory::Transport, 308, Severity::Error, RetryClass::NoRetry, Recovery::Reconnect);
    inline constexpr ErrorCode
    FrameTooLarge(ErrorCategory::Transport, 309, Severity::Warn, RetryClass::NoRetry, Recovery::RestartFSM);
    inline constexpr ErrorCode
    FrameTruncated(ErrorCategory::Transport, 310, Severity::Warn, RetryClass::NoRetry, Recovery::RestartFSM);
    inline constexpr ErrorCode
    VersionNegotiationFailed(ErrorCategory::Transport, 312, Severity::Error, RetryClass::NoRetry, Recovery::Reconnect);
    inline constexpr ErrorCode
    VersionMismatch(ErrorCategory::Transport, 313, Severity::Warn, RetryClass::NoRetry, Recovery::Reenroll);
    inline constexpr ErrorCode
    BufferExhausted(ErrorCategory::Transport, 314, Severity::Error, RetryClass::RetryBackoff, Recovery::RestartFSM);
} // namespace TransportErrc

// ---------------------------------------------------------------------------
// Endpoint — identifies a transport endpoint by scheme, host, port, path
// ---------------------------------------------------------------------------
struct Endpoint {
    std::string scheme;   // "tcp", "udp", "unix", "quic"
    std::string host;     // IPv4, IPv6, or hostname
    uint16_t    port{0};
    std::string path;     // for Unix sockets

    std::string to_string() const;
    static Result<Endpoint> from_string(std::string_view s);
};

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
class TransportSession;
class TransportListener;
class Transport;

using SessionPtr = std::unique_ptr<TransportSession>;
using ListenerPtr = std::unique_ptr<TransportListener>;
using TransportPtr = std::unique_ptr<Transport>;

// ---------------------------------------------------------------------------
// TransportSession — a bidirectional byte stream
// ---------------------------------------------------------------------------
class TransportSession {
public:
    virtual ~TransportSession() = default;

    // Send bytes. Returns error on failure.
    virtual Result<void> send(BytesView data) = 0;

    // Receive up to max_bytes. Blocks until data arrives or error.
    virtual Result<Bytes> recv(size_t max_bytes = 65536) = 0;

    // Close the session gracefully.
    virtual Result<void> close() = 0;

    // Remote endpoint of this session.
    virtual Endpoint remote_endpoint() const = 0;

    // Whether the session is still open.
    virtual bool is_open() const = 0;
};

// ---------------------------------------------------------------------------
// TransportListener — accepts incoming connections
// ---------------------------------------------------------------------------
class TransportListener {
public:
    virtual ~TransportListener() = default;

    // Block until a new connection arrives, then return a TransportSession.
    virtual Result<SessionPtr> accept() = 0;

    // Stop listening and close the listener.
    virtual Result<void> close() = 0;

    // The local endpoint we are listening on.
    virtual Endpoint local_endpoint() const = 0;
};

// ---------------------------------------------------------------------------
// Transport — a transport protocol implementation
// ---------------------------------------------------------------------------
class Transport {
public:
    virtual ~Transport() = default;

    // Human-readable name (e.g., "TCP", "UDP").
    virtual std::string_view name() const = 0;

    // Listen on the given endpoint. Returns a Listener.
    virtual Result<ListenerPtr> listen(const Endpoint& ep) = 0;

    // Connect to a remote endpoint. Returns a Session.
    virtual Result<SessionPtr> connect(const Endpoint& ep) = 0;
};

// ---------------------------------------------------------------------------
// TransportRegistry — scheme-based transport lookup
// ---------------------------------------------------------------------------
class TransportRegistry {
public:
    // Register a transport implementation for its scheme(s).
    void register_transport(TransportPtr transport, std::string scheme);



    // Look up a transport by scheme.
    Transport* get(std::string_view scheme) const;

    // Convenience: connect to an endpoint by resolving scheme from registry.
    Result<SessionPtr> connect(const Endpoint& ep);

    // Global singleton.
    static TransportRegistry& instance();

private:
    std::unordered_map<std::string, TransportPtr, std::hash<std::string>, std::equal_to<>> transports_;
};

} // namespace smo
