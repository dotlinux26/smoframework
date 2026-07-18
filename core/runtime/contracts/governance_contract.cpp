#include "governance_contract.hpp"

#include "core/governance/governance.hpp"
#include "core/authority/authority.hpp"
#include "core/identity/identity.hpp"

#include <sstream>
#include <iomanip>

namespace smo::runtime {

// ── Hex-to-NodeID helper ─────────────────────────────────────────────
static smo::NodeID parse_node_id(const std::string& hex) {
    smo::NodeID nid;
    for (size_t i = 0; i < 32 && i * 2 + 1 < hex.size(); ++i) {
        unsigned int byte = 0;
        std::istringstream iss(hex.substr(i * 2, 2));
        iss >> std::hex >> byte;
        nid.value[i] = static_cast<uint8_t>(byte);
    }
    return nid;
}

ContractMetadata GovernanceContract::default_metadata() {
    ContractMetadata meta;
    meta.id = "system.governance";
    meta.name = "Governance Contract";
    meta.version = "1.0.0";
    meta.api_version = 1;
    meta.author = "SMO Core";
    meta.description = "Manages mesh governance: proposals, voting, and commitment";
    meta.required_capabilities.set(static_cast<size_t>(ContractCapability::Crypto));
    meta.required_capabilities.set(static_cast<size_t>(ContractCapability::Governance));
    meta.max_execution_time_ns = 30'000'000'000;
    meta.tags = {"system", "governance"};
    meta.provides = {"propose", "vote", "commit", "list", "status", "info"};
    meta.entry_point = "system.governance";
    meta.has_validate = true;
    return meta;
}

GovernanceContract::GovernanceContract(smo::GovernanceEngine& engine,
                                       smo::authority::MeshAuthority& authority)
    : NativeContract(default_metadata())
    , engine_(engine)
    , authority_(authority) {}

Result<ContractResult> GovernanceContract::execute(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    if (input.method == "propose") return handle_propose(input, ctx);
    if (input.method == "vote")    return handle_vote(input, ctx);
    if (input.method == "commit")  return handle_commit(input, ctx);
    if (input.method == "list")    return handle_list(input, ctx);
    if (input.method == "status")  return handle_status(input, ctx);
    if (input.method == "info")    return handle_info(input, ctx);
    return ContractResult::denied("unknown method: " + input.method);
}

// ── handle_propose ──────────────────────────────────────────────────
Result<ContractResult> GovernanceContract::handle_propose(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;

    auto bytes_res = input.arguments.get<Bytes>();
    if (!bytes_res) {
        return ContractResult::denied("missing proposal data (expected serialized CBOR)");
    }

    auto proposal_res = smo::GovernanceProposal::deserialize(bytes_res.value());
    if (!proposal_res) {
        return ContractResult::denied("invalid proposal: " + proposal_res.error().message);
    }

    if (static_cast<uint8_t>(proposal_res.value().action) > 15) {
        return ContractResult::denied("unknown governance action");
    }

    auto id_res = engine_.submit(proposal_res.value());
    if (!id_res) {
        return ContractResult::denied("submit failed: " + id_res.error().message);
    }

    ContractResult result = ContractResult::ok("proposal submitted");
    result.metrics["proposal_id"] = ContextValue(
        static_cast<int64_t>(id_res.value().value));
    result.metrics["action"] = ContextValue(
        static_cast<int64_t>(static_cast<uint8_t>(proposal_res.value().action)));
    result.next_actions.push_back(
        emit_event("governance.proposal_created",
                    "id=" + std::to_string(id_res.value().value)));
    return result;
}

// ── handle_vote ────────────────────────────────────────────────────
Result<ContractResult> GovernanceContract::handle_vote(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;

    // Arguments: { proposal_id: int64, signature: Bytes }
    auto args = input.arguments;

    // Requester is the authority hex from context
    std::string authority_hex = ctx.info.requester;
    if (authority_hex.empty()) {
        return ContractResult::denied("missing authority (set requester in request)");
    }

    // Extract proposal_id
    auto pid_val = args.get<int64_t>();
    if (!pid_val) {
        return ContractResult::denied("missing proposal_id");
    }
    uint64_t pid = static_cast<uint64_t>(pid_val.value());

    // Extract raw signature bytes from arguments
    auto sig_bytes = args.get<Bytes>();
    if (!sig_bytes) {
        return ContractResult::denied("missing signature bytes");
    }

    BytesView sig_view(sig_bytes.value());
    auto sig_res = smo::GovernanceSignature::deserialize(sig_view);
    if (!sig_res) {
        return ContractResult::denied("invalid signature: " + sig_res.error().message);
    }

    smo::NodeID authority_id = parse_node_id(authority_hex);

    auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    auto sign_res = engine_.sign(
        smo::ProposalID{pid},
        authority_id,
        sig_res.value().signature,
        now_ns / 1'000'000'000LL);
    if (!sign_res) {
        return ContractResult::denied("vote failed: " + sign_res.error().message);
    }

    auto proposal = engine_.get(smo::ProposalID{pid});
    int sig_count = proposal ? static_cast<int>(proposal.value().signatures.size()) : 0;

    ContractResult result = ContractResult::ok("vote recorded");
    result.metrics["proposal_id"] = ContextValue(static_cast<int64_t>(pid));
    result.metrics["signatures"] = ContextValue(static_cast<int64_t>(sig_count));
    result.next_actions.push_back(
        emit_event("governance.vote_cast",
                    "proposal=" + std::to_string(pid)));
    return result;
}

// ── handle_commit ──────────────────────────────────────────────────
Result<ContractResult> GovernanceContract::handle_commit(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;

    auto pid_res = input.arguments.get<int64_t>();
    if (!pid_res) {
        return ContractResult::denied("missing proposal_id");
    }

    auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    auto res = engine_.commit(
        smo::ProposalID{static_cast<uint64_t>(pid_res.value())},
        now_ns / 1'000'000'000LL);
    if (!res) {
        return ContractResult::denied("commit failed: " + res.error().message);
    }

    ContractResult result = ContractResult::ok("proposal committed");
    result.metrics["proposal_id"] = ContextValue(pid_res.value());
    result.next_actions.push_back(
        emit_event("governance.proposal_committed",
                    "proposal=" + std::to_string(pid_res.value())));
    return result;
}

// ── handle_list ────────────────────────────────────────────────────
Result<ContractResult> GovernanceContract::handle_list(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)input;
    (void)ctx;

    auto proposals = engine_.pending();

    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < proposals.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{"
            << "\"id\":" << proposals[i].id.value << ","
            << "\"action\":" << static_cast<int>(proposals[i].action) << ","
            << "\"state\":" << static_cast<int>(proposals[i].state) << ","
            << "\"signatures\":" << proposals[i].signatures.size()
            << "}";
    }
    oss << "]";

    ContractResult result = ContractResult::ok(oss.str());
    result.metrics["count"] = ContextValue(static_cast<int64_t>(proposals.size()));
    return result;
}

// ── handle_status ──────────────────────────────────────────────────
Result<ContractResult> GovernanceContract::handle_status(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;

    auto pid_res = input.arguments.get<int64_t>();
    if (!pid_res) {
        return ContractResult::denied("missing proposal_id");
    }

    auto prop_res = engine_.get(smo::ProposalID{static_cast<uint64_t>(pid_res.value())});
    if (!prop_res) {
        return ContractResult::denied("proposal not found");
    }

    auto& p = prop_res.value();
    std::ostringstream oss;
    oss << "{"
        << "\"id\":" << p.id.value << ","
        << "\"action\":" << static_cast<int>(p.action) << ","
        << "\"state\":" << static_cast<int>(p.state) << ","
        << "\"signatures\":" << p.signatures.size() << ","
        << "\"threshold\":" << p.threshold
        << "}";

    ContractResult result = ContractResult::ok(oss.str());
    result.metrics["state"] = ContextValue(
        static_cast<int64_t>(static_cast<uint8_t>(p.state)));
    result.metrics["signatures"] = ContextValue(
        static_cast<int64_t>(p.signatures.size()));
    return result;
}

// ── handle_info ────────────────────────────────────────────────────
Result<ContractResult> GovernanceContract::handle_info(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)input;
    (void)ctx;

    ContractResult result = ContractResult::ok();
    result.data = R"({
        "contract": "system.governance",
        "version": "1.0.0",
        "methods": ["propose", "vote", "commit", "list", "status", "info"],
        "capabilities": ["crypto", "governance"]
    })";
    return result;
}

} // namespace smo::runtime
