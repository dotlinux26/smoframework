#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "core/contract/contract_definition.hpp"
#include "core/contract/contract_id.hpp"
#include "core/errors/error.hpp"
#include "core/storage/sqlite_store.hpp"

namespace smo {

class ContractRegistry {
public:
    explicit ContractRegistry(std::string base_path);

    Result<void> open();

    ContractID publish(const std::string& canonical_json,
                       const std::string& signature,
                       const std::string& publisher_id);

    ContractID register_native(std::string_view canonical_json);

    Result<ContractDefinition> resolve(std::string_view opcode,
                                       std::string_view version_constraint = "");

    Result<ContractDefinition> get(const ContractID& id);
    Result<ContractDefinition> get(std::string_view contract_id_hex);

    Result<void> deprecate(const ContractID& id, std::string_view reason,
                           std::string_view publisher_signature);

    std::vector<ContractDefinition> list(std::string_view opcode = "",
                                          std::string_view category = "",
                                          std::string_view status = "active");

private:
    std::unique_ptr<SqliteStore> store_;
    std::string base_path_;

    void ensure_table();
    ContractID insert_contract(const ContractDefinition& def);
};

} // namespace smo
