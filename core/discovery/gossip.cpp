#include "core/discovery/gossip.hpp"
#include "core/network/sync/membership_sync.hpp"
#include "core/transport/framing.hpp"
#include "core/transport/tcp_transport.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

namespace smo {

GossipEngine::Config GossipEngine::default_config() {
    return Config{};
}

GossipEngine::GossipEngine(MembershipTable& table, const Config& cfg)
    : table_(table), config_(cfg), rng_(std::random_device{}()) {}

GossipEngine::~GossipEngine() = default;

void GossipEngine::start() { running_ = true; }
void GossipEngine::stop() { running_ = false; }

void GossipEngine::tick(int64_t now_ns) {
    if (!running_) return;
    if (now_ns - last_gossip_ < gossip_interval_ns_) return;
    last_gossip_ = now_ns;
    incarnation_++;

    auto peers = select_fanout_peers();
    for (auto& ep : peers) {
        send_gossip_to_peer(ep);
    }
}

Bytes GossipEngine::pending_updates(uint64_t since_sequence) const {
    if (membership_sync_) {
        auto events = membership_sync_->pending_events(since_sequence);
        return membership_sync_->serialize_events(events);
    }
    // Fallback: serialize PeerRecord list directly
    // Format: [count: uint32 LE][[type:uint8=1(added)][node_id:32]...]
    Bytes out;
    auto peers = table_.peers();
    uint32_t count = 0;
    for (auto& rec : peers) {
        if (rec.state != PeerState::Offline) count++;
    }
    auto put_u32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
        }
    };
    put_u32(count);
    for (auto& rec : peers) {
        if (rec.state == PeerState::Offline) continue;
        out.push_back(static_cast<uint8_t>(1)); // PeerAdded
        out.insert(out.end(), rec.node_id.value.begin(), rec.node_id.value.end());
    }
    return out;
}

Result<void> GossipEngine::apply_gossip(BytesView data) {
    if (membership_sync_) {
        Bytes buf(data.begin(), data.end());
        return membership_sync_->apply_events(buf);
    }
    // Fallback: parse minimal format
    if (data.size() < 4) return {};
    uint32_t count = 0;
    for (int i = 3; i >= 0; --i) count = (count << 8) | data[i];
    data = data.subspan(4);
    for (uint32_t i = 0; i < count; ++i) {
        if (data.size() < 33) break;
        data = data.subspan(1); // skip type
        NodeID nid;
        std::copy_n(data.data(), 32, nid.value.begin());
        data = data.subspan(32);
        if (!table_.lookup(nid)) {
            PeerRecord rec;
            rec.node_id = nid;
            rec.state = PeerState::Online;
            rec.last_seen = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            (void)table_.upsert(std::move(rec));
        }
    }
    return {};
}

Result<void> GossipEngine::handle_gossip_message(BytesView payload, GossipEngine& engine) {
    return engine.apply_gossip(payload);
}

void GossipEngine::send_gossip_to_peer(const Endpoint& target) {
    if (target.host.empty() || target.port == 0) return;

    auto fd = tcp_connect_to(target);
    if (!fd) return;

    // Get pending events since our last acknowledged sequence
    Bytes payload = pending_updates(local_sequence_);
    if (payload.empty()) {
        ::close(fd.value());
        return;
    }

    // Frame: SMO FrameHeader + [gossip_magic:4] + payload
    // gossip_magic = 'GOSP' (0x474F5350)
    Bytes framed_payload;
    uint32_t gmagic = 0x474F5350; // "GOSP"
    framed_payload.reserve(4 + payload.size());
    framed_payload.push_back(static_cast<uint8_t>((gmagic >> 24) & 0xFF));
    framed_payload.push_back(static_cast<uint8_t>((gmagic >> 16) & 0xFF));
    framed_payload.push_back(static_cast<uint8_t>((gmagic >> 8) & 0xFF));
    framed_payload.push_back(static_cast<uint8_t>(gmagic & 0xFF));
    framed_payload.insert(framed_payload.end(), payload.begin(), payload.end());

    // Wrap in SMO FrameHeader (magic + payload_len + flags)
    Bytes frame;
    frame_write(framed_payload, kFrameFlagNone, frame);

    auto ok = tcp_send(fd.value(), frame);
    if (ok && membership_sync_) {
        // Update local sequence to last known
        auto events = membership_sync_->pending_events(local_sequence_);
        if (!events.empty()) {
            local_sequence_ = events.back().sequence;
        }
    }

    ::close(fd.value());
}

std::vector<Endpoint> GossipEngine::select_fanout_peers() {
    std::vector<Endpoint> result;
    auto peers = table_.peers_with_state(PeerState::Online);

    if (peers.empty()) return result;

    std::shuffle(peers.begin(), peers.end(), rng_);
    size_t count = std::min<size_t>(config_.fanout, peers.size());

    for (size_t i = 0; i < count; ++i) {
        Endpoint ep;
        ep.scheme = "tcp";
        ep.host = peers[i].endpoint.host;
        ep.port = peers[i].endpoint.port;
        result.push_back(ep);
    }
    return result;
}

Result<int> GossipEngine::tcp_connect_to(const Endpoint& ep) const {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(ep.port);
    int rc = ::getaddrinfo(ep.host.c_str(), port_str.c_str(), &hints, &res);
    if (rc != 0 || !res) {
        return SMO_ERR_TRANSPORT(300, Error, RetrySafe, None, "DNS resolution failed for gossip target");
    }

    int fd = -1;
    for (auto* rp = res; rp; rp = rp->ai_next) {
        fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (::connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);

    if (fd < 0) {
        return SMO_ERR_TRANSPORT(300, Error, RetrySafe, None, "Failed to connect to gossip target");
    }
    return fd;
}

Result<bool> GossipEngine::tcp_send(int fd, BytesView data) {
    size_t total = 0;
    while (total < data.size()) {
        auto n = ::send(fd, data.data() + total, data.size() - total, MSG_NOSIGNAL);
        if (n <= 0) {
            return SMO_ERR_TRANSPORT(304, Error, RetrySafe, None, "Gossip TCP send failed");
        }
        total += static_cast<size_t>(n);
    }
    return true;
}

// EventBus listener for RecoveryApproved events
// Gossips CRL updates to peers
void GossipEngine::on_recovery_approved(const runtime::Event& ev) {
    // Parse JSON payload from event details
    // Expected: "CertificateRevocation proposal approved: {fingerprint, node_id_hex, reason, epoch}"
    std::string payload = ev.details;
    size_t brace_pos = payload.find('{');
    if (brace_pos == std::string::npos) return;

    std::string json_str = payload.substr(brace_pos);

    // Simple JSON parsing
    auto extract_field = [&](const std::string& json, const std::string& key) -> std::string {
        std::string search = "\"" + key + "\":\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return "";
        pos += search.length();
        size_t end = json.find('"', pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    };
    auto extract_uint = [&](const std::string& json, const std::string& key) -> uint64_t {
        std::string search = "\"" + key + "\":";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return 0;
        pos += search.length();
        size_t end = json.find_first_of(",}", pos);
        if (end == std::string::npos) return 0;
        return std::stoull(json.substr(pos, end - pos));
    };

    std::string fingerprint = extract_field(json_str, "fingerprint");
    std::string node_id_hex = extract_field(json_str, "node_id_hex");
    std::string reason = extract_field(json_str, "reason");
    uint64_t epoch = extract_uint(json_str, "epoch");

    if (fingerprint.empty() || node_id_hex.empty()) return;

    // Add to CRL if available
    if (crl_) {
        int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        crl_->revoke(fingerprint, node_id_hex, reason, epoch, now);
    }

    // Increment incarnation to force full sync on next gossip cycle
    incarnation_++;

    std::printf("[smo-node] GossipEngine: CRL update gossiped for fingerprint=%s epoch=%llu\n",
                fingerprint.c_str(), (unsigned long long)epoch);
}

} // namespace smo