#pragma once

#include "core/contract/contract_definition.hpp"
#include "core/errors/error.hpp"
#include "contract/registry/contract_registry.hpp"

namespace smo {

Result<void> register_native_contracts(ContractRegistry& registry);

} // namespace smo
