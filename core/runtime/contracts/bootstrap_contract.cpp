#include "bootstrap_contract.hpp"

#include "core/bootstrap/bootstrap_protocol.hpp"
#include "core/bootstrap/bootstrap_snapshot.hpp"
#include "core/join/join_protocol.hpp"
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
    meta.provides = {"snapshot", "request", "info", "bootstrap_sync"};
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
    if (input.method == "bootstrap_sync") {
        return handle_bootstrap_sync(input, ctx);
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

// ── handle_bootstrap_sync ───────────────────────────────────────────
// Delta sync for post-join bootstrap (opcode 0x0603/0x0604, per DISCUSSION_0039 §5.4)
Result<ContractResult> BootstrapContract::handle_bootstrap_sync(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;

    auto cbor_res = input.arguments.get<Bytes>();
    if (!cbor_res) {
        return ContractResult::denied("missing bootstrap sync request CBOR data");
    }

    auto req_res = join::BootstrapSyncRequest::decode_cbor(BytesView(cbor_res.value()));
    if (!req_res) {
        return ContractResult::denied("invalid bootstrap sync request: " + req_res.error().message);
    }
    const auto& req = req_res.value();

    // Get current mesh context for epoch comparison
    auto mesh_res = mesh_mgr_.get_current_mesh();
    if (!mesh_res) {
        return ContractResult::denied("no active mesh for bootstrap sync");
    }
    auto& ctx_mesh = mesh_res.value();
    auto& cfg = ctx_mesh->config;
    uint64_t current_epoch = static_cast<uint64_t>(cfg.epoch);

    join::BootstrapSyncResponse resp;
    resp.nonce = req.nonce;

    // Always send current epochs so joiner knows latest state
    resp.manifest_epoch = current_epoch;

    // ── Manifest delta ────────────────────────────────────────────
    if (req.manifest_epoch < current_epoch) {
        cbor::Encoder man_enc;
        man_enc.encode_map(4);
        man_enc.encode_uint(1); man_enc.encode_string(cfg.mesh_id);
        man_enc.encode_uint(2); man_enc.encode_uint(current_epoch);
        man_enc.encode_uint(3); man_enc.encode_string(cfg.display_name.empty() ? cfg.mesh_id : cfg.display_name);
        man_enc.encode_uint(4);
        man_enc.encode_array(cfg.bootstrap_endpoints.size());
        for (auto& ep : cfg.bootstrap_endpoints) {
            man_enc.encode_string(ep);
        }
        resp.manifest_delta = man_enc.take();
    }

    // ── Membership delta ──────────────────────────────────────────
    uint64_t membership_epoch = current_epoch;
    resp.membership_epoch = membership_epoch;
    if (req.membership_epoch < membership_epoch) {
        auto nodes_res = authority_.registry().list_nodes(cfg.mesh_id);
        if (nodes_res) {
            cbor::Encoder mem_enc;
            auto& nodes = nodes_res.value();
            mem_enc.encode_array(nodes.size());
            for (auto& node : nodes) {
                mem_enc.encode_map(5);
                mem_enc.encode_uint(1); mem_enc.encode_string(node.node_id_hex);
                mem_enc.encode_uint(2); mem_enc.encode_string(node.role);
                mem_enc.encode_uint(3); mem_enc.encode_string(node.status);
                mem_enc.encode_uint(4); mem_enc.encode_uint(node.epoch);
                mem_enc.encode_uint(5); mem_enc.encode_int(node.last_seen);
            }
            resp.membership_delta = mem_enc.take();
        }
    }

    // ── CRL delta ─────────────────────────────────────────────────
    uint64_t crl_epoch = current_epoch;
    resp.crl_epoch = crl_epoch;
    if (crl_ && req.crl_epoch < crl_epoch) {
        auto entries = crl_->entries_since(req.crl_epoch);
        if (!entries.empty()) {
            cbor::Encoder crl_enc;
            crl_enc.encode_array(entries.size());
            for (auto& entry : entries) {
                crl_enc.encode_map(4);
                crl_enc.encode_uint(1); crl_enc.encode_string(entry.cert_fingerprint);
                crl_enc.encode_uint(2); crl_enc.encode_string(entry.node_id_hex);
                crl_enc.encode_uint(3); crl_enc.encode_uint(entry.epoch);
                crl_enc.encode_uint(4); crl_enc.encode_int(entry.revoked_at);
            }
            resp.crl_delta = crl_enc.take();
        }
    }

    // ── Policy delta ──────────────────────────────────────────────
    uint64_t policy_version = current_epoch;
    resp.policy_version = policy_version;
    if (req.policy_version < policy_version && governance_) {
        auto pending = governance_->pending();
        if (!pending.empty()) {
            cbor::Encoder pol_enc;
            pol_enc.encode_array(pending.size());
            for (auto& prop : pending) {
                pol_enc.encode_map(3);
                pol_enc.encode_uint(1); pol_enc.encode_uint(prop.id.value);
                pol_enc.encode_uint(2); pol_enc.encode_int(prop.created_at);
                pol_enc.encode_uint(3); pol_enc.encode_uint(static_cast<uint64_t>(prop.tier));
            }
            resp.policy_delta = pol_enc.take();
        }
    }

    // Serialize and return
    Bytes response_cbor = resp.encode_cbor();

    ContractResult result = ContractResult::ok();
    result.data = "bootstrap sync response";
    result.binary = std::move(response_cbor);
    result.metrics["manifest_epoch"] = ContextValue(static_cast<int64_t>(resp.manifest_epoch));
    result.metrics["membership_epoch"] = ContextValue(static_cast<int64_t>(resp.membership_epoch));
    result.metrics["crl_epoch"] = ContextValue(static_cast<int64_t>(resp.crl_epoch));
    result.metrics["policy_version"] = ContextValue(static_cast<int64_t>(resp.policy_version));

    result.next_actions.push_back(
        emit_event("bootstrap.sync_delivered",
                    "manifest_epoch=" + std::to_string(resp.manifest_epoch)));

    return result;
}

} // namespace smo::runtime
