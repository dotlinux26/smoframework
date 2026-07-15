#include "dag_store.h"

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

static std::string dag_key(const std::string& graph_id) {
    return "dag:" + graph_id;
}

static std::string dag_serialize(const DagStore::DagRecord& r) {
    std::ostringstream os;
    os << r.graph_id << '\0' << r.intent_id << '\0'
       << r.dag_json << '\0' << r.dag_hash << '\0'
       << r.compiled_at;
    return os.str();
}

static bool dag_deserialize(const std::string& data, DagStore::DagRecord& r) {
    std::istringstream is(data);
    if (!std::getline(is, r.graph_id, '\0')) return false;
    if (!std::getline(is, r.intent_id, '\0')) return false;
    if (!std::getline(is, r.dag_json, '\0')) return false;
    if (!std::getline(is, r.dag_hash, '\0')) return false;
    std::string field;
    std::getline(is, field);
    r.compiled_at = std::stoll(field);
    return true;
}

std::error_code DagStore::open() noexcept {
    store_ = std::make_unique<SqliteStore>(StoreID::DAG, base_path_);
    auto res = store_->open();
    if (!res) return std::error_code(Errc::STORE_UNAVAILABLE);
    return {};
}

void DagStore::close() noexcept { if (store_) store_->close(); }
std::error_code DagStore::flush() noexcept { return {}; }

std::error_code DagStore::put(const DagRecord& dag) {
    if (!store_) return std::error_code(Errc::STORE_UNAVAILABLE);
    auto res = store_->put(to_bytes(dag_key(dag.graph_id)),
                            to_bytes(dag_serialize(dag)));
    if (!res) return std::error_code(Errc::STORE_CORRUPTION);
    return {};
}

std::optional<DagStore::DagRecord> DagStore::get(const std::string& graph_id) {
    if (!store_) return std::nullopt;
    auto res = store_->get(to_bytes(dag_key(graph_id)));
    if (!res) return std::nullopt;
    DagRecord r;
    if (!dag_deserialize(to_str(res.value()), r)) return std::nullopt;
    return r;
}

} // namespace smo
