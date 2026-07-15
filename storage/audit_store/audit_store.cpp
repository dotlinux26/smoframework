#include "audit_store.h"

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

static std::string entry_key(const std::string& contract_id, int64_t seq) {
    std::ostringstream os;
    os << "aud:" << contract_id << ":" << seq;
    return os.str();
}

static std::string entry_serialize(const AuditStore::Entry& e) {
    std::ostringstream os;
    os << e.sequence << '\0'
       << e.contract_id << '\0'
       << e.from_state << '\0'
       << e.to_state << '\0'
       << e.trigger << '\0'
       << e.timestamp << '\0'
       << e.signature;
    return os.str();
}

static bool read_field(std::istringstream& is, std::string& out) {
    out.clear();
    char c;
    while (is.get(c)) {
        if (c == '\0') return true;
        out.push_back(c);
    }
    return out.empty() ? !is.fail() : true;
}

static bool entry_deserialize(const std::string& data, AuditStore::Entry& e) {
    std::istringstream is(data);
    std::string field;
    if (!read_field(is, field)) return false;
    e.sequence = std::stoll(field);
    if (!read_field(is, e.contract_id)) return false;
    if (!read_field(is, e.from_state)) return false;
    if (!read_field(is, e.to_state)) return false;
    if (!read_field(is, e.trigger)) return false;
    if (!read_field(is, field)) return false;
    e.timestamp = std::stoll(field);
    // Last field: read remaining (signature may be empty / no trailing \0)
    e.signature.assign(std::istreambuf_iterator<char>(is), {});
    return true;
}

std::error_code AuditStore::open() noexcept {
    store_ = std::make_unique<SqliteStore>(StoreID::Audit, base_path_);
    auto res = store_->open();
    if (!res) return std::error_code(Errc::STORE_UNAVAILABLE);
    sequence_ = 0;
    // Find max sequence
    Bytes prefix = to_bytes("aud:");
    auto list_res = store_->list(prefix);
    if (list_res) {
        for (auto& key : list_res.value()) {
            auto val_res = store_->get(key);
            if (!val_res) continue;
            Entry e;
            if (entry_deserialize(to_str(val_res.value()), e)) {
                if (e.sequence > sequence_)
                    sequence_ = e.sequence;
            }
        }
    }
    return {};
}

void AuditStore::close() noexcept { if (store_) store_->close(); }
std::error_code AuditStore::flush() noexcept { return {}; }

std::error_code AuditStore::append(const Entry& entry) {
    if (!store_) return std::error_code(Errc::STORE_UNAVAILABLE);
    Entry e = entry;
    if (e.sequence == 0) e.sequence = ++sequence_;
    auto res = store_->put(to_bytes(entry_key(e.contract_id, e.sequence)),
                            to_bytes(entry_serialize(e)));
    if (!res) return std::error_code(Errc::STORE_CORRUPTION);
    return {};
}

std::vector<AuditStore::Entry> AuditStore::query(const std::string& contract_id) {
    std::vector<Entry> result;
    if (!store_) return result;
    Bytes prefix = to_bytes("aud:" + contract_id + ":");
    auto list_res = store_->list(prefix);
    if (!list_res) return result;
    for (auto& key : list_res.value()) {
        auto val_res = store_->get(key);
        if (!val_res) continue;
        Entry e;
        if (entry_deserialize(to_str(val_res.value()), e))
            result.push_back(std::move(e));
    }
    return result;
}

std::optional<AuditStore::Entry> AuditStore::last(const std::string& contract_id) {
    if (!store_) return std::nullopt;
    Bytes prefix = to_bytes("aud:" + contract_id + ":");
    auto list_res = store_->list(prefix);
    if (!list_res || list_res.value().empty()) return std::nullopt;
    auto& keys = list_res.value();
    auto last_key = store_->get(keys.back());
    if (!last_key) return std::nullopt;
    Entry e;
    if (!entry_deserialize(to_str(last_key.value()), e)) return std::nullopt;
    return e;
}

} // namespace smo
