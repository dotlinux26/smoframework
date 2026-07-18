#pragma once

#include "core/runtime/contract_interface.hpp"
#include "core/runtime/runtime_context.hpp"

#include <string>
#include <memory>

namespace smo {
namespace bootstrap {
struct BootstrapRequest;
struct BootstrapResponse;
struct BootstrapSnapshot;
}
class MeshManager;
class GovernanceEngine;
namespace authority { class MeshAuthority; }
namespace recovery { class CRL; }
}

namespace smo::runtime {

class BootstrapContract : public NativeContract {
public:
    BootstrapContract(smo::MeshManager& mesh_mgr,
                      smo::authority::MeshAuthority& authority,
                      smo::GovernanceEngine* governance,
                      smo::recovery::CRL* crl);

    ContractCapabilities required_capabilities() const override {
        ContractCapabilities caps;
        caps.set(static_cast<size_t>(ContractCapability::Crypto));
        caps.set(static_cast<size_t>(ContractCapability::Network));
        return caps;
    }

    Result<ContractResult> execute(
        const ContractInput& input,
        const RuntimeContext& ctx) override;

    static ContractMetadata default_metadata();

private:
    smo::MeshManager& mesh_mgr_;
    smo::authority::MeshAuthority& authority_;
    smo::GovernanceEngine* governance_;
    smo::recovery::CRL* crl_;

    Result<ContractResult> handle_snapshot(const ContractInput& input, const RuntimeContext& ctx);
    Result<ContractResult> handle_request(const ContractInput& input, const RuntimeContext& ctx);
    Result<ContractResult> handle_info(const ContractInput& input, const RuntimeContext& ctx);
};

} // namespace smo::runtime
