#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "core/contract/contract_id.hpp"
#include "core/errors/error.hpp"

namespace smo {

struct ContractParameter {
    std::string name;
    std::string type;
    bool        required{false};
    std::optional<std::string> default_value;
    std::string description;
};

struct CompilerHints {
    uint32_t max_parallelism{1};
    uint32_t timeout_sec{30};
    bool     idempotent{false};
};

struct ContractDefinition {
    std::string                     contract_version;
    std::string                     category;
    std::string                     opcode;
    std::string                     name;
    std::string                     description;
    std::string                     publisher;
    std::string                     semver;
    std::vector<ContractParameter>  parameters;
    std::map<std::string, uint32_t> capabilities_required;
    CompilerHints                   compiler_hints;
    std::string                     signature;
    ContractID                      contract_id;

    static Result<ContractDefinition> from_canonical_json(std::string_view json);
    std::string to_canonical_json() const;
    ContractID compute_id() const;
};

} // namespace smo
