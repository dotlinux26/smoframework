#pragma once

#include "core/contract/contract_abi.hpp"
#include "core/contract/contract_id.hpp"
#include "core/errors/error.hpp"
#include "core/types.hpp"

namespace smo {

enum class ContractCategory : uint8_t {
    Kernel = 0,
    Native = 1,
    Mesh   = 2,
    Private = 3
};

struct ExecutionContext {
    std::string session_id;
    std::string requester_node_id;
    std::string responder_node_id;
    std::string scope;              // "single" (unicast) or "mesh" (meshcast)
    std::string granted_capabilities;
    uint64_t    deadline_ns{0};
    uint32_t    max_concurrency{1};
};

struct ExecutionResult {
    bool        success{false};
    ContractID  contract_id;
    std::string dag_hash;
    std::string output_json;
    uint64_t    started_at{0};
    uint64_t    completed_at{0};
    std::vector<Error> errors;
};

class Contract {
public:
    virtual ~Contract() = default;

    virtual ContractID id() const = 0;
    virtual ContractCategory category() const = 0;
    virtual ContractABI abi() const = 0;
    virtual Result<ExecutionResult> execute(
        const std::string& dag_json,
        const ExecutionContext& ctx
    ) = 0;
};

} // namespace smo
