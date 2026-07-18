#pragma once

#include "../contract_interface.hpp"
#include "../runtime_context.hpp"

#include "core/recovery/recovery_engine.hpp"
#include "core/recovery/crl.hpp"
#include "core/governance/governance.hpp"

#include <string>

namespace smo::runtime {

class RecoveryContract final : public NativeContract {
public:
    RecoveryContract(smo::recovery::RecoveryEngine& engine,
                     smo::recovery::CRL* crl,
                     smo::GovernanceEngine& governance_engine);

    Result<ContractResult> execute(
        const ContractInput& input,
        const RuntimeContext& ctx) override;

private:
    smo::recovery::RecoveryEngine& engine_;
    smo::recovery::CRL* crl_;
    smo::GovernanceEngine& governance_engine_;

    static ContractMetadata default_metadata();

    Result<ContractResult> handle_assess(const ContractInput& input,
                                          const RuntimeContext& ctx);
    Result<ContractResult> handle_start(const ContractInput& input,
                                         const RuntimeContext& ctx);
    Result<ContractResult> handle_sign(const ContractInput& input,
                                        const RuntimeContext& ctx);
    Result<ContractResult> handle_execute(const ContractInput& input,
                                           const RuntimeContext& ctx);
    Result<ContractResult> handle_cancel(const ContractInput& input,
                                          const RuntimeContext& ctx);
    Result<ContractResult> handle_crl_revoke(const ContractInput& input,
                                              const RuntimeContext& ctx);
    Result<ContractResult> handle_crl_check(const ContractInput& input,
                                             const RuntimeContext& ctx);
    Result<ContractResult> handle_crl_sync(const ContractInput& input,
                                            const RuntimeContext& ctx);
    Result<ContractResult> handle_info(const ContractInput& input,
                                        const RuntimeContext& ctx);
};

} // namespace smo::runtime
