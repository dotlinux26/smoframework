#include "recovery_contract.hpp"

#include <sstream>
#include <iomanip>
#include <chrono>
#include <cstdlib>

namespace smo::runtime {

// ── Map-extraction helpers ──────────────────────────────────────────
static Result<std::string> map_str(const ContextValue& args,
                                    const std::string& key)
{
    if (!args.is_map()) {
        return Error(ErrorCode{ErrorCategory::Runtime, 1002,
                                Severity::Error, RetryClass::NoRetry,
                                Recovery::None},
                     "arguments must be a map");
    }
    auto map = args.get<std::unordered_map<std::string, std::string>>();
    auto it = map.value().find(key);
    if (it == map.value().end()) {
        return Error(ErrorCode{ErrorCategory::Runtime, 1001,
                                Severity::Error, RetryClass::NoRetry,
                                Recovery::None},
                     "missing argument: " + key);
    }
    return it->second;
}

static Result<int64_t> map_int(const ContextValue& args,
                                const std::string& key)
{
    auto s = map_str(args, key);
    if (!s) return {s.error()};
    char* end = nullptr;
    int64_t val = std::strtoll(s.value().c_str(), &end, 10);
    if (end == s.value().c_str()) {
        return Error(ErrorCode{ErrorCategory::Runtime, 1001,
                                Severity::Error, RetryClass::NoRetry,
                                Recovery::None},
                     "invalid integer: " + key);
    }
    return val;
}

static Result<uint64_t> map_uint(const ContextValue& args,
                                  const std::string& key)
{
    auto v = map_int(args, key);
    if (!v) return {v.error()};
    if (v.value() < 0) {
        return Error(ErrorCode{ErrorCategory::Runtime, 1001,
                                Severity::Error, RetryClass::NoRetry,
                                Recovery::None},
                     "negative value for unsigned: " + key);
    }
    return static_cast<uint64_t>(v.value());
}

// ── Hex decode ──────────────────────────────────────────────────────
static Result<Bytes> hex_to_bytes(const std::string& hex) {
    if (hex.size() % 2 != 0) {
        return Error(ErrorCode{ErrorCategory::Runtime, 1001,
                                Severity::Error, RetryClass::NoRetry,
                                Recovery::None},
                     "invalid hex length");
    }
    Bytes out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        unsigned int byte = 0;
        std::istringstream iss(hex.substr(i, 2));
        iss >> std::hex >> byte;
        out.push_back(static_cast<uint8_t>(byte));
    }
    return out;
}

static std::string bytes_to_hex(const Bytes& data) {
    std::ostringstream oss;
    for (auto b : data) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(b);
    }
    return oss.str();
}

// ── Metadata ────────────────────────────────────────────────────────
ContractMetadata RecoveryContract::default_metadata() {
    ContractMetadata meta;
    meta.id = "system.recovery";
    meta.name = "Recovery Contract";
    meta.version = "1.0.0";
    meta.api_version = 1;
    meta.author = "SMO Core";
    meta.description = "Manages mesh recovery: session management, CRL operations, and epoch transitions";
    meta.required_capabilities.set(static_cast<size_t>(ContractCapability::Crypto));
    meta.required_capabilities.set(static_cast<size_t>(ContractCapability::Governance));
    meta.max_execution_time_ns = 30'000'000'000;
    meta.tags = {"system", "recovery"};
    meta.provides = {"assess", "start", "sign", "execute", "cancel",
                     "crl_revoke", "crl_check", "crl_sync", "info"};
    meta.entry_point = "system.recovery";
    meta.has_validate = true;
    return meta;
}

RecoveryContract::RecoveryContract(smo::recovery::RecoveryEngine& engine,
                                    smo::recovery::CRL* crl,
                                    smo::GovernanceEngine& governance_engine)
    : NativeContract(default_metadata())
    , engine_(engine)
    , crl_(crl)
    , governance_engine_(governance_engine) {}

Result<ContractResult> RecoveryContract::execute(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    if (input.method == "assess")     return handle_assess(input, ctx);
    if (input.method == "start")      return handle_start(input, ctx);
    if (input.method == "sign")       return handle_sign(input, ctx);
    if (input.method == "execute")    return handle_execute(input, ctx);
    if (input.method == "cancel")     return handle_cancel(input, ctx);
    if (input.method == "crl_revoke") return handle_crl_revoke(input, ctx);
    if (input.method == "crl_check")  return handle_crl_check(input, ctx);
    if (input.method == "crl_sync")   return handle_crl_sync(input, ctx);
    if (input.method == "info")       return handle_info(input, ctx);
    return ContractResult::denied("unknown method: " + input.method);
}

// ── handle_assess ───────────────────────────────────────────────────
Result<ContractResult> RecoveryContract::handle_assess(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;

    auto total = map_uint(input.arguments, "total_authorities");
    if (!total) return ContractResult::denied(total.error().message);

    auto online = map_uint(input.arguments, "online_authorities");
    if (!online) return ContractResult::denied(online.error().message);

    auto quorum = map_int(input.arguments, "quorum_threshold");
    if (!quorum) return ContractResult::denied(quorum.error().message);

    auto mode = engine_.assess_mode(
        static_cast<uint32_t>(total.value()),
        static_cast<uint32_t>(online.value()),
        static_cast<int>(quorum.value()));

    const char* mode_str = "none";
    switch (mode) {
        case smo::recovery::RecoveryMode::Soft: mode_str = "soft"; break;
        case smo::recovery::RecoveryMode::Hard: mode_str = "hard"; break;
        default: break;
    }

    ContractResult result = ContractResult::ok();
    result.metrics["mode"] = ContextValue(
        static_cast<int64_t>(static_cast<uint8_t>(mode)));
    result.data = std::string("{\"mode\":\"") + mode_str + "\"}";
    return result;
}

// ── handle_start ────────────────────────────────────────────────────
Result<ContractResult> RecoveryContract::handle_start(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;

    auto mode_str = map_str(input.arguments, "mode");
    if (!mode_str) return ContractResult::denied(mode_str.error().message);

    auto mesh_id = map_str(input.arguments, "mesh_id");
    if (!mesh_id) return ContractResult::denied(mesh_id.error().message);

    auto root_node_id = map_str(input.arguments, "root_node_id");
    if (!root_node_id) return ContractResult::denied(root_node_id.error().message);

    auto epoch = map_uint(input.arguments, "current_epoch");
    if (!epoch) return ContractResult::denied(epoch.error().message);

    auto passphrase = map_str(input.arguments, "recovery_passphrase");
    if (!passphrase) return ContractResult::denied(passphrase.error().message);

    auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    Result<smo::recovery::RecoverySession> session_res =
        (mode_str.value() == "hard")
            ? engine_.start_hard(mesh_id.value(), root_node_id.value(),
                                  epoch.value(), passphrase.value(), now_ns)
            : engine_.start_soft(mesh_id.value(), root_node_id.value(),
                                  epoch.value(), passphrase.value(), now_ns);

    if (!session_res) {
        return ContractResult::denied("start failed: " + session_res.error().message);
    }

    auto ser = session_res.value().serialize();
    if (!ser) {
        return ContractResult::denied("session serialization failed: " + ser.error().message);
    }

    ContractResult result = ContractResult::ok("recovery session started");
    result.metrics["mode"] = ContextValue(
        static_cast<int64_t>(static_cast<uint8_t>(session_res.value().mode)));
    result.metrics["state"] = ContextValue(
        static_cast<int64_t>(static_cast<uint8_t>(session_res.value().state)));
    result.metrics["session_id"] = ContextValue(session_res.value().session_id);
    result.metrics["new_epoch"] = ContextValue(
        static_cast<int64_t>(session_res.value().new_epoch));
    result.binary = ser.value();
    result.next_actions.push_back(
        emit_event("recovery.session_started",
                    "session=" + session_res.value().session_id));
    return result;
}

// ── handle_sign ─────────────────────────────────────────────────────
Result<ContractResult> RecoveryContract::handle_sign(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;

    auto session_hex = map_str(input.arguments, "session");
    if (!session_hex) return ContractResult::denied(session_hex.error().message);

    auto session_bytes = hex_to_bytes(session_hex.value());
    if (!session_bytes) return ContractResult::denied(session_bytes.error().message);

    auto session_res = smo::recovery::RecoverySession::deserialize(
        BytesView(session_bytes.value()));
    if (!session_res) {
        return ContractResult::denied("invalid session: " + session_res.error().message);
    }

    auto sig_hex = map_str(input.arguments, "signature");
    if (!sig_hex) return ContractResult::denied(sig_hex.error().message);

    auto sig_bytes = hex_to_bytes(sig_hex.value());
    if (!sig_bytes) return ContractResult::denied(sig_bytes.error().message);

    auto sig_res = smo::GovernanceSignature::deserialize(
        BytesView(sig_bytes.value()));
    if (!sig_res) {
        return ContractResult::denied("invalid signature: " + sig_res.error().message);
    }

    auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    auto add_res = engine_.add_signature(session_res.value(), sig_res.value(), now_ns);
    if (!add_res) {
        return ContractResult::denied("sign failed: " + add_res.error().message);
    }

    auto ser = session_res.value().serialize();
    if (!ser) {
        return ContractResult::denied("session serialization failed: " + ser.error().message);
    }

    ContractResult result = ContractResult::ok("signature added");
    result.metrics["session_id"] = ContextValue(session_res.value().session_id);
    result.metrics["signatures"] = ContextValue(
        static_cast<int64_t>(session_res.value().signatures.size()));
    result.binary = ser.value();
    return result;
}

// ── handle_execute ──────────────────────────────────────────────────
Result<ContractResult> RecoveryContract::handle_execute(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;

    auto session_hex = map_str(input.arguments, "session");
    if (!session_hex) return ContractResult::denied(session_hex.error().message);

    auto session_bytes = hex_to_bytes(session_hex.value());
    if (!session_bytes) return ContractResult::denied(session_bytes.error().message);

    auto session_res = smo::recovery::RecoverySession::deserialize(
        BytesView(session_bytes.value()));
    if (!session_res) {
        return ContractResult::denied("invalid session: " + session_res.error().message);
    }

    auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    auto exec_res = engine_.execute(session_res.value(), now_ns);
    if (!exec_res) {
        return ContractResult::denied("execute failed: " + exec_res.error().message);
    }

    auto ser = session_res.value().serialize();
    if (!ser) {
        return ContractResult::denied("session serialization failed: " + ser.error().message);
    }

    ContractResult result = ContractResult::ok("recovery executed");
    result.metrics["session_id"] = ContextValue(session_res.value().session_id);
    result.metrics["new_state"] = ContextValue(
        static_cast<int64_t>(static_cast<uint8_t>(session_res.value().state)));
    result.binary = ser.value();
    return result;
}

// ── handle_cancel ───────────────────────────────────────────────────
Result<ContractResult> RecoveryContract::handle_cancel(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;

    auto session_hex = map_str(input.arguments, "session");
    if (!session_hex) return ContractResult::denied(session_hex.error().message);

    auto session_bytes = hex_to_bytes(session_hex.value());
    if (!session_bytes) return ContractResult::denied(session_bytes.error().message);

    auto session_res = smo::recovery::RecoverySession::deserialize(
        BytesView(session_bytes.value()));
    if (!session_res) {
        return ContractResult::denied("invalid session: " + session_res.error().message);
    }

    auto cancel_res = engine_.cancel(session_res.value());
    if (!cancel_res) {
        return ContractResult::denied("cancel failed: " + cancel_res.error().message);
    }

    auto ser = session_res.value().serialize();
    if (!ser) {
        return ContractResult::denied("session serialization failed: " + ser.error().message);
    }

    ContractResult result = ContractResult::ok("recovery cancelled");
    result.metrics["session_id"] = ContextValue(session_res.value().session_id);
    result.binary = ser.value();
    return result;
}

// ── handle_crl_revoke ───────────────────────────────────────────────
// Now creates a GovernanceProposal (CertificateRevocation) instead of direct revoke.
// Governance must approve before CRL revocation.
Result<ContractResult> RecoveryContract::handle_crl_revoke(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;

    auto fingerprint = map_str(input.arguments, "cert_fingerprint");
    if (!fingerprint) return ContractResult::denied(fingerprint.error().message);

    auto node_id_hex = map_str(input.arguments, "node_id_hex");
    if (!node_id_hex) return ContractResult::denied(node_id_hex.error().message);

    auto reason = map_str(input.arguments, "reason");
    if (!reason) return ContractResult::denied(reason.error().message);

    auto epoch = map_uint(input.arguments, "epoch");
    if (!epoch) return ContractResult::denied(epoch.error().message);

    // Build GovernanceProposal for CertificateRevocation
    smo::GovernanceProposal proposal;
    proposal.tier = smo::GovernanceTier::Constitution;
    proposal.level = smo::GovernanceLevel::Critical;
    proposal.action = smo::GovernanceAction::CertificateRevocation;
    proposal.threshold = 1; // Will be overridden by engine based on N
    proposal.created_at = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    proposal.expires_at = proposal.created_at + 24 * 3600 * 1'000'000'000LL; // 24h TTL

    // Payload: JSON with fingerprint, node_id_hex, reason, epoch
    std::ostringstream payload_oss;
    payload_oss << "{"
        << "\"fingerprint\":\"" << fingerprint.value() << "\","
        << "\"node_id_hex\":\"" << node_id_hex.value() << "\","
        << "\"reason\":\"" << reason.value() << "\","
        << "\"epoch\":" << epoch.value()
        << "}";
    proposal.payload = Bytes(payload_oss.str().begin(), payload_oss.str().end());

    // Submit to GovernanceEngine
    auto id_res = governance_engine_.submit(proposal);
    if (!id_res) {
        return ContractResult::denied("submit failed: " + id_res.error().message);
    }

    ContractResult result = ContractResult::ok("revocation proposal submitted for governance");
    result.metrics["proposal_id"] = ContextValue(static_cast<int64_t>(id_res.value().value));
    result.metrics["cert_fingerprint"] = ContextValue(fingerprint.value());
    result.metrics["epoch"] = ContextValue(static_cast<int64_t>(epoch.value()));

    // Emit RecoveryProposalCreated event
    auto ev = make_event(
        EventType::RecoveryProposalCreated,
        "recovery_contract",
        std::to_string(id_res.value().value),
        "",
        "system.recovery",
        payload_oss.str()
    );
    // Note: EventBus publish would need access to runtime EventBus
    // For now, emit as next_action
    result.next_actions.push_back(
        emit_event("recovery.proposal_created",
                    "proposal_id=" + std::to_string(id_res.value().value) +
                    ",fingerprint=" + fingerprint.value()));

    return result;
}

// ── handle_crl_check ────────────────────────────────────────────────
Result<ContractResult> RecoveryContract::handle_crl_check(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;

    if (!crl_) return ContractResult::denied("CRL not available");

    auto fingerprint = map_str(input.arguments, "cert_fingerprint");
    if (!fingerprint) return ContractResult::denied(fingerprint.error().message);

    auto revoked_res = crl_->is_revoked(fingerprint.value());
    if (!revoked_res) {
        return ContractResult::denied("check failed: " + revoked_res.error().message);
    }

    ContractResult result = ContractResult::ok();
    result.metrics["revoked"] = ContextValue(static_cast<int64_t>(revoked_res.value()));
    result.data = revoked_res.value()
        ? std::string("{\"revoked\":true}")
        : std::string("{\"revoked\":false}");
    return result;
}

// ── handle_crl_sync ─────────────────────────────────────────────────
Result<ContractResult> RecoveryContract::handle_crl_sync(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)ctx;

    if (!crl_) return ContractResult::denied("CRL not available");

    auto epoch = map_uint(input.arguments, "epoch");
    if (!epoch) return ContractResult::denied(epoch.error().message);

    auto entries = crl_->entries_since(epoch.value());

    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{"
            << "\"fingerprint\":\"" << entries[i].cert_fingerprint << "\","
            << "\"node_id\":\"" << entries[i].node_id_hex << "\","
            << "\"reason\":\"" << entries[i].reason << "\","
            << "\"epoch\":" << entries[i].epoch
            << "}";
    }
    oss << "]";

    ContractResult result = ContractResult::ok(oss.str());
    result.metrics["count"] = ContextValue(static_cast<int64_t>(entries.size()));
    return result;
}

// ── handle_info ─────────────────────────────────────────────────────
Result<ContractResult> RecoveryContract::handle_info(
    const ContractInput& input,
    const RuntimeContext& ctx)
{
    (void)input;
    (void)ctx;

    ContractResult result = ContractResult::ok();
    result.data = R"({
        "contract": "system.recovery",
        "version": "1.0.0",
        "methods": ["assess", "start", "sign", "execute", "cancel",
                     "crl_revoke", "crl_check", "crl_sync", "info"],
        "capabilities": ["crypto", "governance"]
    })";
    return result;
}

} // namespace smo::runtime
