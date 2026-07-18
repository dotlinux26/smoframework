#pragma once

#include "runtime_types.hpp"
#include "runtime_context.hpp"
#include "contract_interface.hpp"

#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

namespace smo::runtime {

// ── Dispatcher: Routes execute() calls to registered contracts ──────
class Dispatcher {
public:
    Dispatcher() = default;
    ~Dispatcher() = default;

    // Register a contract implementation
    void register_contract(const std::string& id,
                           std::unique_ptr<ContractInterface> impl);

    // Get contract by ID (raw pointer, Dispatcher owns)
    ContractInterface* get_contract(const std::string& id);
    const ContractInterface* get_contract(const std::string& id) const;

    // Check if contract exists
    bool has_contract(const std::string& id) const;

    // List all registered contracts
    std::vector<std::string> list_contracts() const;

    // Unregister a contract
    bool unregister_contract(const std::string& id);

    // Get contract metadata
    const ContractMetadata* get_metadata(const std::string& id) const;

    // Execute a contract directly (no policy, no audit, no retry)
    Result<ContractResult> execute(
        const std::string& contract_id,
        const ContractInput& input,
        const RuntimeContext& ctx);

private:
    std::unordered_map<std::string, std::unique_ptr<ContractInterface>> contracts_;
};

} // namespace smo::runtime
