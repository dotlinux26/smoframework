#pragma once

#include <memory>
#include "core/contract/contract_definition.hpp"
#include "core/contract/contract_id.hpp"
#include "core/errors/error.hpp"
#include "core/intent/intent.h"
#include "core/opcode/opcode_registry.hpp"
#include "contract/registry/contract_registry.hpp"

namespace smo {

class ContractFactory {
public:
    ContractFactory(ContractRegistry& registry, OpcodeRegistry& opcodes);

    Result<ContractDefinition> resolve(const Intent& intent);
    Result<ContractDefinition> resolve_by_id(const ContractID& id);

private:
    ContractRegistry& registry_;
    OpcodeRegistry&   opcodes_;
};

} // namespace smo
