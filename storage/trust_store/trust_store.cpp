#include "trust_store.h"

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

static std::string trust_key(const std::string& node_id) {
    return "tr:" + node_id;
}

static std::string record_serialize(const TrustStore::Record& r) {
    std::ostringstream os;
    os << r.node_id << '\0'
       << r.citizen_score << '\0'
       << r.execution_score << '\0'
       << r.witness_score << '\0'
       << r.consistency_score << '\0'
       << r.composite << '\0'
       << r.updated_at;
    return os.str();
}

static bool record_deserialize(const std::string& data, TrustStore::Record& r) {
    std::istringstream is(data);
    std::string field;
    if (!std::getline(is, r.node_id, '\0')) return false;
    if (!std::getline(is, field, '\0')) return false;
    r.citizen_score = std::stod(field);
    if (!std::getline(is, field, '\0')) return false;
    r.execution_score = std::stod(field);
    if (!std::getline(is, field, '\0')) return false;
    r.witness_score = std::stod(field);
    if (!std::getline(is, field, '\0')) return false;
    r.consistency_score = std::stod(field);
    if (!std::getline(is, field, '\0')) return false;
    r.composite = std::stod(field);
    std::getline(is, field);
    r.updated_at = std::stoll(field);
    return true;
}

std::error_code TrustStore::open() noexcept {
    store_ = std::make_unique<SqliteStore>(StoreID::Trust, base_path_);
    auto res = store_->open();
    if (!res) return std::error_code(Errc::STORE_UNAVAILABLE);
    return {};
}

void TrustStore::close() noexcept { if (store_) store_->close(); }
std::error_code TrustStore::flush() noexcept { return {}; }

std::error_code TrustStore::put(const Record& rec) {
    if (!store_) return std::error_code(Errc::STORE_UNAVAILABLE);
    auto res = store_->put(to_bytes(trust_key(rec.node_id)),
                            to_bytes(record_serialize(rec)));
    if (!res) return std::error_code(Errc::STORE_CORRUPTION);
    return {};
}

std::optional<TrustStore::Record> TrustStore::get(const std::string& node_id) {
    if (!store_) return std::nullopt;
    auto res = store_->get(to_bytes(trust_key(node_id)));
    if (!res) return std::nullopt;
    Record r;
    if (!record_deserialize(to_str(res.value()), r)) return std::nullopt;
    return r;
}

std::error_code TrustStore::remove(const std::string& node_id) {
    if (!store_) return std::error_code(Errc::STORE_UNAVAILABLE);
    store_->del(to_bytes(trust_key(node_id)));
    return {};
}

} // namespace smo
