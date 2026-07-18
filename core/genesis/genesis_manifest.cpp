#include "genesis_manifest.hpp"

#include <algorithm>
#include <sstream>
#include <iomanip>

namespace smo::genesis {

// ── Serialize manifest to JSON-like string (v1) ─────────────────────
Result<Bytes> GenesisManifest::serialize() const {
    std::ostringstream oss;

    auto esc = [](const std::string& s) -> std::string {
        std::string out;
        out.reserve(s.size() + 2);
        out += '"';
        for (char c : s) {
            if (c == '"' || c == '\\') { out += '\\'; }
            out += c;
        }
        out += '"';
        return out;
    };

    oss << "{\n";
    oss << "  \"schema_version\": " << schema_version << ",\n";
    oss << "  \"mesh_id\": " << esc(mesh_id) << ",\n";
    oss << "  \"root_public_key\": " << esc(root_public_key) << ",\n";
    oss << "  \"manifest_version\": " << manifest_version << ",\n";
    oss << "  \"epoch\": " << epoch << ",\n";
    oss << "  \"state\": " << esc(state) << ",\n";
    oss << "  \"profile\": " << esc(to_string(profile)) << ",\n";
    oss << "  \"authorities\": {\n";
    oss << "    \"minimum\": " << authorities.minimum << ",\n";
    oss << "    \"preferred\": " << authorities.preferred << ",\n";
    oss << "    \"maximum\": " << authorities.maximum << "\n";
    oss << "  },\n";
    oss << "  \"quorum\": {\n";
    oss << "    \"authority_create\": " << esc(quorum.authority_create) << ",\n";
    oss << "    \"authority_revoke\": " << esc(quorum.authority_revoke) << ",\n";
    oss << "    \"policy_update\": " << esc(quorum.policy_update) << ",\n";
    oss << "    \"emergency_lockdown\": " << esc(quorum.emergency_lockdown) << ",\n";
    oss << "    \"epoch_rotate\": " << esc(quorum.epoch_rotate) << ",\n";
    oss << "    \"recovery\": " << esc(quorum.recovery) << "\n";
    oss << "  },\n";
    oss << "  \"fault_tolerance\": {\n";
    oss << "    \"max_authority_failures\": " << fault_tolerance.max_authority_failures << ",\n";
    oss << "    \"max_compromised\": " << fault_tolerance.max_compromised << "\n";
    oss << "  },\n";
    oss << "  \"created_at\": " << created_at << ",\n";
    oss << "  \"wizard_version\": " << wizard_version << "\n";
    oss << "}\n";

    std::string str = oss.str();
    return Bytes(str.begin(), str.end());
}

// ── Minimal JSON deserialization for v1 ────────────────────────────
static std::string json_str_value(const std::string& key, const std::string& json) {
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

static uint64_t json_int_value(const std::string& key, const std::string& json) {
    auto pos = json.find(key);
    if (pos == std::string::npos) return 0;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return 0;
    pos = json.find_first_of("0123456789", pos);
    if (pos == std::string::npos) return 0;
    char* end = nullptr;
    return strtoull(json.c_str() + pos, &end, 10);
}

Result<GenesisManifest> GenesisManifest::deserialize(BytesView data) {
    std::string json(reinterpret_cast<const char*>(data.data()), data.size());

    GenesisManifest m;
    m.schema_version  = (uint32_t)json_int_value("schema_version", json);
    m.mesh_id         = json_str_value("mesh_id", json);
    m.root_public_key = json_str_value("root_public_key", json);
    m.manifest_version= (uint32_t)json_int_value("manifest_version", json);
    m.epoch           = (uint32_t)json_int_value("epoch", json);
    m.state           = json_str_value("state", json);

    auto profile_str = json_str_value("profile", json);
    auto profile_res = deployment_profile_from_string(profile_str);
    if (!profile_res) {
        m.profile = DeploymentProfile::Enterprise;
    } else {
        m.profile = std::move(profile_res).value();
    }

    m.authorities.minimum   = (uint32_t)json_int_value("minimum", json);
    m.authorities.preferred = (uint32_t)json_int_value("preferred", json);
    m.authorities.maximum   = (uint32_t)json_int_value("maximum", json);

    m.quorum.authority_create   = json_str_value("authority_create", json);
    m.quorum.authority_revoke   = json_str_value("authority_revoke", json);
    m.quorum.policy_update      = json_str_value("policy_update", json);
    m.quorum.emergency_lockdown = json_str_value("emergency_lockdown", json);
    m.quorum.epoch_rotate       = json_str_value("epoch_rotate", json);
    m.quorum.recovery           = json_str_value("recovery", json);

    m.fault_tolerance.max_authority_failures = (uint32_t)json_int_value("max_authority_failures", json);
    m.fault_tolerance.max_compromised        = (uint32_t)json_int_value("max_compromised", json);

    m.created_at    = json_int_value("created_at", json);
    m.wizard_version = (uint32_t)json_int_value("wizard_version", json);

    if (m.mesh_id.empty()) {
        return SMO_ERR_GENESIS(1401, Error, NoRetry, ManualIntervention,
                               "mesh_id is required");
    }
    if (m.root_public_key.empty()) {
        return SMO_ERR_GENESIS(1401, Error, NoRetry, ManualIntervention,
                               "root_public_key is required");
    }

    return m;
}

} // namespace smo::genesis
