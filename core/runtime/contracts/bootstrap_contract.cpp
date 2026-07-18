#include "bootstrap_contract.hpp"

#include "core/bootstrap/bootstrap_protocol.hpp"
#include "core/bootstrap/bootstrap_snapshot.hpp"
#include "core/mesh/mesh_manager.hpp"
#include "core/authority/authority.hpp"
#include "core/governance/governance.hpp"
#include "core/recovery/crl.hpp"

namespace smo::runtime {

ContractMetadata BootstrapContract::default_metadata() {
    ContractMetadata meta;
    meta.id = "system.bootstrap";
    meta.name = "Bootstrap Contract";
    meta.version = "1.0.0";
    meta.api_version = 1;
    meta.author = "SMO Core";
    meta.description = "Provides mesh bootstrap snapshots to newly joined nodes";
    meta.required_capabilities.set(static_cast<size_t>(ContractCapability::Crypto));
    meta.required_capabilities.set(static_cast<size_t>(ContractCapability::Network));
    meta.max_execution_time_ns = 15'000'000'000;  // 15s
    meta.tags = {"system", "bootstrap"};
    meta.provides = {"snapshot", "request", "info"};
    meta.entry_point = "system.bootstrap";
    meta.has_initialize = false;
    meta.has_shutdown = false;
    meta.has_validate = true;
    return meta;
}

BootstrapContract::BootstrapContract(smo::MeshManager& mesh_mgr,
                                     smo::authority::MeshAuthority& authority,
                                     smo::GovernanceEngine* governance,
                                     smo::recovery::CRL* crl)
    : NativeContract(default_metadata())
    , mesh_mgr_(mesh_mgr)
    , authority_(authority)
    , governance_(governance)
    , crl_(crl) {}

Result<ContractResult> BootstrapContract::execute(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    if (input.method == "snapshot") {
        return handle_snapshot(input, ctx);
    }
    if (input.method == "request") {
        return handle_request(input, ctx);
    }
    if (input.method == "info") {
        return handle_info(input, ctx);
    }
    return ContractResult::denied("unknown method: " + input.method);
}

// ── handle_snapshot ──────────────────────────────────────────────────
// Build and return the current BootstrapSnapshot without a request context.
Result<ContractResult> BootstrapContract::handle_snapshot(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)input;
    (void)ctx;

    // Build a minimal bootstrap request to use handle_bootstrap_request
    smo::bootstrap::BootstrapRequest req;
    req.version = 1;
    req.nonce = {}; // zero nonce for direct snapshot

    auto resp_res = smo::bootstrap::handle_bootstrap_request(
        req, mesh_mgr_, authority_, governance_, crl_);
    if (!resp_res) {
        return ContractResult::denied("snapshot unavailable: " + resp_res.error().message);
    }

    auto& response = resp_res.value();

    // Serialize the snapshot to CBOR bytes
    Bytes snapshot_cbor = response.snapshot.encode_cbor();

    ContractResult result = ContractResult::ok();
    result.data = "bootstrap snapshot";
    result.binary = std::move(snapshot_cbor);
    result.metrics["epoch"] = ContextValue(static_cast<int64_t>(response.snapshot.epoch));
    result.metrics["mesh_state"] = ContextValue(response.snapshot.mesh_state);
    result.metrics["authority_count"] = ContextValue(
        static_cast<int64_t>(response.snapshot.authorities.size()));

    result.next_actions.push_back(
        emit_event("bootstrap.snapshot_delivered",
                    "epoch=" + std::to_string(response.snapshot.epoch)));

    return result;
}

// ── handle_request ───────────────────────────────────────────────────
// Parse a BootstrapRequest from input arguments, build the response.
Result<ContractResult> BootstrapContract::handle_request(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;

    // Extract the serialized BootstrapRequest CBOR from arguments
    auto cbor_res = input.arguments.get<Bytes>();
    if (!cbor_res) {
        return ContractResult::denied("missing bootstrap request CBOR data");
    }

    // Decode the BootstrapRequest
    auto req_res = smo::bootstrap::BootstrapRequest::decode_cbor(cbor_res.value());
    if (!req_res) {
        return ContractResult::denied("invalid bootstrap request: " + req_res.error().message);
    }

    // Build the response using existing logic
    auto resp_res = smo::bootstrap::handle_bootstrap_request(
        req_res.value(), mesh_mgr_, authority_, governance_, crl_);
    if (!resp_res) {
        return ContractResult::denied("bootstrap failed: " + resp_res.error().message);
    }

    // Serialize the full response to CBOR
    Bytes response_cbor = resp_res.value().encode_cbor();

    ContractResult result = ContractResult::ok();
    result.data = "bootstrap response";
    result.binary = std::move(response_cbor);
    result.metrics["epoch"] = ContextValue(static_cast<int64_t>(resp_res.value().snapshot.epoch));

    result.next_actions.push_back(
        emit_event("bootstrap.request_handled",
                    "epoch=" + std::to_string(resp_res.value().snapshot.epoch)));

    return result;
}

// ── handle_info ──────────────────────────────────────────────────────
Result<ContractResult> BootstrapContract::handle_info(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)input;
    (void)ctx;

    ContractResult result = ContractResult::ok();
    result.data = R"({
        "contract": "system.bootstrap",
        "version": "1.0.0",
        "methods": ["snapshot", "request", "info"],
        "capabilities": ["crypto", "network"]
    })";
    return result;
}

} // namespace smo::runtime
