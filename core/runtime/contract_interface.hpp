#pragma once

#include "runtime_types.hpp"
#include "runtime_context.hpp"
#include "core/types.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

namespace smo::runtime {

// ── ContractMetadata (RFC 0040 §3) ──────────────────────────────────
struct ContractMetadata {
    // Identity
    std::string id;
    std::string name;
    std::string version;
    uint32_t api_version = 1;

    // Authorship
    std::string author;
    std::string description;
    std::string repository;
    std::string documentation;

    // Dependencies
    std::vector<std::string> dependencies;
    std::vector<std::string> optional_deps;

    // Capabilities
    ContractCapabilities required_capabilities;
    ContractCapabilities optional_capabilities;

    // Execution limits
    uint64_t max_execution_time_ns = 30'000'000'000;
    uint64_t max_memory_bytes = 64 * 1024 * 1024;

    // Registration
    std::vector<std::string> tags;
    std::vector<std::string> provides;
    std::string entry_point;

    // Lifecycle hooks
    bool has_initialize = false;
    bool has_shutdown = false;
    bool has_validate = false;

    // Health
    ContractLifecycle lifecycle;
};

// ── ContractInterface (RFC 0036 §3.1) ───────────────────────────────
class ContractInterface {
public:
    virtual ~ContractInterface() = default;

    // Identity
    virtual std::string id() const = 0;
    virtual const ContractMetadata& metadata() const = 0;
    virtual ContractCapabilities required_capabilities() const = 0;

    // Lifecycle hooks (optional)
    virtual Result<void> initialize(const ContractConfig& config) {
        (void)config;
        return {};
    }
    virtual Result<void> shutdown() { return {}; }

    // Core execution
    virtual Result<ContractResult> execute(
        const ContractInput& input,
        const RuntimeContext& ctx
    ) = 0;

    // Validation (optional)
    virtual Result<void> validate(const ContractInput& input) const {
        (void)input;
        return {};
    }
};

// ── NativeContract (RFC 0036 base class) ────────────────────────────
class NativeContract : public ContractInterface {
public:
    NativeContract(const ContractMetadata& meta) : metadata_(meta) {}
    virtual ~NativeContract() = default;

    std::string id() const override { return metadata_.id; }
    const ContractMetadata& metadata() const override { return metadata_; }

    ContractCapabilities required_capabilities() const override {
        return metadata_.required_capabilities;
    }

protected:
    ContractMetadata metadata_;
};

} // namespace smo::runtime
