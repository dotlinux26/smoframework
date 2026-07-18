#pragma once

#include "runtime_types.hpp"
#include "runtime_context.hpp"
#include "contract_interface.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace smo::runtime {

// ── ContractRegistry (RFC 0040 §4.1) ───────────────────────────────
// Handles metadata, lookup, and discovery ONLY.
class ContractRegistry {
public:
    ContractRegistry() = default;
    ~ContractRegistry() = default;

    // Registration
    Result<void> register_contract(std::unique_ptr<ContractInterface> contract);
    Result<void> unregister_contract(const std::string& id);

    // Lookup
    ContractInterface* get_contract(const std::string& id);
    const ContractInterface* get_contract(const std::string& id) const;
    const ContractMetadata* get_metadata(const std::string& id) const;
    bool has_contract(const std::string& id) const;
    std::vector<std::string> list_contracts() const;

    // Discovery
    std::vector<const ContractMetadata*> discover(const std::string& tag = "") const;
    std::vector<const ContractMetadata*> discover_by_capability(ContractCapability cap) const;

private:
    std::unordered_map<std::string, std::unique_ptr<ContractInterface>> contracts_;
};

// ── ContractManager (RFC 0040 §4.2) ────────────────────────────────
// Handles lifecycle: load/init/shutdown/unload, fault handling.
// Operates on contracts owned by ContractRegistry.
class ContractManager {
public:
    explicit ContractManager(ContractRegistry& registry) : registry_(registry) {}

    // Lifecycle
    Result<void> load(const std::string& id);
    Result<void> initialize(const std::string& id, const ContractConfig& config);
    Result<void> shutdown(const std::string& id);
    Result<void> unload(const std::string& id);

    // State queries
    ContractLifecycleState get_state(const std::string& id) const;
    bool is_ready(const std::string& id) const;
    bool is_faulted(const std::string& id) const;

    // Health
    std::vector<std::string> get_faulted_contracts() const;
    Result<void> recover(const std::string& id);

    // Ensure contract is ready for execution (auto load + init if needed)
    Result<void> ensure_ready(const std::string& id);

private:
    ContractRegistry& registry_;
    std::unordered_map<std::string, ContractLifecycle> lifecycle_;
};

} // namespace smo::runtime
