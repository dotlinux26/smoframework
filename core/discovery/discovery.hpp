#pragma once

#include "../certificate/certificate.hpp"
#include "../errors/error.hpp"
#include "../identity/identity.hpp"
#include "../transport/transport.hpp"
#include "../types.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace smo {

// ===========================================================================
// Discovery error codes (400-409)
// ===========================================================================
namespace DiscoveryErrc {
    inline constexpr ErrorCode
    PeerNotFound(ErrorCategory::Discovery, 400, Severity::Info, RetryClass::RetrySafe, Recovery::None);
    inline constexpr ErrorCode
    PeerUnreachable(ErrorCategory::Discovery, 401, Severity::Warn, RetryClass::RetryBackoff, Recovery::None);
    inline constexpr ErrorCode
    PeerSuspect(ErrorCategory::Discovery, 402, Severity::Info, RetryClass::RetrySafe, Recovery::None);
    inline constexpr ErrorCode
    PeerTimeout(ErrorCategory::Discovery, 403, Severity::Warn, RetryClass::NoRetry, Recovery::None);
    inline constexpr ErrorCode
    MembershipTableFull(ErrorCategory::Discovery, 405, Severity::Warn, RetryClass::NoRetry, Recovery::None);
    inline constexpr ErrorCode
    BootstrapNoSeeds(ErrorCategory::Discovery, 406, Severity::Error, RetryClass::RetryBackoff, Recovery::ManualIntervention);
} // namespace DiscoveryErrc

// ===========================================================================
// PeerState
// ===========================================================================
enum class PeerState : uint8_t {
    Unknown = 0,
    Online  = 1,
    Suspect = 2,
    Offline = 3,
};

const char* to_string(PeerState s) noexcept;

// ===========================================================================
// PeerRecord
// ===========================================================================
struct PeerRecord {
    NodeID    node_id;
    std::string display_name;       // human-friendly name (from cert)
    std::string hostname;           // OS hostname
    std::string mesh_name;          // mesh this record belongs to
    Role      role = Role::Reader;  // from Membership Certificate
    std::vector<std::string> tags;  // user-defined tags
    std::string platform;           // "linux", "windows", ...
    std::string arch;               // "x86_64", "arm64", ...
    std::string version;            // SMO runtime version
    std::string location;           // optional physical/logical location
    std::vector<std::string> aliases; // alternative names
    Endpoint  endpoint;
    PeerState state = PeerState::Unknown;
    int64_t   last_seen = 0;
    int       ping_misses = 0;
    double    rtt_ms = 0.0;        // moving average RTT

    Bytes serialize() const;
    static Result<PeerRecord> deserialize(BytesView data);
};

// ===========================================================================
// MembershipTable
// ===========================================================================
class MembershipTable {
public:
    MembershipTable() = default;
    explicit MembershipTable(size_t capacity) : capacity_(capacity) {}

    Result<void> upsert(PeerRecord record);
    Result<PeerRecord> lookup(const NodeID& id) const;
    Result<PeerRecord> lookup_by_name(const std::string& name) const;
    std::vector<PeerRecord> peers() const;
    std::vector<PeerRecord> peers_with_state(PeerState state) const;

    // Filter methods for Selection Engine
    std::vector<PeerRecord> peers_by_role(Role role) const;
    std::vector<PeerRecord> peers_by_tag(const std::string& tag) const;
    std::vector<PeerRecord> peers_by_os(const std::string& os) const;
    std::vector<PeerRecord> peers_by_arch(const std::string& arch) const;
    std::vector<PeerRecord> peers_by_mesh(const std::string& mesh_name) const;

    Result<void> remove(const NodeID& id);
    size_t count() const noexcept { return records_.size(); }
    size_t capacity() const noexcept { return capacity_; }
    void set_capacity(size_t cap) noexcept { capacity_ = cap; }

    Bytes serialize() const;
    static Result<MembershipTable> deserialize(BytesView data);

private:
    static uint64_t to_key(const NodeID& id);

    std::unordered_map<uint64_t, PeerRecord> records_;
    size_t capacity_ = 1000;
};

// ===========================================================================
// HealthMonitor
// ===========================================================================
class HealthMonitor {
public:
    void record_ping(const NodeID& id, int64_t now);
    Result<void> record_pong(const NodeID& id, int64_t now);

    // Advance time; mark peers as Suspect/Offline based on misses
    void tick(MembershipTable& table, int64_t now,
              int64_t ping_timeout_ns = 5000000000LL, // 5s
              int max_misses = 3);

    PeerState state(const NodeID& id) const;
    int ping_misses(const NodeID& id) const;

private:
    struct PingRecord {
        int64_t sent_at;
        int misses;
    };
    static uint64_t to_key(const NodeID& id);
    std::unordered_map<uint64_t, PingRecord> pings_;
};

// ===========================================================================
// Discovery protocol messages (HELLO, WELCOME, PING, PONG, DISCOVER,
// NODE_INFO, OFFLINE)
// ===========================================================================
struct HelloMsg {
    NodeID    node_id;
    uint64_t  pubkey_fingerprint = 0;  // first 8 bytes of pubkey hash
    uint16_t  protocol_version = 1;

    Bytes serialize() const;
    static Result<HelloMsg> deserialize(BytesView data);
};

struct WelcomeMsg {
    NodeID     node_id;
    PeerRecord peer_record;

    Bytes serialize() const;
    static Result<WelcomeMsg> deserialize(BytesView data);
};

struct PingMsg {
    int64_t timestamp = 0;

    Bytes serialize() const;
    static Result<PingMsg> deserialize(BytesView data);
};

struct PongMsg {
    int64_t timestamp = 0;  // echo of PingMsg.timestamp

    Bytes serialize() const;
    static Result<PongMsg> deserialize(BytesView data);
};

struct DiscoverMsg {
    Bytes serialize() const;
    static Result<DiscoverMsg> deserialize(BytesView data);
};

struct NodeInfoMsg {
    PeerRecord peer_record;

    Bytes serialize() const;
    static Result<NodeInfoMsg> deserialize(BytesView data);
};

struct OfflineMsg {
    NodeID  node_id;
    uint8_t reason = 0;

    Bytes serialize() const;
    static Result<OfflineMsg> deserialize(BytesView data);
};

// ===========================================================================
// Bootstrap
// ===========================================================================
class Bootstrap {
public:
    // Try each seed endpoint until one responds with WELCOME.
    // Returns the first peer that responds.
    static Result<PeerRecord> find_seed(
        const std::vector<Endpoint>& seeds,
        Transport& transport,
        const NodeID& local_id,
        int64_t now);
};

// ===========================================================================
// DiscoveryEngine
// ===========================================================================
class DiscoveryEngine {
public:
    DiscoveryEngine(MembershipTable& table, HealthMonitor& monitor, Transport& transport);

    // Handle incoming discovery messages
    Result<void> handle_hello(const HelloMsg& msg, const Endpoint& from, int64_t now);
    Result<void> handle_welcome(const WelcomeMsg& msg, int64_t now);
    Result<void> handle_ping(const PingMsg& msg, int64_t now);
    Result<void> handle_pong(const PongMsg& msg, int64_t now);
    Result<void> handle_discover(const DiscoverMsg& msg, const Endpoint& from, int64_t now);
    Result<void> handle_node_info(const NodeInfoMsg& msg, int64_t now);
    Result<void> handle_offline(const OfflineMsg& msg, int64_t now);

    // Periodic maintenance
    void tick(int64_t now);

    const MembershipTable& table() const noexcept { return table_; }
    const HealthMonitor& monitor() const noexcept { return monitor_; }

private:
    MembershipTable& table_;
    HealthMonitor& monitor_;
    Transport& transport_;

    void send_node_info(const Endpoint& to) const;
};

} // namespace smo
