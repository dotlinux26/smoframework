#include "node_store.h"

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

static std::string identity_key() { return "id:self"; }

static std::string route_key(const std::string& node_id) {
    return "rt:" + node_id;
}

static std::string identity_serialize(const NodeStore::Identity& id) {
    std::ostringstream os;
    os << id.node_id << '\0';
    for (auto b : id.public_key.value) os.put(static_cast<char>(b));
    os << '\0';
    for (auto b : id.secret_key.value) os.put(static_cast<char>(b));
    os << '\0';
    os << id.domain;
    return os.str();
}

static bool identity_deserialize(const std::string& data, NodeStore::Identity& id) {
    std::istringstream is(data);
    if (!std::getline(is, id.node_id, '\0')) return false;
    for (auto& b : id.public_key.value) {
        int c = is.get();
        if (c < 0) return false;
        b = static_cast<uint8_t>(c);
    }
    if (is.get() != '\0') return false;
    for (auto& b : id.secret_key.value) {
        int c = is.get();
        if (c < 0) return false;
        b = static_cast<uint8_t>(c);
    }
    if (is.get() != '\0') return false;
    std::getline(is, id.domain);
    return true;
}

static std::string route_serialize(const NodeStore::RouteEntry& e) {
    std::ostringstream os;
    os << e.node_id << '\0' << e.address << '\0'
       << e.port << '\0' << e.last_seen << '\0' << e.trust_score;
    return os.str();
}

static bool route_deserialize(const std::string& data, NodeStore::RouteEntry& e) {
    std::istringstream is(data);
    std::string field;
    if (!std::getline(is, e.node_id, '\0')) return false;
    if (!std::getline(is, e.address, '\0')) return false;
    if (!std::getline(is, field, '\0')) return false;
    e.port = static_cast<uint16_t>(std::stoul(field));
    if (!std::getline(is, field, '\0')) return false;
    e.last_seen = std::stoll(field);
    std::getline(is, field);
    e.trust_score = std::stod(field);
    return true;
}

std::error_code NodeStore::open() noexcept {
    store_ = std::make_unique<SqliteStore>(StoreID::Node, base_path_);
    auto res = store_->open();
    if (!res) return std::error_code(Errc::STORE_UNAVAILABLE);
    return {};
}

void NodeStore::close() noexcept { if (store_) store_->close(); }
std::error_code NodeStore::flush() noexcept { return {}; }

std::error_code NodeStore::put_identity(const Identity& id) {
    if (!store_) return std::error_code(Errc::STORE_UNAVAILABLE);
    auto res = store_->put(to_bytes(identity_key()),
                            to_bytes(identity_serialize(id)));
    if (!res) return std::error_code(Errc::STORE_CORRUPTION);
    return {};
}

std::optional<NodeStore::Identity> NodeStore::get_identity() {
    if (!store_) return std::nullopt;
    auto res = store_->get(to_bytes(identity_key()));
    if (!res) return std::nullopt;
    Identity id;
    if (!identity_deserialize(to_str(res.value()), id)) return std::nullopt;
    return id;
}

std::error_code NodeStore::put_route(const RouteEntry& entry) {
    if (!store_) return std::error_code(Errc::STORE_UNAVAILABLE);
    auto res = store_->put(to_bytes(route_key(entry.node_id)),
                            to_bytes(route_serialize(entry)));
    if (!res) return std::error_code(Errc::STORE_CORRUPTION);
    return {};
}

std::optional<NodeStore::RouteEntry> NodeStore::get_route(const std::string& node_id) {
    if (!store_) return std::nullopt;
    auto res = store_->get(to_bytes(route_key(node_id)));
    if (!res) return std::nullopt;
    RouteEntry e;
    if (!route_deserialize(to_str(res.value()), e)) return std::nullopt;
    return e;
}

std::vector<NodeStore::RouteEntry> NodeStore::all_routes() {
    std::vector<RouteEntry> result;
    if (!store_) return result;
    Bytes prefix = to_bytes("rt:");
    auto list_res = store_->list(prefix);
    if (!list_res) return result;
    for (auto& key : list_res.value()) {
        auto val_res = store_->get(key);
        if (!val_res) continue;
        RouteEntry e;
        if (route_deserialize(to_str(val_res.value()), e))
            result.push_back(std::move(e));
    }
    return result;
}

} // namespace smo
