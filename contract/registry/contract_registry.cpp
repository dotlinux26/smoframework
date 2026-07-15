#include "contract_registry.hpp"
#include <chrono>
#include <sstream>

namespace smo {

ContractRegistry::ContractRegistry(std::string base_path)
    : base_path_(std::move(base_path)) {}

Result<void> ContractRegistry::open() {
    store_ = std::make_unique<SqliteStore>(StoreID::DAG, base_path_);
    auto res = store_->open();
    if (!res) return res;
    auto& db = store_->database();
    auto r1 = db.exec(
        "CREATE TABLE IF NOT EXISTS contract_registry ("
        "contract_id TEXT PRIMARY KEY, "
        "canonical_json TEXT NOT NULL, "
        "category TEXT NOT NULL, "
        "opcode TEXT NOT NULL, "
        "name TEXT NOT NULL, "
        "publisher TEXT NOT NULL, "
        "semver TEXT NOT NULL, "
        "parameters TEXT NOT NULL, "
        "capabilities TEXT NOT NULL, "
        "compiler_hints TEXT NOT NULL, "
        "signature TEXT, "
        "published_at INTEGER NOT NULL, "
        "status TEXT NOT NULL DEFAULT 'active', "
        "deprecation_note TEXT)"
    );
    if (!r1) return r1;
    auto r2 = db.exec(
        "CREATE INDEX IF NOT EXISTS idx_registry_opcode "
        "ON contract_registry(opcode)"
    );
    if (!r2) return r2;
    return db.exec(
        "CREATE INDEX IF NOT EXISTS idx_registry_publisher "
        "ON contract_registry(publisher)"
    );
}

ContractID ContractRegistry::publish(const std::string& canonical_json,
                                     const std::string& signature,
                                     const std::string& publisher_id) {
    auto def_res = ContractDefinition::from_canonical_json(canonical_json);
    if (!def_res) return {};
    auto& def = def_res.value();
    auto cid = def.contract_id;
    auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    auto& db = store_->database();
    auto stmt_res = db.prepare(
        "INSERT OR IGNORE INTO contract_registry "
        "(contract_id, canonical_json, category, opcode, name, publisher, "
        "semver, parameters, capabilities, compiler_hints, signature, "
        "published_at, status) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'active')"
    );
    if (!stmt_res) return {};
    auto stmt = std::move(stmt_res.value());
    stmt.bind_text(1, cid.hex);
    stmt.bind_text(2, canonical_json);
    stmt.bind_text(3, def.category);
    stmt.bind_text(4, def.opcode);
    stmt.bind_text(5, def.name);
    stmt.bind_text(6, publisher_id);
    stmt.bind_text(7, def.semver);
    stmt.bind_text(8, "[]");
    stmt.bind_text(9, "{}");
    stmt.bind_text(10, "{}");
    stmt.bind_text(11, signature);
    stmt.bind_int64(12, now);
    auto step_res = stmt.step();
    if (!step_res) return {};
    return cid;
}

ContractID ContractRegistry::register_native(
    std::string_view canonical_json) {
    auto def_res = ContractDefinition::from_canonical_json(canonical_json);
    if (!def_res) return {};
    auto& def = def_res.value();
    def.category = "native";
    def.publisher = "00000000-0000-0000-0000-000000000000";
    auto cid = def.contract_id;
    auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    auto& db = store_->database();
    auto stmt_res = db.prepare(
        "INSERT OR IGNORE INTO contract_registry "
        "(contract_id, canonical_json, category, opcode, name, publisher, "
        "semver, parameters, capabilities, compiler_hints, signature, "
        "published_at, status) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NULL, ?, 'active')"
    );
    if (!stmt_res) return {};
    auto stmt = std::move(stmt_res.value());
    stmt.bind_text(1, cid.hex);
    stmt.bind_text(2, std::string(canonical_json));
    stmt.bind_text(3, def.category);
    stmt.bind_text(4, def.opcode);
    stmt.bind_text(5, def.name);
    stmt.bind_text(6, def.publisher);
    stmt.bind_text(7, def.semver);
    stmt.bind_text(8, "[]");
    stmt.bind_text(9, "{}");
    stmt.bind_text(10, "{}");
    stmt.bind_int64(11, now);
    auto step_res = stmt.step();
    if (!step_res) return {};
    return cid;
}

Result<ContractDefinition> ContractRegistry::resolve(
    std::string_view opcode, std::string_view) {
    auto& db = store_->database();
    auto stmt_res = db.prepare(
        "SELECT canonical_json FROM contract_registry "
        "WHERE opcode = ? AND status = 'active' "
        "ORDER BY published_at DESC LIMIT 1"
    );
    if (!stmt_res)
        return SMO_ERR_INTERNAL(1, Error, NoRetry, None, "query prepare failed");
    auto stmt = std::move(stmt_res.value());
    stmt.bind_text(1, std::string(opcode));
    auto step_res = stmt.step();
    if (!step_res || step_res.value() != SQLITE_ROW)
        return SMO_ERR_INTERNAL(2, Error, NoRetry, None, "no active contract for opcode");
    return ContractDefinition::from_canonical_json(stmt.column_text(0));
}

Result<ContractDefinition> ContractRegistry::get(const ContractID& id) {
    return get(id.hex);
}

Result<ContractDefinition> ContractRegistry::get(
    std::string_view contract_id_hex) {
    auto& db = store_->database();
    auto stmt_res = db.prepare(
        "SELECT canonical_json FROM contract_registry WHERE contract_id = ?"
    );
    if (!stmt_res)
        return SMO_ERR_INTERNAL(3, Error, NoRetry, None, "query prepare failed");
    auto stmt = std::move(stmt_res.value());
    stmt.bind_text(1, std::string(contract_id_hex));
    auto step_res = stmt.step();
    if (!step_res || step_res.value() != SQLITE_ROW)
        return SMO_ERR_INTERNAL(4, Error, NoRetry, None, "contract not found");
    return ContractDefinition::from_canonical_json(stmt.column_text(0));
}

Result<void> ContractRegistry::deprecate(const ContractID& id,
                                         std::string_view reason,
                                         std::string_view) {
    auto& db = store_->database();
    auto stmt_res = db.prepare(
        "UPDATE contract_registry SET status = 'deprecated', "
        "deprecation_note = ? WHERE contract_id = ?"
    );
    if (!stmt_res)
        return SMO_ERR_INTERNAL(5, Error, NoRetry, None, "update prepare failed");
    auto stmt = std::move(stmt_res.value());
    stmt.bind_text(1, std::string(reason));
    stmt.bind_text(2, id.hex);
    auto step_res = stmt.step();
    if (!step_res)
        return SMO_ERR_INTERNAL(6, Error, NoRetry, None, "deprecate failed");
    return {};
}

std::vector<ContractDefinition> ContractRegistry::list(
    std::string_view opcode, std::string_view category,
    std::string_view status) {
    std::string sql = "SELECT canonical_json FROM contract_registry WHERE 1=1";
    std::vector<std::string> params;
    if (!opcode.empty()) {
        sql += " AND opcode = ?";
        params.push_back(std::string(opcode));
    }
    if (!category.empty()) {
        sql += " AND category = ?";
        params.push_back(std::string(category));
    }
    if (!status.empty()) {
        sql += " AND status = ?";
        params.push_back(std::string(status));
    }
    sql += " ORDER BY published_at DESC";

    auto& db = store_->database();
    auto stmt_res = db.prepare(sql.c_str());
    if (!stmt_res) return {};

    auto stmt = std::move(stmt_res.value());
    for (int i = 0; i < static_cast<int>(params.size()); ++i)
        stmt.bind_text(i + 1, params[i]);

    std::vector<ContractDefinition> results;
    while (true) {
        auto step_res = stmt.step();
        if (!step_res || step_res.value() != SQLITE_ROW) break;
        auto def = ContractDefinition::from_canonical_json(stmt.column_text(0));
        if (def) results.push_back(std::move(def.value()));
    }
    return results;
}

} // namespace smo
