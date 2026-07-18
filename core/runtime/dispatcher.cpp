#include "dispatcher.hpp"
#include "contract_interface.hpp"
#include "runtime_types.hpp"
#include "runtime_context.hpp"

namespace smo::runtime {

void Dispatcher::register_contract(const std::string& id,
                                    std::unique_ptr<ContractInterface> impl) {
    contracts_[id] = std::move(impl);
}

ContractInterface* Dispatcher::get_contract(const std::string& id) {
    auto it = contracts_.find(id);
    return it != contracts_.end() ? it->second.get() : nullptr;
}

const ContractInterface* Dispatcher::get_contract(const std::string& id) const {
    auto it = contracts_.find(id);
    return it != contracts_.end() ? it->second.get() : nullptr;
}

bool Dispatcher::has_contract(const std::string& id) const {
    return contracts_.find(id) != contracts_.end();
}

std::vector<std::string> Dispatcher::list_contracts() const {
    std::vector<std::string> ids;
    ids.reserve(contracts_.size());
    for (const auto& [id, _] : contracts_) ids.push_back(id);
    return ids;
}

bool Dispatcher::unregister_contract(const std::string& id) {
    return contracts_.erase(id) > 0;
}

const ContractMetadata* Dispatcher::get_metadata(const std::string& id) const {
    auto it = contracts_.find(id);
    if (it == contracts_.end()) return nullptr;
    return &it->second->metadata();
}

Result<ContractResult> Dispatcher::execute(
    const std::string& contract_id,
    const ContractInput& input,
    const RuntimeContext& ctx) {

    auto* contract = get_contract(contract_id);
    if (!contract) {
        return Result<ContractResult>(
            static_cast<Error>(RuntimeError::not_found("contract not found: " + contract_id)));
    }

    // Validate input
    auto val_res = contract->validate(input);
    if (!val_res) {
        return Result<ContractResult>(
            static_cast<Error>(RuntimeError::validation("input validation failed: " + val_res.error().message)));
    }

    // Execute
    return contract->execute(input, ctx);
}

} // namespace smo::runtime
