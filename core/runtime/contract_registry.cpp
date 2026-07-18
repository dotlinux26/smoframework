#include "contract_registry.hpp"

#include <algorithm>

namespace smo::runtime {

// ── ContractRegistry Implementation ──────────────────────────────────

Result<void> ContractRegistry::register_contract(std::unique_ptr<ContractInterface> contract) {
    if (!contract) {
        return Result<void>(static_cast<Error>(RuntimeError::validation("null contract")));
    }
    const auto& id = contract->id();
    if (id.empty()) {
        return Result<void>(static_cast<Error>(RuntimeError::validation("contract id is empty")));
    }
    if (contracts_.find(id) != contracts_.end()) {
        return Result<void>(static_cast<Error>(RuntimeError::validation("contract already registered: " + id)));
    }
    contracts_[id] = std::move(contract);
    return {};
}

Result<void> ContractRegistry::unregister_contract(const std::string& id) {
    auto it = contracts_.find(id);
    if (it == contracts_.end()) {
        return Result<void>(static_cast<Error>(RuntimeError::not_found("contract not found: " + id)));
    }
    contracts_.erase(it);
    return {};
}

ContractInterface* ContractRegistry::get_contract(const std::string& id) {
    auto it = contracts_.find(id);
    return it != contracts_.end() ? it->second.get() : nullptr;
}

const ContractInterface* ContractRegistry::get_contract(const std::string& id) const {
    auto it = contracts_.find(id);
    return it != contracts_.end() ? it->second.get() : nullptr;
}

const ContractMetadata* ContractRegistry::get_metadata(const std::string& id) const {
    auto it = contracts_.find(id);
    if (it == contracts_.end()) return nullptr;
    return &it->second->metadata();
}

bool ContractRegistry::has_contract(const std::string& id) const {
    return contracts_.find(id) != contracts_.end();
}

std::vector<std::string> ContractRegistry::list_contracts() const {
    std::vector<std::string> ids;
    ids.reserve(contracts_.size());
    for (const auto& [id, _] : contracts_) ids.push_back(id);
    return ids;
}

std::vector<const ContractMetadata*> ContractRegistry::discover(const std::string& tag) const {
    std::vector<const ContractMetadata*> result;
    for (const auto& [id, contract] : contracts_) {
        const auto& meta = contract->metadata();
        if (tag.empty()) {
            result.push_back(&meta);
        } else {
            auto it = std::find(meta.tags.begin(), meta.tags.end(), tag);
            if (it != meta.tags.end()) result.push_back(&meta);
        }
    }
    return result;
}

std::vector<const ContractMetadata*> ContractRegistry::discover_by_capability(ContractCapability cap) const {
    std::vector<const ContractMetadata*> result;
    for (const auto& [id, contract] : contracts_) {
        const auto& meta = contract->metadata();
        if (meta.required_capabilities.test(static_cast<size_t>(cap))) {
            result.push_back(&meta);
        }
    }
    return result;
}

// ── ContractManager Implementation ───────────────────────────────────

Result<void> ContractManager::load(const std::string& id) {
    if (!registry_.has_contract(id)) {
        return Result<void>(static_cast<Error>(RuntimeError::not_found("contract not found in registry: " + id)));
    }
    auto& lc = lifecycle_[id];
    if (lc.state != ContractLifecycleState::Registered &&
        lc.state != ContractLifecycleState::Idle &&
        lc.state != ContractLifecycleState::LoadFailed) {
        return {}; // already loaded or in progress
    }
    lc.state = ContractLifecycleState::Loaded;
    lc.loaded_at_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    lc.error_message.clear();
    return {};
}

Result<void> ContractManager::initialize(const std::string& id, const ContractConfig& config) {
    auto* contract = registry_.get_contract(id);
    if (!contract) {
        return Result<void>(static_cast<Error>(RuntimeError::not_found("contract not found: " + id)));
    }

    auto& lc = lifecycle_[id];
    if (lc.state != ContractLifecycleState::Loaded &&
        lc.state != ContractLifecycleState::InitFailed) {
        return Result<void>(static_cast<Error>(RuntimeError::validation("contract not in Loaded state: " + id)));
    }

    lc.state = ContractLifecycleState::Initialized;

    // Call the initialize hook if implemented
    auto res = const_cast<ContractInterface*>(contract)->initialize(config);
    if (!res) {
        lc.state = ContractLifecycleState::InitFailed;
        lc.error_message = res.error().message;
        return res;
    }

    lc.state = ContractLifecycleState::Ready;
    lc.initialized_at_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return {};
}

Result<void> ContractManager::shutdown(const std::string& id) {
    auto* contract = registry_.get_contract(id);
    if (!contract) {
        return Result<void>(static_cast<Error>(RuntimeError::not_found("contract not found: " + id)));
    }

    auto& lc = lifecycle_[id];
    if (lc.state != ContractLifecycleState::Ready &&
        lc.state != ContractLifecycleState::Initialized) {
        return {}; // nothing to shutdown
    }

    auto res = const_cast<ContractInterface*>(contract)->shutdown();
    lc.state = ContractLifecycleState::Idle;
    return res;
}

Result<void> ContractManager::unload(const std::string& id) {
    if (!registry_.has_contract(id)) {
        return Result<void>(static_cast<Error>(RuntimeError::not_found("contract not found: " + id)));
    }
    lifecycle_[id].state = ContractLifecycleState::Unloaded;
    return {};
}

ContractLifecycleState ContractManager::get_state(const std::string& id) const {
    auto it = lifecycle_.find(id);
    if (it == lifecycle_.end()) return ContractLifecycleState::Registered;
    return it->second.state;
}

bool ContractManager::is_ready(const std::string& id) const {
    return get_state(id) == ContractLifecycleState::Ready;
}

bool ContractManager::is_faulted(const std::string& id) const {
    auto s = get_state(id);
    return s == ContractLifecycleState::LoadFailed ||
           s == ContractLifecycleState::InitFailed ||
           s == ContractLifecycleState::InitTimeout ||
           s == ContractLifecycleState::CrashLoop;
}

std::vector<std::string> ContractManager::get_faulted_contracts() const {
    std::vector<std::string> result;
    for (const auto& [id, lc] : lifecycle_) {
        if (is_faulted(id)) result.push_back(id);
    }
    return result;
}

Result<void> ContractManager::recover(const std::string& id) {
    if (!is_faulted(id)) return {};
    auto& lc = lifecycle_[id];
    lc.crash_count = 0;
    lc.error_message.clear();
    lc.state = ContractLifecycleState::Registered; // will be re-loaded on next ensure_ready
    return {};
}

Result<void> ContractManager::ensure_ready(const std::string& id) {
    if (!registry_.has_contract(id)) {
        return Result<void>(static_cast<Error>(RuntimeError::not_found("contract: " + id)));
    }

    auto state = get_state(id);
    if (state == ContractLifecycleState::Ready) return {};

    if (state == ContractLifecycleState::Registered) {
        SMO_TRY(load(id));
        ContractConfig default_config;
        SMO_TRY(initialize(id, default_config));
    }

    if (is_faulted(id)) {
        return Result<void>(static_cast<Error>(RuntimeError::internal("contract faulted: " + id)));
    }

    return {};
}

} // namespace smo::runtime
