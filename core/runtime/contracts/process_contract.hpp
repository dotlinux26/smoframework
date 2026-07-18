#pragma once

#include "../contract_interface.hpp"
#include "../runtime_context.hpp"

#include <string>

namespace smo::runtime {

class ProcessContract final : public NativeContract {
public:
    ProcessContract();

    Result<ContractResult> execute(
        const ContractInput& input,
        const RuntimeContext& ctx) override;

    ContractCapabilities required_capabilities() const override;

private:
    static ContractMetadata default_metadata();

    Result<ContractResult> handle_exec(const ContractInput& input,
                                        const RuntimeContext& ctx);
    Result<ContractResult> handle_kill(const ContractInput& input,
                                        const RuntimeContext& ctx);
    Result<ContractResult> handle_ps(const ContractInput& input,
                                      const RuntimeContext& ctx);
    Result<ContractResult> handle_top(const ContractInput& input,
                                       const RuntimeContext& ctx);
    Result<ContractResult> handle_systemctl(const ContractInput& input,
                                             const RuntimeContext& ctx);
    Result<ContractResult> handle_service(const ContractInput& input,
                                           const RuntimeContext& ctx);
    Result<ContractResult> handle_info(const ContractInput& input,
                                        const RuntimeContext& ctx);
};

} // namespace smo::runtime
