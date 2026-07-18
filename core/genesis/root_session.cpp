#include "root_session.hpp"

#include <sstream>
#include <random>

namespace smo::genesis {

static std::string generate_session_id() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream oss;
    oss << std::hex << dist(gen) << dist(gen);
    return oss.str();
}

static std::string json_esc(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

static std::string json_val_str(const std::string& key, const std::string& json) {
    auto pos = json.find(key);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos);
    if (pos == std::string::npos) return {};
    pos = json.find_first_of('"', pos);
    if (pos == std::string::npos) return {};
    ++pos;
    auto end = json.find('"', pos);
    if (end == std::string::npos) return {};
    return json.substr(pos, end - pos);
}

static uint64_t json_val_int(const std::string& key, const std::string& json) {
    auto pos = json.find(key);
    if (pos == std::string::npos) return 0;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return 0;
    pos = json.find_first_of("0123456789", pos);
    if (pos == std::string::npos) return 0;
    char* end = nullptr;
    return strtoull(json.c_str() + pos, &end, 10);
}

// ── RootSession ───────────────────────────────────────────────────────

void RootSession::emit(const AuditEvent& ev) {
    if (audit_sink) {
        audit_sink(ev);
    }
}

void RootSession::activate(const std::string& sid,
                            const std::string& node_id,
                            const std::string& pubkey,
                            std::unique_ptr<crypto::SignerContext> ctx,
                            SessionPolicy pol,
                            AuditSink sink,
                            uint64_t now_ns,
                            uint64_t ttl_ns)
{
    session_id     = sid;
    root_node_id   = node_id;
    root_public_key = pubkey;
    if (ctx) signer = std::move(ctx);
    // If ctx is null, keep existing signer (set by unlock())
    policy         = std::move(pol);
    audit_sink     = std::move(sink);
    state_         = RootSessionState::Active;
    created_at_    = now_ns;
    expires_at_    = now_ns + ttl_ns;
    consumed_at_   = 0;
    audit_counter_ = 1;

    AuditEvent ev;
    ev.audit_id     = next_audit_id();
    ev.timestamp_ns = now_ns;
    ev.operation    = RootOperation::SignJoinToken;
    ev.reason       = "Session opened";
    ev.success      = true;
    ev.details      = "root_node=" + node_id;
    emit(ev);
}

void RootSession::destroy() {
    if (signer) {
        signer->destroy();
    }
    signer.reset();

    if (state_ != RootSessionState::Consumed) {
        auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        AuditEvent ev;
        ev.audit_id     = next_audit_id();
        ev.timestamp_ns = now;
        ev.operation    = RootOperation::SignJoinToken;
        ev.reason       = "Session destroyed";
        ev.success      = true;
        emit(ev);
    }

    state_ = RootSessionState::Consumed;
}

Result<void> RootSession::consume(uint64_t now_ns) {
    if (state_ != RootSessionState::Active) {
        return SMO_ERR_GENESIS(1406, Error, NoRetry, ManualIntervention,
                               "session not active");
    }

    AuditEvent ev;
    ev.audit_id     = next_audit_id();
    ev.timestamp_ns = now_ns;
    ev.operation    = RootOperation::SignJoinToken;
    ev.reason       = "Session consumed";
    ev.success      = true;
    emit(ev);

    if (signer) signer->destroy();
    signer.reset();
    state_      = RootSessionState::Consumed;
    consumed_at_ = now_ns;
    return {};
}

Result<RootResult> RootSession::execute(const RootRequest& req,
                                         RngRef& rng,
                                         uint64_t now_ns)
{
    if (state_ != RootSessionState::Active) {
        return SMO_ERR_GENESIS(1406, Error, NoRetry, ManualIntervention,
                               "RootSession not active");
    }
    if (!is_valid(now_ns)) {
        state_ = RootSessionState::Expired;
        if (signer) signer->destroy();
        signer.reset();
        return SMO_ERR_GENESIS(1405, Warn, NoRetry, ManualIntervention,
                               "RootSession expired");
    }
    if (!signer || !signer->valid()) {
        return SMO_ERR_GENESIS(1406, Error, NoRetry, ManualIntervention,
                               "RootSession signer context invalid");
    }

    // ── 1. Policy check ────────────────────────────────────────────
    if (!policy.check(req.operation)) {
        AuditEvent fail_ev;
        fail_ev.audit_id     = next_audit_id();
        fail_ev.timestamp_ns = now_ns;
        fail_ev.operation    = req.operation;
        fail_ev.reason       = req.reason.empty() ? "execute" : req.reason;
        fail_ev.success      = false;
        fail_ev.details      = "policy denied";
        emit(fail_ev);
        return SMO_ERR_GENESIS(1406, Error, NoRetry, ManualIntervention,
                               "Operation not permitted by session policy");
    }

    // ── 2. Execute via SignerContext ────────────────────────────────
    auto sign_result = [&]() -> Result<Bytes> {
        switch (req.operation) {
            case RootOperation::SignJoinToken:
            case RootOperation::SignBootstrapCSR:
            case RootOperation::SignRecovery:
                return signer->sign(BytesView(req.payload), rng);

            case RootOperation::IssueRecovery:
            case RootOperation::RotateRoot:
            case RootOperation::EmergencyLockdown:
            case RootOperation::ResetEpoch:
            case RootOperation::ConstitutionChange:
                return SMO_ERR_GENESIS(1400, Error, NoRetry, ManualIntervention,
                                       "RootOperation not yet implemented");
        }
        return SMO_ERR_GENESIS(1400, Error, NoRetry, ManualIntervention,
                               "Unknown RootOperation");
    }();

    if (!sign_result) {
        AuditEvent fail_ev;
        fail_ev.audit_id     = next_audit_id();
        fail_ev.timestamp_ns = now_ns;
        fail_ev.operation    = req.operation;
        fail_ev.reason       = req.reason.empty() ? "execute" : req.reason;
        fail_ev.success      = false;
        fail_ev.details      = sign_result.error().message;
        emit(fail_ev);
        return sign_result.error();
    }

    // ── 3. Audit success ────────────────────────────────────────────
    {
        AuditEvent ev;
        ev.audit_id     = next_audit_id();
        ev.timestamp_ns = now_ns;
        ev.operation    = req.operation;
        ev.reason       = req.reason.empty() ? "execute" : req.reason;
        ev.success      = true;
        if (!req.mesh_id.empty()) ev.details = "mesh=" + req.mesh_id;
        if (!req.requester.empty()) {
            if (!ev.details.empty()) ev.details += ";";
            ev.details += "requester=" + req.requester;
        }
        emit(ev);
    }

    RootResult res;
    res.success = true;
    res.output  = std::move(sign_result).value();
    return res;
}

Result<Bytes> RootSession::serialize() const {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"session_id\": " << json_esc(session_id) << ",\n";
    oss << "  \"state\": " << (int)state_ << ",\n";
    oss << "  \"root_node_id\": " << json_esc(root_node_id) << ",\n";
    oss << "  \"root_public_key\": " << json_esc(root_public_key) << ",\n";
    oss << "  \"created_at\": " << created_at_ << ",\n";
    oss << "  \"expires_at\": " << expires_at_ << ",\n";
    oss << "  \"consumed_at\": " << consumed_at_ << "\n";
    oss << "}\n";
    std::string str = oss.str();
    return Bytes(str.begin(), str.end());
}

Result<RootSession> RootSession::deserialize(BytesView data) {
    std::string json(reinterpret_cast<const char*>(data.data()), data.size());
    RootSession s;
    s.session_id     = json_val_str("session_id", json);
    s.state_         = (RootSessionState)json_val_int("state", json);
    s.root_node_id   = json_val_str("root_node_id", json);
    s.root_public_key = json_val_str("root_public_key", json);
    s.created_at_    = json_val_int("created_at", json);
    s.expires_at_    = json_val_int("expires_at", json);
    s.consumed_at_   = json_val_int("consumed_at", json);
    return s;
}

// ── RootSessionManager ────────────────────────────────────────────────

Result<std::string> RootSessionManager::start_session(
    const std::string& root_node_id,
    const std::string& root_public_key,
    std::unique_ptr<crypto::SignerContext> signer,
    SessionPolicy policy,
    AuditSink audit_sink,
    uint64_t now_ns)
{
    if (session.state() == RootSessionState::Active && !session.is_valid(now_ns)) {
        // Expired — will be replaced by open() below
    }

    auto sid = generate_session_id();
    session.activate(
        sid, root_node_id, root_public_key,
        std::move(signer), std::move(policy),
        std::move(audit_sink), now_ns, ttl_ns
    );

    return session.session_id;
}

Result<void> RootSessionManager::validate_session(const std::string& session_id,
                                                    uint64_t now_ns) {
    if (session.session_id != session_id) {
        return SMO_ERR_GENESIS(1406, Error, NoRetry, ManualIntervention,
                               "session ID mismatch");
    }
    if (session.state() != RootSessionState::Active) {
        return SMO_ERR_GENESIS(1406, Error, NoRetry, ManualIntervention,
                               "session not active");
    }
    if (!session.is_valid(now_ns)) {
        if (session.signer) session.signer->destroy();
        session.signer.reset();
        return SMO_ERR_GENESIS(1405, Warn, NoRetry, ManualIntervention,
                               "root session expired");
    }
    return {};
}

Result<void> RootSessionManager::expire_session(uint64_t now_ns) {
    if (session.signer) session.signer->destroy();
    session.signer.reset();
    return session.consume(now_ns);
}

} // namespace smo::genesis
