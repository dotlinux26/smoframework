#include "discovery.hpp"

#include <cstring>
#include <limits>

namespace smo {

// ===========================================================================
// Big-endian helpers
// ===========================================================================
namespace {

void write_u64(Bytes& out, uint64_t v) {
    for (int i = 7; i >= 0; --i)
        out.push_back(static_cast<uint8_t>(v >> (i * 8)));
}

void write_u32(Bytes& out, uint32_t v) {
    for (int i = 3; i >= 0; --i)
        out.push_back(static_cast<uint8_t>(v >> (i * 8)));
}

void write_u16(Bytes& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

uint64_t read_u64(BytesView& data, size_t& offset) {
    uint64_t v = 0;
    for (int i = 0; i < 8 && offset < data.size(); ++i)
        v = (v << 8) | data[offset++];
    return v;
}

uint32_t read_u32(BytesView& data, size_t& offset) {
    uint32_t v = 0;
    for (int i = 0; i < 4 && offset < data.size(); ++i)
        v = (v << 8) | data[offset++];
    return v;
}

uint16_t read_u16(BytesView& data, size_t& offset) {
    uint16_t v = 0;
    for (int i = 0; i < 2 && offset < data.size(); ++i)
        v = static_cast<uint16_t>((v << 8) | data[offset++]);
    return v;
}

void write_endpoint(Bytes& out, const Endpoint& ep) {
    write_u16(out, static_cast<uint16_t>(ep.scheme.size()));
    out.insert(out.end(), ep.scheme.begin(), ep.scheme.end());
    write_u16(out, static_cast<uint16_t>(ep.host.size()));
    out.insert(out.end(), ep.host.begin(), ep.host.end());
    write_u16(out, ep.port);
    write_u16(out, static_cast<uint16_t>(ep.path.size()));
    out.insert(out.end(), ep.path.begin(), ep.path.end());
}

Endpoint read_endpoint(BytesView& data, size_t& offset) {
    Endpoint ep;
    uint16_t slen = read_u16(data, offset);
    ep.scheme = std::string(data.begin() + static_cast<ptrdiff_t>(offset),
                             data.begin() + static_cast<ptrdiff_t>(offset + slen));
    offset += slen;
    uint16_t hlen = read_u16(data, offset);
    ep.host = std::string(data.begin() + static_cast<ptrdiff_t>(offset),
                           data.begin() + static_cast<ptrdiff_t>(offset + hlen));
    offset += hlen;
    ep.port = read_u16(data, offset);
    uint16_t plen = read_u16(data, offset);
    ep.path = std::string(data.begin() + static_cast<ptrdiff_t>(offset),
                           data.begin() + static_cast<ptrdiff_t>(offset + plen));
    offset += plen;
    return ep;
}

} // anonymous namespace

// ===========================================================================
// PeerState
// ===========================================================================
const char* to_string(PeerState s) noexcept {
    switch (s) {
        case PeerState::Unknown: return "Unknown";
        case PeerState::Online:  return "Online";
        case PeerState::Suspect: return "Suspect";
        case PeerState::Offline: return "Offline";
        default:                 return "Unknown";
    }
}

// ===========================================================================
// PeerRecord
// ===========================================================================
Bytes PeerRecord::serialize() const {
    Bytes out;
    out.insert(out.end(), node_id.value.begin(), node_id.value.end());
    out.push_back(static_cast<uint8_t>(state));
    write_u64(out, static_cast<uint64_t>(last_seen));
    out.push_back(static_cast<uint8_t>(ping_misses));

    // Write new fields
    write_u16(out, static_cast<uint16_t>(display_name.size()));
    out.insert(out.end(), display_name.begin(), display_name.end());
    write_u16(out, static_cast<uint16_t>(hostname.size()));
    out.insert(out.end(), hostname.begin(), hostname.end());
    write_u16(out, static_cast<uint16_t>(mesh_name.size()));
    out.insert(out.end(), mesh_name.begin(), mesh_name.end());
    out.push_back(static_cast<uint8_t>(role));
    write_u16(out, static_cast<uint16_t>(tags.size()));
    for (auto& t : tags) {
        write_u16(out, static_cast<uint16_t>(t.size()));
        out.insert(out.end(), t.begin(), t.end());
    }
    write_u16(out, static_cast<uint16_t>(platform.size()));
    out.insert(out.end(), platform.begin(), platform.end());
    write_u16(out, static_cast<uint16_t>(arch.size()));
    out.insert(out.end(), arch.begin(), arch.end());
    write_u16(out, static_cast<uint16_t>(version.size()));
    out.insert(out.end(), version.begin(), version.end());
    write_u16(out, static_cast<uint16_t>(location.size()));
    out.insert(out.end(), location.begin(), location.end());
    write_u16(out, static_cast<uint16_t>(aliases.size()));
    for (auto& a : aliases) {
        write_u16(out, static_cast<uint16_t>(a.size()));
        out.insert(out.end(), a.begin(), a.end());
    }

    write_endpoint(out, endpoint);
    // rtt_ms after endpoint for backward compat
    write_u64(out, static_cast<uint64_t>(rtt_ms * 1000.0)); // store as microseconds
    return out;
}

Result<PeerRecord> PeerRecord::deserialize(BytesView data) {
    PeerRecord rec;
    size_t off = 0;

    if (off + 32 > data.size()) {
        return SMO_ERR_DISCOVERY(400, Info, RetrySafe, None,
                                 "truncated node_id in PeerRecord");
    }
    std::memcpy(rec.node_id.value.data(), data.data() + off, 32);
    off += 32;

    if (off >= data.size()) {
        return SMO_ERR_DISCOVERY(400, Info, RetrySafe, None,
                                 "truncated state in PeerRecord");
    }
    rec.state = static_cast<PeerState>(data[off++]);
    rec.last_seen = static_cast<int64_t>(read_u64(data, off));
    rec.ping_misses = (off < data.size()) ? static_cast<int>(data[off++]) : 0;

    // Read new fields (optional — default to empty if truncated)
    auto read_str = [&]() -> std::string {
        if (off + 2 > data.size()) return {};
        uint16_t len = read_u16(data, off);
        if (off + len > data.size()) return {};
        std::string s(data.begin() + static_cast<ptrdiff_t>(off),
                       data.begin() + static_cast<ptrdiff_t>(off + len));
        off += len;
        return s;
    };
    auto read_str_vec = [&]() -> std::vector<std::string> {
        if (off + 2 > data.size()) return {};
        uint16_t count = read_u16(data, off);
        std::vector<std::string> vec;
        for (uint16_t i = 0; i < count; ++i) {
            auto s = read_str();
            if (s.empty() && off >= data.size()) break;
            vec.push_back(std::move(s));
        }
        return vec;
    };

    rec.display_name = read_str();
    rec.hostname = read_str();
    rec.mesh_name = read_str();
    if (off < data.size()) {
        rec.role = static_cast<Role>(data[off++]);
    }
    rec.tags = read_str_vec();
    rec.platform = read_str();
    rec.arch = read_str();
    rec.version = read_str();
    rec.location = read_str();
    rec.aliases = read_str_vec();

    rec.endpoint = read_endpoint(data, off);

    if (off + 8 <= data.size()) {
        rec.rtt_ms = static_cast<double>(read_u64(data, off)) / 1000.0;
    }

    return rec;
}

// ===========================================================================
// MembershipTable
// ===========================================================================
uint64_t MembershipTable::to_key(const NodeID& id) {
    uint64_t key = 0;
    std::memcpy(&key, id.value.data(), sizeof(key));
    return key;
}

Result<void> MembershipTable::upsert(PeerRecord record) {
    auto key = to_key(record.node_id);
    auto it = records_.find(key);
    if (it != records_.end()) {
        it->second = std::move(record);
        return {};
    }
    if (records_.size() >= capacity_) {
        return SMO_ERR_DISCOVERY(405, Warn, NoRetry, None,
                                 "membership table at capacity");
    }
    records_.emplace(key, std::move(record));
    return {};
}

Result<PeerRecord> MembershipTable::lookup(const NodeID& id) const {
    auto key = to_key(id);
    auto it = records_.find(key);
    if (it == records_.end()) {
        return SMO_ERR_DISCOVERY(400, Info, RetrySafe, None,
                                 "peer not found in membership table");
    }
    return it->second;
}

Result<PeerRecord> MembershipTable::lookup_by_name(const std::string& name) const {
    for (const auto& [key, rec] : records_) {
        if (rec.display_name == name) return rec;
        for (auto& alias : rec.aliases) {
            if (alias == name) return rec;
        }
    }
    return SMO_ERR_DISCOVERY(400, Info, RetrySafe, None,
                             "no peer with that name");
}

std::vector<PeerRecord> MembershipTable::peers() const {
    std::vector<PeerRecord> result;
    result.reserve(records_.size());
    for (const auto& [key, rec] : records_) {
        result.push_back(rec);
    }
    return result;
}

std::vector<PeerRecord> MembershipTable::peers_with_state(PeerState state) const {
    std::vector<PeerRecord> result;
    for (const auto& [key, rec] : records_) {
        if (rec.state == state)
            result.push_back(rec);
    }
    return result;
}

std::vector<PeerRecord> MembershipTable::peers_by_role(Role role) const {
    std::vector<PeerRecord> result;
    for (const auto& [key, rec] : records_) {
        if (rec.role == role)
            result.push_back(rec);
    }
    return result;
}

std::vector<PeerRecord> MembershipTable::peers_by_tag(const std::string& tag) const {
    std::vector<PeerRecord> result;
    for (const auto& [key, rec] : records_) {
        for (const auto& t : rec.tags) {
            if (t == tag) {
                result.push_back(rec);
                break;
            }
        }
    }
    return result;
}

std::vector<PeerRecord> MembershipTable::peers_by_os(const std::string& os) const {
    std::vector<PeerRecord> result;
    for (const auto& [key, rec] : records_) {
        if (rec.platform == os)
            result.push_back(rec);
    }
    return result;
}

std::vector<PeerRecord> MembershipTable::peers_by_arch(const std::string& arch) const {
    std::vector<PeerRecord> result;
    for (const auto& [key, rec] : records_) {
        if (rec.arch == arch)
            result.push_back(rec);
    }
    return result;
}

std::vector<PeerRecord> MembershipTable::peers_by_mesh(const std::string& mesh_name) const {
    std::vector<PeerRecord> result;
    for (const auto& [key, rec] : records_) {
        if (rec.mesh_name == mesh_name)
            result.push_back(rec);
    }
    return result;
}

Result<void> MembershipTable::remove(const NodeID& id) {
    auto key = to_key(id);
    auto it = records_.find(key);
    if (it == records_.end()) {
        return SMO_ERR_DISCOVERY(400, Info, RetrySafe, None,
                                 "peer not found for removal");
    }
    records_.erase(it);
    return {};
}

Bytes MembershipTable::serialize() const {
    Bytes out;
    write_u32(out, static_cast<uint32_t>(records_.size()));
    for (const auto& [key, rec] : records_) {
        Bytes ser = rec.serialize();
        write_u32(out, static_cast<uint32_t>(ser.size()));
        out.insert(out.end(), ser.begin(), ser.end());
    }
    return out;
}

Result<MembershipTable> MembershipTable::deserialize(BytesView data) {
    MembershipTable table;
    size_t off = 0;

    uint32_t count = read_u32(data, off);
    for (uint32_t i = 0; i < count; ++i) {
        if (off + 4 > data.size()) break;
        uint32_t rec_len = read_u32(data, off);
        if (off + rec_len > data.size()) break;

        auto rec_data = data.subspan(off, rec_len);
        off += rec_len;

        auto rec = PeerRecord::deserialize(rec_data);
        if (!rec) continue;
        table.records_.emplace(to_key(rec.value().node_id), std::move(rec.value()));
    }

    return table;
}

// ===========================================================================
// HealthMonitor
// ===========================================================================
uint64_t HealthMonitor::to_key(const NodeID& id) {
    uint64_t key = 0;
    std::memcpy(&key, id.value.data(), sizeof(key));
    return key;
}

void HealthMonitor::record_ping(const NodeID& id, int64_t now) {
    auto key = to_key(id);
    auto it = pings_.find(key);
    if (it == pings_.end()) {
        pings_[key] = {now, 1};
    } else {
        it->second.sent_at = now;
        it->second.misses++;
    }
}

Result<void> HealthMonitor::record_pong(const NodeID& id, int64_t now) {
    auto key = to_key(id);
    auto it = pings_.find(key);
    if (it == pings_.end()) {
        pings_[key] = {now, 0};
        return {};
    }
    it->second.misses = 0;
    it->second.sent_at = now;
    return {};
}

void HealthMonitor::tick(MembershipTable& table, int64_t now,
                          int64_t ping_timeout_ns, int max_misses)
{
    for (auto& [key, prec] : pings_) {
        if (prec.misses == 0) continue;

        bool expired = (now - prec.sent_at) > ping_timeout_ns;
        if (!expired) continue;

        prec.misses++;
        prec.sent_at = now;

        // Look up peer in membership table
        NodeID nid;
        std::memcpy(nid.value.data(), &key, sizeof(key));

        auto peer_result = table.lookup(nid);
        if (!peer_result) continue;

        auto rec = peer_result.value();
        if (prec.misses >= max_misses) {
            rec.state = PeerState::Offline;
            rec.ping_misses = prec.misses;
        } else {
            rec.state = PeerState::Suspect;
            rec.ping_misses = prec.misses;
        }
        table.upsert(std::move(rec));
    }
}

PeerState HealthMonitor::state(const NodeID& id) const {
    auto key = to_key(id);
    auto it = pings_.find(key);
    if (it == pings_.end()) return PeerState::Unknown;
    if (it->second.misses >= 3) return PeerState::Offline;
    if (it->second.misses > 0) return PeerState::Suspect;
    return PeerState::Online;
}

int HealthMonitor::ping_misses(const NodeID& id) const {
    auto key = to_key(id);
    auto it = pings_.find(key);
    if (it == pings_.end()) return 0;
    return it->second.misses;
}

// ===========================================================================
// HelloMsg
// ===========================================================================
Bytes HelloMsg::serialize() const {
    Bytes out;
    out.insert(out.end(), node_id.value.begin(), node_id.value.end());
    write_u64(out, pubkey_fingerprint);
    write_u16(out, protocol_version);
    return out;
}

Result<HelloMsg> HelloMsg::deserialize(BytesView data) {
    HelloMsg msg;
    size_t off = 0;
    if (off + 32 > data.size()) {
        return SMO_ERR_DISCOVERY(400, Info, RetrySafe, None,
                                 "truncated HelloMsg node_id");
    }
    std::memcpy(msg.node_id.value.data(), data.data() + off, 32);
    off += 32;
    msg.pubkey_fingerprint = read_u64(data, off);
    msg.protocol_version = read_u16(data, off);
    return msg;
}

// ===========================================================================
// WelcomeMsg
// ===========================================================================
Bytes WelcomeMsg::serialize() const {
    Bytes out;
    out.insert(out.end(), node_id.value.begin(), node_id.value.end());
    Bytes rec = peer_record.serialize();
    write_u32(out, static_cast<uint32_t>(rec.size()));
    out.insert(out.end(), rec.begin(), rec.end());
    return out;
}

Result<WelcomeMsg> WelcomeMsg::deserialize(BytesView data) {
    WelcomeMsg msg;
    size_t off = 0;
    if (off + 32 > data.size()) {
        return SMO_ERR_DISCOVERY(400, Info, RetrySafe, None,
                                 "truncated WelcomeMsg node_id");
    }
    std::memcpy(msg.node_id.value.data(), data.data() + off, 32);
    off += 32;
    uint32_t rec_len = read_u32(data, off);
    if (off + rec_len > data.size()) {
        return SMO_ERR_DISCOVERY(400, Info, RetrySafe, None,
                                 "truncated WelcomeMsg peer_record");
    }
    auto rec = PeerRecord::deserialize(data.subspan(off, rec_len));
    if (!rec) return std::move(rec.error());
    msg.peer_record = std::move(rec.value());
    return msg;
}

// ===========================================================================
// PingMsg
// ===========================================================================
Bytes PingMsg::serialize() const {
    Bytes out;
    write_u64(out, static_cast<uint64_t>(timestamp));
    return out;
}

Result<PingMsg> PingMsg::deserialize(BytesView data) {
    PingMsg msg;
    size_t off = 0;
    msg.timestamp = static_cast<int64_t>(read_u64(data, off));
    return msg;
}

// ===========================================================================
// PongMsg
// ===========================================================================
Bytes PongMsg::serialize() const {
    Bytes out;
    write_u64(out, static_cast<uint64_t>(timestamp));
    return out;
}

Result<PongMsg> PongMsg::deserialize(BytesView data) {
    PongMsg msg;
    size_t off = 0;
    msg.timestamp = static_cast<int64_t>(read_u64(data, off));
    return msg;
}

// ===========================================================================
// DiscoverMsg
// ===========================================================================
Bytes DiscoverMsg::serialize() const {
    return Bytes{};  // empty payload for MVP
}

Result<DiscoverMsg> DiscoverMsg::deserialize(BytesView data) {
    (void)data;
    return DiscoverMsg{};
}

// ===========================================================================
// NodeInfoMsg
// ===========================================================================
Bytes NodeInfoMsg::serialize() const {
    return peer_record.serialize();
}

Result<NodeInfoMsg> NodeInfoMsg::deserialize(BytesView data) {
    auto rec = PeerRecord::deserialize(data);
    if (!rec) return std::move(rec.error());
    NodeInfoMsg msg;
    msg.peer_record = std::move(rec.value());
    return msg;
}

// ===========================================================================
// OfflineMsg
// ===========================================================================
Bytes OfflineMsg::serialize() const {
    Bytes out;
    out.insert(out.end(), node_id.value.begin(), node_id.value.end());
    out.push_back(reason);
    return out;
}

Result<OfflineMsg> OfflineMsg::deserialize(BytesView data) {
    OfflineMsg msg;
    size_t off = 0;
    if (off + 32 > data.size()) {
        return SMO_ERR_DISCOVERY(400, Info, RetrySafe, None,
                                 "truncated OfflineMsg node_id");
    }
    std::memcpy(msg.node_id.value.data(), data.data() + off, 32);
    off += 32;
    msg.reason = (off < data.size()) ? data[off] : 0;
    return msg;
}

// ===========================================================================
// Bootstrap
// ===========================================================================
Result<PeerRecord> Bootstrap::find_seed(
    const std::vector<Endpoint>& seeds,
    Transport& transport,
    const NodeID& local_id,
    int64_t now)
{
    (void)local_id;
    (void)now;

    if (seeds.empty()) {
        return SMO_ERR_DISCOVERY(406, Error, RetryBackoff, ManualIntervention,
                                 "no seed endpoints provided");
    }

    for (const auto& seed : seeds) {
        auto session = transport.connect(seed);
        if (!session) continue;

        // Send HELLO
        HelloMsg hello;
        hello.node_id = local_id;
        auto hello_data = hello.serialize();
        auto send_result = session.value()->send(hello_data);
        if (!send_result) continue;

        // Read WELCOME response
        auto recv_result = session.value()->recv(4096);
        if (!recv_result) continue;

        auto welcome = WelcomeMsg::deserialize(recv_result.value());
        if (!welcome) continue;

        session.value()->close();
        return welcome.value().peer_record;
    }

    return SMO_ERR_DISCOVERY(406, Error, RetryBackoff, ManualIntervention,
                             "no seed nodes responded");
}

// ===========================================================================
// DiscoveryEngine
// ===========================================================================
DiscoveryEngine::DiscoveryEngine(MembershipTable& table, HealthMonitor& monitor, Transport& transport)
    : table_(table), monitor_(monitor), transport_(transport) {}

Result<void> DiscoveryEngine::handle_hello(const HelloMsg& msg, const Endpoint& from, int64_t now) {
    if (msg.protocol_version != 1) {
        return {};  // silently ignore incompatible versions
    }

    PeerRecord rec;
    rec.node_id = msg.node_id;
    rec.endpoint = from;
    rec.state = PeerState::Online;
    rec.last_seen = now;
    rec.ping_misses = 0;

    return table_.upsert(std::move(rec));
}

Result<void> DiscoveryEngine::handle_welcome(const WelcomeMsg& msg, int64_t now) {
    auto rec = msg.peer_record;
    rec.state = PeerState::Online;
    rec.last_seen = now;
    rec.ping_misses = 0;
    return table_.upsert(std::move(rec));
}

Result<void> DiscoveryEngine::handle_ping(const PingMsg& msg, int64_t now) {
    (void)msg;
    (void)now;
    return {};
}

Result<void> DiscoveryEngine::handle_pong(const PongMsg& msg, int64_t now) {
    (void)msg;
    (void)now;
    return {};
}

Result<void> DiscoveryEngine::handle_discover(const DiscoverMsg& msg, const Endpoint& from, int64_t now) {
    (void)msg;
    (void)now;
    // Respond with full peer table (NODE_INFO for each peer)
    send_node_info(from);
    return {};
}

Result<void> DiscoveryEngine::handle_node_info(const NodeInfoMsg& msg, int64_t now) {
    auto rec = msg.peer_record;
    rec.last_seen = now;
    if (rec.state == PeerState::Unknown) {
        rec.state = PeerState::Online;
    }
    return table_.upsert(std::move(rec));
}

Result<void> DiscoveryEngine::handle_offline(const OfflineMsg& msg, int64_t now) {
    auto peer = table_.lookup(msg.node_id);
    if (!peer) return {};  // unknown peer, ignore

    auto rec = peer.value();
    rec.state = PeerState::Offline;
    rec.last_seen = now;
    return table_.upsert(std::move(rec));
}

void DiscoveryEngine::tick(int64_t now) {
    monitor_.tick(table_, now);
}

void DiscoveryEngine::send_node_info(const Endpoint& to) const {
    auto peers = table_.peers();
    for (auto& rec : peers) {
        if (rec.state == PeerState::Offline) continue;
        NodeInfoMsg msg;
        msg.peer_record = rec;
        auto data = msg.serialize();
        auto session = transport_.connect(to);
        if (session) {
            session.value()->send(data).ignore();
        }
    }
}

} // namespace smo
