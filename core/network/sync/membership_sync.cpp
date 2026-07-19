#include "membership_sync.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace smo::network::sync {

MembershipSync::MembershipSync(MembershipTable& table, HealthMonitor& health)
    : membership_(table), health_(health) {}

uint64_t MembershipSync::subscribe(std::function<void(const MembershipEvent&)> cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t id = next_sub_id_++;
    subscribers_.emplace_back(next_sub_id_, std::move(cb));
    return id;
}

void MembershipSync::unsubscribe(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.erase(
        std::remove_if(subscribers_.begin(), subscribers_.end(),
                       [id](const auto& p) { return p.first == id; }),
        subscribers_.end());
}

void MembershipSync::emit(MembershipEvent event) {
    event.timestamp_ns = std::chrono::system_clock::now().time_since_epoch().count();
    event.sequence = ++sequence_;

    // Keep event log bounded
    event_log_.push_back(event);
    if (event_log_.size() > max_event_log_) {
        event_log_.erase(event_log_.begin());
    }

    // Notify subscribers
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, cb] : subscribers_) {
        cb(event);
    }
}

void MembershipSync::emit_peer_added(const PeerRecord& rec) {
    MembershipEvent ev;
    ev.type = MembershipEventType::PeerAdded;
    ev.node_id = rec.node_id;
    emit(std::move(ev));
}

void MembershipSync::emit_peer_removed(const NodeID& id) {
    MembershipEvent ev;
    ev.type = MembershipEventType::PeerRemoved;
    ev.node_id = id;
    emit(std::move(ev));
}

void MembershipSync::emit_peer_updated(const PeerRecord& old_rec, const PeerRecord& new_rec) {
    MembershipEvent ev;
    ev.type = MembershipEventType::PeerUpdated;
    ev.node_id = new_rec.node_id;
    emit(std::move(ev));
}

void MembershipSync::emit_peer_renamed(const NodeID& id,
                                       const std::string& old_name,
                                       const std::string& new_name) {
    MembershipEvent ev;
    ev.type = MembershipEventType::PeerRenamed;
    ev.node_id = id;
    ev.old_display_name = old_name;
    ev.new_display_name = new_name;
    emit(std::move(ev));
}

void MembershipSync::emit_capability_changed(const NodeID& id, Role old_role, Role new_role,
                                             const std::vector<std::string>& added,
                                             const std::vector<std::string>& removed) {
    MembershipEvent ev;
    ev.type = MembershipEventType::CapabilityChange;
    ev.node_id = id;
    ev.new_role = new_role;
    ev.added_caps = added;
    ev.removed_caps = removed;
    emit(std::move(ev));
}

void MembershipSync::emit_certificate_rotated(const NodeID& id, const Certificate& new_cert) {
    MembershipEvent ev;
    ev.type = MembershipEventType::CertificateRotate;
    ev.node_id = id;
    // Certificate is serialized in apply_events
    emit(std::move(ev));
}

void MembershipSync::emit_state_changed(const NodeID& id, PeerState old_state,
                                         PeerState new_state, int misses) {
    MembershipEvent ev;
    ev.type = MembershipEventType::StateChange;
    ev.node_id = id;
    ev.new_state = new_state;
    // misses encoded in sequence for now
    emit(std::move(ev));
}

namespace {
    void put_u32(Bytes& buf, uint32_t v) {
        buf.push_back(static_cast<uint8_t>(v & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    }
    void put_u64(Bytes& buf, uint64_t v) {
        for (int i = 0; i < 8; ++i)
            buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }
    void put_u16(Bytes& buf, uint16_t v) {
        buf.push_back(static_cast<uint8_t>(v & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }
    void put_str(Bytes& buf, const std::string& s) {
        put_u16(buf, static_cast<uint16_t>(s.size()));
        buf.insert(buf.end(), s.begin(), s.end());
    }
    void serialize_event_payload(Bytes& payload, const MembershipEvent& ev) {
        switch (ev.type) {
            case MembershipEventType::PeerAdded:
            case MembershipEventType::PeerRemoved:
            case MembershipEventType::PeerUpdated:
                break;
            case MembershipEventType::PeerRenamed:
                put_str(payload, ev.old_display_name);
                put_str(payload, ev.new_display_name);
                break;
            case MembershipEventType::StateChange:
                payload.push_back(static_cast<uint8_t>(ev.new_state));
                break;
            case MembershipEventType::CapabilityChange:
                payload.push_back(static_cast<uint8_t>(ev.new_role));
                put_u16(payload, static_cast<uint16_t>(ev.added_caps.size()));
                put_u16(payload, static_cast<uint16_t>(ev.removed_caps.size()));
                for (auto& s : ev.added_caps) put_str(payload, s);
                for (auto& s : ev.removed_caps) put_str(payload, s);
                break;
            case MembershipEventType::CertificateRotate: {
                auto cert_bytes = ev.new_cert.serialize();
                put_u16(payload, static_cast<uint16_t>(cert_bytes.size()));
                payload.insert(payload.end(), cert_bytes.begin(), cert_bytes.end());
                break;
            }
        }
    }
}

Bytes MembershipSync::serialize_events(const std::vector<MembershipEvent>& events) const {
    // Format: [num_events: uint32 LE]
    // Each event: [type:uint8][node_id:32 bytes][ts:uint64 LE][seq:uint64 LE][payload_len:uint32 LE][payload]
    // String encoding: [len:uint16 LE][bytes]
    
    Bytes out;
    put_u32(out, static_cast<uint32_t>(events.size()));

    for (auto& ev : events) {
        out.push_back(static_cast<uint8_t>(ev.type));
        out.insert(out.end(), ev.node_id.value.begin(), ev.node_id.value.end());
        put_u64(out, static_cast<uint64_t>(ev.timestamp_ns));
        put_u64(out, ev.sequence);

        Bytes payload;
        serialize_event_payload(payload, ev);
        put_u32(out, static_cast<uint32_t>(payload.size()));
        out.insert(out.end(), payload.begin(), payload.end());
    }
    return out;
}

// Helper to read LE values from BytesView
namespace {
    uint32_t read_u32(BytesView& data) {
        if (data.size() < 4) return 0;
        uint32_t v = 0;
        for (int i = 3; i >= 0; --i) v = (v << 8) | data[i];
        data = data.subspan(4);
        return v;
    }
    uint64_t read_u64(BytesView& data) {
        if (data.size() < 8) return 0;
        uint64_t v = 0;
        for (int i = 7; i >= 0; --i) v = (v << 8) | data[i];
        data = data.subspan(8);
        return v;
    }
    uint16_t read_u16(BytesView& data) {
        if (data.size() < 2) return 0;
        uint16_t v = 0;
        for (int i = 1; i >= 0; --i) v = (v << 8) | data[i];
        data = data.subspan(2);
        return v;
    }
    uint8_t read_u8(BytesView& data) {
        if (data.empty()) return 0;
        uint8_t v = data[0];
        data = data.subspan(1);
        return v;
    }
    std::string read_str(BytesView& data) {
        auto len = read_u16(data);
        if (len > data.size()) return {};
        std::string s(reinterpret_cast<const char*>(data.data()), len);
        data = data.subspan(len);
        return s;
    }
}

Result<void> MembershipSync::apply_events(const Bytes& data) {
    BytesView view = data;
    auto count = read_u32(view);
    if (count == 0) return {};

    for (uint32_t i = 0; i < count; ++i) {
        MembershipEvent ev;
        
        auto type_byte = read_u8(view);
        ev.type = static_cast<MembershipEventType>(type_byte);
        
        // NodeID: copy 32 bytes
        if (view.size() < 32) break;
        std::copy_n(view.data(), 32, ev.node_id.value.begin());
        view = view.subspan(32);
        
        ev.timestamp_ns = static_cast<int64_t>(read_u64(view));
        ev.sequence = read_u64(view);
        
        auto payload_len = read_u32(view);
        if (payload_len > view.size()) break;
        BytesView payload = view.subspan(0, payload_len);
        view = view.subspan(payload_len);

        switch (ev.type) {
            case MembershipEventType::PeerAdded:
            case MembershipEventType::PeerRemoved:
            case MembershipEventType::PeerUpdated:
                break;

            case MembershipEventType::PeerRenamed:
                ev.old_display_name = read_str(payload);
                ev.new_display_name = read_str(payload);
                break;

            case MembershipEventType::StateChange:
                ev.new_state = static_cast<PeerState>(read_u8(payload));
                break;

            case MembershipEventType::CapabilityChange: {
                ev.new_role = static_cast<Role>(read_u8(payload));
                auto num_added = read_u16(payload);
                auto num_removed = read_u16(payload);
                for (uint16_t j = 0; j < num_added; ++j)
                    ev.added_caps.push_back(read_str(payload));
                for (uint16_t j = 0; j < num_removed; ++j)
                    ev.removed_caps.push_back(read_str(payload));
                break;
            }

            case MembershipEventType::CertificateRotate: {
                auto cert_len = read_u16(payload);
                if (cert_len > 0 && cert_len <= payload.size()) {
                    BytesView cert_data = payload.subspan(0, cert_len);
                    Bytes cert_bytes(cert_data.begin(), cert_data.end());
                    auto cert = Certificate::deserialize(cert_bytes);
                    if (cert) ev.new_cert = std::move(cert.value());
                }
                break;
            }
        }

        // Apply event to MembershipTable
        switch (ev.type) {
            case MembershipEventType::PeerAdded:
            case MembershipEventType::PeerUpdated:
            case MembershipEventType::StateChange: {
                auto existing = membership_.lookup(ev.node_id);
                if (existing) {
                    auto rec = existing.value();
                    rec.state = ev.new_state != PeerState::Unknown ? ev.new_state : rec.state;
                    rec.last_seen = ev.timestamp_ns;
                    if (ev.type == MembershipEventType::PeerAdded) {
                        // PeerAdded: upsert with minimal record
                        PeerRecord new_rec;
                        new_rec.node_id = ev.node_id;
                        new_rec.state = PeerState::Online;
                        new_rec.last_seen = ev.timestamp_ns;
                        (void)membership_.upsert(new_rec);
                    } else {
                        (void)membership_.upsert(rec);
                    }
                } else {
                    PeerRecord rec;
                    rec.node_id = ev.node_id;
                    rec.state = ev.type == MembershipEventType::StateChange ? ev.new_state : PeerState::Online;
                    rec.last_seen = ev.timestamp_ns;
                    (void)membership_.upsert(rec);
                }
                break;
            }
            case MembershipEventType::PeerRemoved:
                (void)membership_.remove(ev.node_id);
                break;
            default:
                break;
        }

        // Re-emit locally so subscribers (HealthMonitor) get notified
        emit(ev);
    }
    return {};
}

std::vector<MembershipEvent> MembershipSync::pending_events(uint64_t since_sequence) const {
    std::vector<MembershipEvent> result;
    for (auto it = event_log_.rbegin(); it != event_log_.rend(); ++it) {
        if (it->sequence <= since_sequence) break;
        result.push_back(*it);
    }
    std::reverse(result.begin(), result.end());
    return result;
}

void MembershipSync::acknowledge(uint64_t sequence) {
    // For reliable gossip: mark events as acked
    // Could trim event_log_ up to this sequence
    (void)sequence;
}

} // namespace smo::network::sync