#pragma once

#include "core/runtime/contract_interface.hpp"
#include "core/runtime/runtime_context.hpp"
#include "core/enroll/join_token.hpp"
#include "core/crypto/impl.hpp"

#include <string>
#include <memory>

namespace smo::runtime {

// ── JoinContract (Sprint 36D.1) ─────────────────────────────────────
// Processes join tokens and handles node enrollment.
//
// Methods:
//   "join"  — Parse + validate token, allocate identity, return credentials
//   "leave" — Handle node departure
//   "info"  — Return contract/service metadata
//
// Capabilities required: Crypto + Network
class JoinContract : public NativeContract {
public:
    JoinContract(const HashImpl& hash,
                 const SignerImpl& signer,
                 RngRef& rng);

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
    const HashImpl& hash_;
    const SignerImpl& signer_;
    RngRef& rng_;

    // Internal handlers
    Result<ContractResult> handle_join(const ContractInput& input, const RuntimeContext& ctx);
    Result<ContractResult> handle_leave(const ContractInput& input, const RuntimeContext& ctx);
    Result<ContractResult> handle_info(const ContractInput& input, const RuntimeContext& ctx);
};

} // namespace smo::runtime
