#include "session_store.h"

#include "../../core/storage/sqlite_store.hpp"
#include "../../core/types.hpp"
#include <sstream>

namespace smo {

static Bytes to_bytes(const std::string& s) {
    return Bytes(s.begin(), s.end());
}

static std::string to_str(const Bytes& b) {
    return std::string(b.begin(), b.end());
}

static std::string session_key(const SessionId& id) {
    return "ses:" + std::string(id.bytes.begin(), id.bytes.end());
}

static std::string session_serialize(const Session& s) {
    std::ostringstream os;
    os << s.requester << '\0' << s.responder << '\0'
       << s.capabilities.to_ulong() << '\0'
       << s.created_at << '\0' << s.expires_at << '\0'
       << (s.active ? 1 : 0);
    return os.str();
}

static bool session_deserialize(const std::string& data, Session& s) {
    std::istringstream is(data);
    std::string field;
    if (!std::getline(is, field, '\0')) return false;
    s.requester = field;
    if (!std::getline(is, field, '\0')) return false;
    s.responder = field;
    unsigned long caps = 0;
    if (!std::getline(is, field, '\0')) return false;
    caps = std::stoul(field);
    s.capabilities = CapabilitySet(caps);
    if (!std::getline(is, field, '\0')) return false;
    s.created_at = std::stoll(field);
    if (!std::getline(is, field, '\0')) return false;
    s.expires_at = std::stoll(field);
    if (!std::getline(is, field, '\0')) return false;
    s.active = (std::stoi(field) != 0);
    return true;
}

std::error_code SessionStore::open() noexcept {
    store_ = std::make_unique<SqliteStore>(StoreID::Session, base_path_);
    auto res = store_->open();
    if (!res) return std::error_code(Errc::STORE_UNAVAILABLE);
    return std::error_code{};
}

void SessionStore::close() noexcept { if (store_) store_->close(); }
std::error_code SessionStore::flush() noexcept { return {}; }

std::error_code SessionStore::put(const Session& session) {
    if (!store_) return std::error_code(Errc::STORE_UNAVAILABLE);
    auto res = store_->put(to_bytes(session_key(session.id)),
                            to_bytes(session_serialize(session)));
    if (!res) return std::error_code(Errc::STORE_CORRUPTION);
    return {};
}

std::optional<Session> SessionStore::get(const SessionId& id) {
    if (!store_) return std::nullopt;
    auto res = store_->get(to_bytes(session_key(id)));
    if (!res) return std::nullopt;
    Session s;
    if (!session_deserialize(to_str(res.value()), s)) return std::nullopt;
    s.id = id;
    return s;
}

std::error_code SessionStore::remove(const SessionId& id) {
    if (!store_) return std::error_code(Errc::STORE_UNAVAILABLE);
    store_->del(to_bytes(session_key(id)));
    return {};
}

std::error_code SessionStore::expire_before(int64_t timestamp) {
    if (!store_) return std::error_code(Errc::STORE_UNAVAILABLE);
    Bytes prefix = to_bytes("ses:");
    auto list_res = store_->list(prefix);
    if (!list_res) return {};
    for (auto& key : list_res.value()) {
        auto val_res = store_->get(key);
        if (!val_res) continue;
        Session s;
        if (!session_deserialize(to_str(val_res.value()), s)) continue;
        if (s.expires_at > 0 && s.expires_at < timestamp)
            store_->del(key);
    }
    return {};
}

} // namespace smo
