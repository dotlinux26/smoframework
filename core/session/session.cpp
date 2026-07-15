#include "session.hpp"

#include <cstring>
#include <limits>

namespace smo {

// ===========================================================================
// Helpers — big-endian read/write
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

} // anonymous namespace

// ===========================================================================
// SessionId
// ===========================================================================

Result<SessionId> SessionId::derive(BytesView seed, const HashImpl& hash) {
    if (!hash.hash) {
        return SMO_ERR_SESSION(500, Error, NoRetry, Reconnect,
                               "null hash implementation");
    }
    auto h = hash.hash(seed);
    if (!h) return std::move(h.error());

    SessionId id;
    size_t copy = h.value().size() < 16 ? h.value().size() : 16;
    std::memcpy(id.bytes.data(), h.value().data(), copy);
    return id;
}

Bytes SessionId::to_bytes() const {
    return Bytes(bytes.begin(), bytes.end());
}

Result<SessionId> SessionId::from_bytes(BytesView data) {
    if (data.size() < 16) {
        return SMO_ERR_SESSION(500, Error, NoRetry, Reconnect,
                               "truncated SessionId");
    }
    SessionId id;
    std::memcpy(id.bytes.data(), data.data(), 16);
    return id;
}

// ===========================================================================
// SessionState
// ===========================================================================

const char* to_string(SessionState s) noexcept {
    switch (s) {
        case SessionState::Closed:      return "Closed";
        case SessionState::Handshake:   return "Handshake";
        case SessionState::Established: return "Established";
        case SessionState::Active:      return "Active";
        case SessionState::Renewing:    return "Renewing";
        default:                        return "Unknown";
    }
}

// ===========================================================================
// FSM transitions
// ===========================================================================

bool is_valid_transition(SessionState from, SessionEvent event) noexcept {
    switch (from) {
        case SessionState::Closed:
            return event == SessionEvent::OpenRequest;

        case SessionState::Handshake:
            return event == SessionEvent::Established ||
                   event == SessionEvent::Close ||
                   event == SessionEvent::Timeout ||
                   event == SessionEvent::Error;

        case SessionState::Established:
            return event == SessionEvent::Activate ||
                   event == SessionEvent::Renew ||
                   event == SessionEvent::Close ||
                   event == SessionEvent::Timeout ||
                   event == SessionEvent::Error;

        case SessionState::Active:
            return event == SessionEvent::CompleteContract ||
                   event == SessionEvent::Close ||
                   event == SessionEvent::Error;

        case SessionState::Renewing:
            return event == SessionEvent::Established ||
                   event == SessionEvent::Close ||
                   event == SessionEvent::Timeout ||
                   event == SessionEvent::Error;

        default:
            return false;
    }
}

SessionState apply_transition(SessionState from, SessionEvent event) noexcept {
    if (!is_valid_transition(from, event)) return from;

    switch (event) {
        case SessionEvent::OpenRequest:      return SessionState::Handshake;
        case SessionEvent::Established:      return SessionState::Established;
        case SessionEvent::Activate:         return SessionState::Active;
        case SessionEvent::CompleteContract: return SessionState::Established;
        case SessionEvent::Renew:            return SessionState::Renewing;
        case SessionEvent::Close:
        case SessionEvent::Timeout:
        case SessionEvent::Error:
            return SessionState::Closed;
        default:
            return from;
    }
}

// ===========================================================================
// Session
// ===========================================================================

Result<Session> Session::create(SessionId id, NodeID peer_id,
                                 Certificate peer_cert,
                                 CapabilitySet capabilities,
                                 int64_t now, uint64_t ttl_ns)
{
    if (ttl_ns > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return SMO_ERR_SESSION(500, Error, NoRetry, Reconnect,
                               "TTL exceeds int64_t range");
    }
    Session s;
    s.id_ = id;
    s.state_ = SessionState::Handshake;
    s.peer_id_ = peer_id;
    s.peer_cert_ = std::move(peer_cert);
    s.capabilities_ = capabilities;
    s.created_at_ = now;
    s.expires_at_ = now + static_cast<int64_t>(ttl_ns);
    s.last_active_ = now;
    return s;
}

Result<void> Session::on_event(SessionEvent event, int64_t now) noexcept {
    if (!is_valid_transition(state_, event)) {
        return SMO_ERR_SESSION(500, Error, NoRetry, Reconnect,
                               "invalid session state transition");
    }

    state_ = apply_transition(state_, event);
    last_active_ = now;

    if (state_ == SessionState::Closed) {
        expires_at_ = now;
    }

    return {};
}

Result<void> Session::renew(int64_t now, uint64_t ttl_ns) noexcept {
    if (state_ != SessionState::Established && state_ != SessionState::Active) {
        return SMO_ERR_SESSION(505, Warn, RetrySafe, Reconnect,
                               "renew only valid in Established/Active state");
    }
    if (ttl_ns > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return SMO_ERR_SESSION(500, Error, NoRetry, Reconnect,
                               "TTL exceeds int64_t range");
    }
    expires_at_ = now + static_cast<int64_t>(ttl_ns);
    last_active_ = now;
    return {};
}

Bytes Session::serialize() const {
    Bytes out;
    out.push_back(static_cast<uint8_t>(state_));
    out.insert(out.end(), id_.bytes.begin(), id_.bytes.end());
    out.insert(out.end(), peer_id_.value.begin(), peer_id_.value.end());

    // Capability bitset as bytes
    size_t cap_bytes = (capabilities_.size() + 7) / 8;
    for (size_t i = 0; i < cap_bytes; ++i) {
        uint8_t byte = 0;
        for (size_t j = 0; j < 8 && (i * 8 + j) < capabilities_.size(); ++j) {
            if (capabilities_[i * 8 + j])
                byte |= static_cast<uint8_t>(1 << j);
        }
        out.push_back(byte);
    }

    write_u64(out, static_cast<uint64_t>(created_at_));
    write_u64(out, static_cast<uint64_t>(expires_at_));
    write_u64(out, static_cast<uint64_t>(last_active_));

    Bytes cert_ser = peer_cert_.serialize();
    write_u32(out, static_cast<uint32_t>(cert_ser.size()));
    out.insert(out.end(), cert_ser.begin(), cert_ser.end());

    return out;
}

Result<Session> Session::deserialize(BytesView data) {
    Session s;
    size_t off = 0;

    if (off >= data.size()) {
        return SMO_ERR_SESSION(500, Error, NoRetry, Reconnect,
                               "truncated session data");
    }
    s.state_ = static_cast<SessionState>(data[off++]);

    if (off + 16 > data.size()) {
        return SMO_ERR_SESSION(500, Error, NoRetry, Reconnect,
                               "truncated session id");
    }
    std::memcpy(s.id_.bytes.data(), data.data() + off, 16);
    off += 16;

    if (off + 32 > data.size()) {
        return SMO_ERR_SESSION(500, Error, NoRetry, Reconnect,
                               "truncated peer id");
    }
    std::memcpy(s.peer_id_.value.data(), data.data() + off, 32);
    off += 32;

    // Capability bitset
    size_t cap_bytes = (s.capabilities_.size() + 7) / 8;
    if (off + cap_bytes > data.size()) {
        return SMO_ERR_SESSION(500, Error, NoRetry, Reconnect,
                               "truncated capabilities");
    }
    for (size_t i = 0; i < cap_bytes && off < data.size(); ++i) {
        for (size_t j = 0; j < 8 && (i * 8 + j) < s.capabilities_.size(); ++j) {
            if (data[off] & (1 << j))
                s.capabilities_.set(i * 8 + j);
        }
        off++;
    }

    s.created_at_  = static_cast<int64_t>(read_u64(data, off));
    s.expires_at_  = static_cast<int64_t>(read_u64(data, off));
    s.last_active_ = static_cast<int64_t>(read_u64(data, off));

    uint32_t cert_len = read_u32(data, off);
    if (off + cert_len > data.size()) {
        return SMO_ERR_SESSION(500, Error, NoRetry, Reconnect,
                               "truncated certificate");
    }
    auto cert_data = data.subspan(off, cert_len);
    off += cert_len;

    auto cert = Certificate::deserialize(cert_data);
    if (!cert) return std::move(cert.error());
    s.peer_cert_ = std::move(cert.value());

    return s;
}

// ===========================================================================
// SessionManager
// ===========================================================================

uint64_t SessionManager::to_key(const SessionId& id) {
    uint64_t key = 0;
    std::memcpy(&key, id.bytes.data(), sizeof(key));
    return key;
}

Result<Session*> SessionManager::open(Session session) {
    auto key = to_key(session.id());
    if (sessions_.find(key) != sessions_.end()) {
        return SMO_ERR_SESSION(504, Warn, NoRetry, None,
                               "session already exists");
    }
    auto [it, inserted] = sessions_.emplace(key, std::move(session));
    if (!inserted) {
        return SMO_ERR_SESSION(504, Warn, NoRetry, None,
                               "failed to insert session");
    }
    return &it->second;
}

Session* SessionManager::lookup(const SessionId& id) {
    auto key = to_key(id);
    auto it = sessions_.find(key);
    if (it == sessions_.end()) return nullptr;
    return &it->second;
}

Result<void> SessionManager::close(const SessionId& id, int64_t now) {
    auto* session = lookup(id);
    if (!session) {
        return SMO_ERR_SESSION(501, Info, RetrySafe, Reconnect,
                               "session not found");
    }
    return session->on_event(SessionEvent::Close, now);
}

Result<void> SessionManager::transition(const SessionId& id, SessionEvent event, int64_t now) {
    auto* session = lookup(id);
    if (!session) {
        return SMO_ERR_SESSION(501, Info, RetrySafe, Reconnect,
                               "session not found");
    }
    return session->on_event(event, now);
}

void SessionManager::tick(int64_t now) {
    for (auto& [key, session] : sessions_) {
        if (session.is_valid_at(now)) continue;
        session.on_event(SessionEvent::Timeout, now);
    }
}

void SessionManager::collect_garbage() {
    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        if (it->second.state() == SessionState::Closed) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

Bytes SessionManager::serialize_all() const {
    Bytes out;
    write_u32(out, static_cast<uint32_t>(sessions_.size()));
    for (const auto& [key, session] : sessions_) {
        Bytes ser = session.serialize();
        write_u32(out, static_cast<uint32_t>(ser.size()));
        out.insert(out.end(), ser.begin(), ser.end());
    }
    return out;
}

// ===========================================================================
// SessionOpenMsg
// ===========================================================================

Bytes SessionOpenMsg::serialize() const {
    Bytes out;
    // nonce (32 bytes)
    size_t nonce_len = nonce.size() < 32 ? nonce.size() : 32;
    for (size_t i = 0; i < 32; ++i) {
        out.push_back(i < nonce_len ? nonce[i] : 0);
    }
    // signature (64 bytes)
    size_t sig_len = signature.size() < 64 ? signature.size() : 64;
    for (size_t i = 0; i < 64; ++i) {
        out.push_back(i < sig_len ? signature[i] : 0);
    }
    // cert data
    write_u32(out, static_cast<uint32_t>(cert_data.size()));
    out.insert(out.end(), cert_data.begin(), cert_data.end());
    return out;
}

Result<SessionOpenMsg> SessionOpenMsg::deserialize(BytesView data) {
    SessionOpenMsg msg;
    size_t off = 0;

    if (off + 32 > data.size()) {
        return SMO_ERR_SESSION(500, Error, NoRetry, Reconnect,
                               "truncated nonce in SessionOpenMsg");
    }
    msg.nonce = Bytes(data.begin() + static_cast<ptrdiff_t>(off),
                      data.begin() + static_cast<ptrdiff_t>(off + 32));
    off += 32;

    if (off + 64 > data.size()) {
        return SMO_ERR_SESSION(500, Error, NoRetry, Reconnect,
                               "truncated signature in SessionOpenMsg");
    }
    msg.signature = Bytes(data.begin() + static_cast<ptrdiff_t>(off),
                          data.begin() + static_cast<ptrdiff_t>(off + 64));
    off += 64;

    uint32_t cert_len = read_u32(data, off);
    if (off + cert_len > data.size()) {
        return SMO_ERR_SESSION(500, Error, NoRetry, Reconnect,
                               "truncated cert data in SessionOpenMsg");
    }
    msg.cert_data = Bytes(data.begin() + static_cast<ptrdiff_t>(off),
                          data.begin() + static_cast<ptrdiff_t>(off + cert_len));
    off += cert_len;

    return msg;
}

// ===========================================================================
// SessionCloseMsg
// ===========================================================================

Bytes SessionCloseMsg::serialize() const {
    Bytes out;
    out.push_back(reason);
    size_t sig_len = signature.size() < 64 ? signature.size() : 64;
    for (size_t i = 0; i < 64; ++i) {
        out.push_back(i < sig_len ? signature[i] : 0);
    }
    return out;
}

Result<SessionCloseMsg> SessionCloseMsg::deserialize(BytesView data) {
    SessionCloseMsg msg;
    size_t off = 0;

    if (off >= data.size()) {
        return SMO_ERR_SESSION(500, Error, NoRetry, Reconnect,
                               "truncated reason in SessionCloseMsg");
    }
    msg.reason = data[off++];

    if (off + 64 > data.size()) {
        return SMO_ERR_SESSION(500, Error, NoRetry, Reconnect,
                               "truncated signature in SessionCloseMsg");
    }
    msg.signature = Bytes(data.begin() + static_cast<ptrdiff_t>(off),
                          data.begin() + static_cast<ptrdiff_t>(off + 64));
    return msg;
}

} // namespace smo
