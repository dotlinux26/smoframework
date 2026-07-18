#pragma once

#include "core/runtime/contract_interface.hpp"
#include "core/runtime/runtime_context.hpp"

#include <string>
#include <memory>
#include <cstdint>

namespace smo {
class GovernanceEngine;
namespace authority { class MeshAuthority; }
}

namespace smo::runtime {

class GovernanceContract : public NativeContract {
public:
    GovernanceContract(smo::GovernanceEngine& engine,
                       smo::authority::MeshAuthority& authority);

    ContractCapabilities required_capabilities() const override {
        ContractCapabilities caps;
        caps.set(static_cast<size_t>(ContractCapability::Crypto));
        caps.set(static_cast<size_t>(ContractCapability::Governance));
        return caps;
    }

    Result<ContractResult> execute(
        const ContractInput& input,
        const RuntimeContext& ctx) override;

    static ContractMetadata default_metadata();

private:
    smo::GovernanceEngine& engine_;
    smo::authority::MeshAuthority& authority_;

    Result<ContractResult> handle_propose(const ContractInput& input, const RuntimeContext& ctx);
    Result<ContractResult> handle_vote(const ContractInput& input, const RuntimeContext& ctx);
    Result<ContractResult> handle_commit(const ContractInput& input, const RuntimeContext& ctx);
    Result<ContractResult> handle_list(const ContractInput& input, const RuntimeContext& ctx);
    Result<ContractResult> handle_status(const ContractInput& input, const RuntimeContext& ctx);
    Result<ContractResult> handle_info(const ContractInput& input, const RuntimeContext& ctx);
};

} // namespace smo::runtime
