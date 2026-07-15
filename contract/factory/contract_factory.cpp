#include "contract_factory.hpp"

namespace smo {

ContractFactory::ContractFactory(ContractRegistry& registry,
                                 OpcodeRegistry& opcodes)
    : registry_(registry), opcodes_(opcodes) {}

Result<ContractDefinition> ContractFactory::resolve(const Intent& intent) {
    // 1. If user specified an explicit contract_hint, load by ContractID
    if (!intent.contract_hint.empty()) {
        return registry_.get(intent.contract_hint);
    }

    // 2. Resolve opcode name from the Opcode enum
    auto op_res = opcodes_.resolve(intent.opcode);
    if (!op_res) {
        return SMO_ERR_CRYPTO(8, Error, NoRetry, None,
                              "unknown opcode in intent");
    }

    // 3. Look up best contract for this opcode in the registry
    auto def = registry_.resolve(op_res.value().name);
    if (!def) {
        return SMO_ERR_CRYPTO(8, Error, NoRetry, None,
                              "no active contract for opcode");
    }

    return def.value();
}

Result<ContractDefinition> ContractFactory::resolve_by_id(
    const ContractID& id) {
    return registry_.get(id);
}

} // namespace smo
