#include "recovery_engine.hpp"

#include <sstream>
#include <random>

namespace smo::recovery {

// ── serialize / deserialize helpers ─────────────────────────────────
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

// ── RecoverySession serialization ──────────────────────────────────
Result<Bytes> RecoverySession::serialize() const {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"mode\": " << (int)mode << ",\n";
    oss << "  \"state\": " << (int)state << ",\n";
    oss << "  \"session_id\": " << json_esc(session_id) << ",\n";
    oss << "  \"mesh_id\": " << json_esc(mesh_id) << ",\n";
    oss << "  \"root_node_id\": " << json_esc(root_node_id) << ",\n";
    oss << "  \"new_epoch\": " << new_epoch << ",\n";
    oss << "  \"manifest_version\": " << manifest_version << ",\n";
    oss << "  \"created_at\": " << created_at << ",\n";
    oss << "  \"expires_at\": " << expires_at << ",\n";
    oss << "  \"signatures\": [],\n";
    oss << "  \"recovery_token\": " << json_esc(bytes_to_hex(recovery_token)) << "\n";
    oss << "}\n";
    std::string str = oss.str();
    return Bytes(str.begin(), str.end());
}

Result<RecoverySession> RecoverySession::deserialize(BytesView data) {
    std::string json(reinterpret_cast<const char*>(data.data()), data.size());
    RecoverySession s;
    s.mode           = (RecoveryMode)json_val_int("mode", json);
    s.state          = (RecoveryState)json_val_int("state", json);
    s.session_id     = json_val_str("session_id", json);
    s.mesh_id        = json_val_str("mesh_id", json);
    s.root_node_id   = json_val_str("root_node_id", json);
    s.new_epoch      = json_val_int("new_epoch", json);
    s.manifest_version = (uint32_t)json_val_int("manifest_version", json);
    s.created_at     = json_val_int("created_at", json);
    s.expires_at     = json_val_int("expires_at", json);

    auto token_hex = json_val_str("recovery_token", json);
    if (!token_hex.empty()) {
        s.recovery_token.resize(token_hex.size() / 2);
        for (size_t i = 0; i < token_hex.size(); i += 2)
            s.recovery_token[i / 2] = (uint8_t)strtoul(token_hex.substr(i, 2).c_str(), nullptr, 16);
    }
    return s;
}

RecoveryEngine::RecoveryEngine(RecoveryConfig config)
    : config_(std::move(config))
{}

RecoveryMode RecoveryEngine::assess_mode(uint32_t total,
                                          uint32_t online,
                                          int quorum_threshold) const {
    if (online == 0) return RecoveryMode::Hard;
    if (static_cast<int>(online) < quorum_threshold) return RecoveryMode::Hard;
    if (online < total) return RecoveryMode::Soft;
    return RecoveryMode::None;
}

static std::string generate_session_id() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream oss;
    oss << "SMO-REC-" << std::hex << dist(gen) << dist(gen);
    return oss.str();
}

Result<RecoverySession> RecoveryEngine::start_soft(
    const std::string& mesh_id,
    const std::string& root_node_id,
    uint64_t current_epoch,
    const std::string& recovery_passphrase,
    int64_t now_ns)
{
    auto pkg_valid = verify_recovery_package(recovery_passphrase);
    if (!pkg_valid) {
        return SMO_ERR_RECOVERY(1503, Error, NoRetry, ManualIntervention,
                                "recovery package passphrase mismatch");
    }

    RecoverySession session;
    session.mode       = RecoveryMode::Soft;
    session.state      = RecoveryState::AwaitingRoot;
    session.session_id = generate_session_id();
    session.mesh_id    = mesh_id;
    session.root_node_id = root_node_id;
    session.new_epoch  = current_epoch + 1;
    session.created_at = now_ns;
    session.expires_at = now_ns + static_cast<int64_t>(config_.session_ttl_ns);

    return session;
}

Result<RecoverySession> RecoveryEngine::start_hard(
    const std::string& mesh_id,
    const std::string& root_node_id,
    uint64_t current_epoch,
    const std::string& recovery_passphrase,
    int64_t now_ns)
{
    auto pkg_valid = verify_recovery_package(recovery_passphrase);
    if (!pkg_valid) {
        return SMO_ERR_RECOVERY(1503, Error, NoRetry, ManualIntervention,
                                "recovery package passphrase mismatch");
    }

    RecoverySession session;
    session.mode       = RecoveryMode::Hard;
    session.state      = RecoveryState::AwaitingRoot;
    session.session_id = generate_session_id();
    session.mesh_id    = mesh_id;
    session.root_node_id = root_node_id;
    session.new_epoch  = current_epoch + 1;
    session.created_at = now_ns;
    session.expires_at = now_ns + static_cast<int64_t>(config_.session_ttl_ns);

    return session;
}

Result<void> RecoveryEngine::add_signature(RecoverySession& session,
                                             const GovernanceSignature& sig,
                                             int64_t now_ns)
{
    if (!session.is_valid(now_ns)) {
        session.state = RecoveryState::Failed;
        return SMO_ERR_RECOVERY(1501, Warn, NoRetry, ManualIntervention,
                                "recovery session expired");
    }
    if (session.state != RecoveryState::AwaitingRoot &&
        session.state != RecoveryState::Signing) {
        return SMO_ERR_RECOVERY(1502, Error, NoRetry, ManualIntervention,
                                "session not in signable state");
    }

    session.signatures.push_back(sig);
    session.state = RecoveryState::Signing;
    return {};
}

Result<void> RecoveryEngine::execute(RecoverySession& session, int64_t now_ns) {
    if (!session.is_valid(now_ns)) {
        session.state = RecoveryState::Failed;
        return SMO_ERR_RECOVERY(1501, Warn, NoRetry, ManualIntervention,
                                "recovery session expired");
    }
    if (session.state != RecoveryState::Signing && session.state != RecoveryState::AwaitingRoot) {
        return SMO_ERR_RECOVERY(1502, Error, NoRetry, ManualIntervention,
                                "session not ready for execution");
    }

    // Hard recovery: invalidate all certs by incrementing epoch
    if (session.mode == RecoveryMode::Hard) {
        // epoch++ already set in new_epoch
        // Invalidation happens when nodes compare their epoch against session.new_epoch
        session.state = RecoveryState::Complete;
        return {};
    }

    session.state = RecoveryState::Complete;
    return {};
}

Result<void> RecoveryEngine::cancel(RecoverySession& session) {
    session.state = RecoveryState::Failed;
    return {};
}

Result<bool> RecoveryEngine::verify_recovery_package(const std::string& passphrase) {
    (void)passphrase;
    // TODO: actual recovery package verification
    return true;
}

} // namespace smo::recovery
