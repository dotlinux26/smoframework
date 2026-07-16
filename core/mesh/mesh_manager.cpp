#include "mesh_manager.hpp"

#include "core/crypto/registry.hpp"
#include "core/enroll/join_token.hpp"

#include <blake3.h>

#include <filesystem>
#include <sqlite3.h>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <array>
#include <fstream>

namespace smo {

// Simple JSON field readers (no JSON library dependency)
static std::string json_read_string(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return {};
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return {};
    auto start = json.find('"', colon + 1);
    if (start == std::string::npos) return {};
    auto end = json.find('"', start + 1);
    if (end == std::string::npos) return {};
    return json.substr(start + 1, end - start - 1);
}

static int64_t json_read_int(const std::string& json, const std::string& key, int64_t def) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return def;
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return def;
    auto start = json.find_first_of("0123456789-", colon);
    if (start == std::string::npos) return def;
    auto end = json.find_first_not_of("0123456789", start);
    if (end == std::string::npos) return def;
    try { return std::stoll(json.substr(start, end - start)); } catch (...) { return def; }
}

static const char kMeshSchema[] = R"(
    CREATE TABLE IF NOT EXISTS meshes (
        mesh_id TEXT PRIMARY KEY,
        display_name TEXT NOT NULL,
        authority_pubkey TEXT NOT NULL,
        root_pubkey TEXT NOT NULL,
        epoch INTEGER NOT NULL DEFAULT 1,
        created_at INTEGER NOT NULL,
        config_json TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_meshes_name ON meshes(display_name);
)";

static std::string generate_mesh_id(const MeshConfig& config) {
    std::string canonical = config.root_pubkey + "|" +
        std::to_string(config.created_at) + "|" + std::to_string(std::rand());
    std::array<uint8_t, 32> hash;
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, canonical.data(), canonical.size());
    blake3_hasher_finalize(&hasher, hash.data(), 32);
    std::ostringstream ss;
    for (uint8_t b : hash) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    }
    return ss.str();
}

static MeshPaths make_mesh_paths(const std::string& base_dir, const std::string& mesh_id) {
    MeshPaths p;
    p.mesh_dir = base_dir + "/meshes/" + mesh_id;
    p.mesh_json = p.mesh_dir + "/mesh.json";
    p.cert_path = p.mesh_dir + "/cert.smoc";
    p.identity_json = p.mesh_dir + "/identity.json";
    p.peers_db = p.mesh_dir + "/peers.db";
    p.audit_db = p.mesh_dir + "/audit.db";
    p.contract_db = p.mesh_dir + "/contract.db";
    p.policy_dir = p.mesh_dir + "/policies";
    p.contracts_dir = p.mesh_dir + "/contracts";
    p.cache_dir = p.mesh_dir + "/cache";
    p.dumps_dir = p.mesh_dir + "/dumps";
    p.workflow_dir = p.mesh_dir + "/workflows";
    return p;
}

static void ensure_dirs(const MeshPaths& p) {
    std::error_code ec;
    std::filesystem::create_directories(p.mesh_dir, ec);
    std::filesystem::create_directories(p.policy_dir, ec);
    std::filesystem::create_directories(p.contracts_dir, ec);
    std::filesystem::create_directories(p.cache_dir, ec);
    std::filesystem::create_directories(p.dumps_dir, ec);
    std::filesystem::create_directories(p.workflow_dir, ec);
}

static std::string escape_json(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

static std::string serialize_config(const MeshConfig& config) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"mesh_id\":\"" << escape_json(config.mesh_id) << "\",";
    oss << "\"display_name\":\"" << escape_json(config.display_name) << "\",";
    oss << "\"authority_pubkey\":\"" << config.authority_pubkey << "\",";
    oss << "\"root_pubkey\":\"" << config.root_pubkey << "\",";
    oss << "\"hmac_secret\":\"" << config.hmac_secret << "\",";
    oss << "\"cipher_suite_id\":" << (int)config.cipher_suite_id << ",";
    oss << "\"epoch\":" << config.epoch << ",";
    oss << "\"created_at\":" << config.created_at << ",";
    oss << "\"listen_address\":\"" << escape_json(config.listen_address) << "\",";
    oss << "\"bootstrap_configured\":" << (config.bootstrap_configured ? "true" : "false") << ",";
    // advertise_addresses array
    oss << "\"advertise_addresses\":[";
    for (size_t i = 0; i < config.advertise_addresses.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << escape_json(config.advertise_addresses[i]) << "\"";
    }
    oss << "],";
    // bootstrap_endpoints array
    oss << "\"bootstrap_endpoints\":[";
    for (size_t i = 0; i < config.bootstrap_endpoints.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << escape_json(config.bootstrap_endpoints[i]) << "\"";
    }
    oss << "]";
    oss << "}";
    return oss.str();
}

class MeshManager::Impl {
public:
    Config config_;
    sqlite3* db_ = nullptr;
    std::string current_mesh_id_;
    std::unordered_map<std::string, std::shared_ptr<MeshContext>> cache_;

    Impl(const Config& config) : config_(config) {}

    ~Impl() { close(); }

    Result<void> open() {
        std::error_code ec;
        std::filesystem::create_directories(config_.base_data_dir, ec);
        if (ec) {
            return SMO_ERR_STORAGE(900, Critical, NoRetry, RebootNode,
                                   "Failed to create base directory: " + ec.message());
        }
        std::string db_path = config_.base_data_dir + "/catalog.db";
        int rc = sqlite3_open_v2(db_path.c_str(), &db_,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                                 nullptr);
        if (rc != SQLITE_OK) {
            return SMO_ERR_STORAGE(900, Critical, NoRetry, RebootNode,
                                   "Failed to open catalog DB: " + std::string(sqlite3_errmsg(db_)));
        }
        char* err = nullptr;
        rc = sqlite3_exec(db_, "PRAGMA journal_mode = WAL;", nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "unknown";
            sqlite3_free(err);
            sqlite3_close(db_); db_ = nullptr;
            return SMO_ERR_STORAGE(900, Critical, NoRetry, RebootNode, msg);
        }
        rc = sqlite3_exec(db_, kMeshSchema, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "unknown";
            sqlite3_free(err);
            sqlite3_close(db_); db_ = nullptr;
            return SMO_ERR_STORAGE(900, Critical, NoRetry, RebootNode, msg);
        }
        ensure_default_mesh();
        return {};
    }

    void close() {
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
    }

    Result<void> create_mesh(const MeshConfig& config, const std::string& name) {
        // Copy config so we can fill in defaults
        MeshConfig cfg = config;

        // Generate HMAC secret if not provided
        if (cfg.hmac_secret.empty()) {
            std::array<uint8_t, 32> secret;
            std::random_device rd;
            for (auto& b : secret) b = static_cast<uint8_t>(rd());
            std::ostringstream oss;
            for (uint8_t b : secret)
                oss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
            cfg.hmac_secret = oss.str();
        }

        std::string mesh_id = cfg.mesh_id.empty() ? generate_mesh_id(cfg) : cfg.mesh_id;
        cfg.mesh_id = mesh_id;  // Store the generated mesh_id in the config
        auto exists = find_mesh(mesh_id, name);
        if (exists) {
            return SMO_ERR_STORAGE(902, Warn, NoRetry, None, "Mesh already exists");
        }
        // Create on-disk structure with MeshID as folder name
        MeshPaths paths = make_mesh_paths(config_.base_data_dir, mesh_id);
        ensure_dirs(paths);
        // Write mesh.json
        std::ofstream mf(paths.mesh_json);
        mf << serialize_config(cfg);
        mf.close();
        // Insert into catalog
        std::string sql = "INSERT INTO meshes (mesh_id, display_name, authority_pubkey, "
                          "root_pubkey, epoch, created_at, config_json) "
                          "VALUES (?, ?, ?, ?, ?, ?, ?)";
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return SMO_ERR_STORAGE(904, Error, RetrySafe, RetryOperation, "Failed to prepare insert");
        }
        sqlite3_bind_text(stmt, 1, mesh_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, cfg.authority_pubkey.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, cfg.root_pubkey.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 5, cfg.epoch);
        sqlite3_bind_int64(stmt, 6, std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        sqlite3_bind_text(stmt, 7, serialize_config(cfg).c_str(), -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) {
            return SMO_ERR_STORAGE(904, Error, RetrySafe, RetryOperation, "Failed to insert mesh");
        }
        return {};
    }

    Result<void> switch_mesh(const std::string& id_or_name) {
        auto found = find_mesh(id_or_name, id_or_name);
        if (!found) {
            return SMO_ERR_STORAGE(404, Info, NoRetry, None, "Mesh not found: " + id_or_name);
        }
        current_mesh_id_ = found.value();
        return {};
    }

    std::vector<std::string> list_meshes() const {
        std::vector<std::string> meshes;
        std::string sql = "SELECT display_name, mesh_id FROM meshes ORDER BY display_name";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return {};
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            std::string id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            meshes.push_back(name + " (" + id.substr(0, 8) + "...)");
        }
        sqlite3_finalize(stmt);
        return meshes;
    }

    std::string get_current_mesh_id() const { return current_mesh_id_; }

    std::string get_current_mesh_name() const {
        if (current_mesh_id_.empty()) return "";
        auto ctx = load_mesh_context(current_mesh_id_);
        return ctx ? ctx->display_name : "";
    }

    Result<std::shared_ptr<MeshContext>> get_mesh(const std::string& mesh_id) {
        auto it = cache_.find(mesh_id);
        if (it != cache_.end()) return it->second;
        auto ctx = load_mesh_context(mesh_id);
        if (!ctx) {
            return SMO_ERR_STORAGE(404, Info, NoRetry, None, "Mesh not found: " + mesh_id);
        }
        cache_[mesh_id] = ctx;
        return ctx;
    }

    std::shared_ptr<MeshContext> load_mesh_context(const std::string& mesh_id) const {
        std::string sql = "SELECT display_name, config_json FROM meshes WHERE mesh_id = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return nullptr;
        sqlite3_bind_text(stmt, 1, mesh_id.c_str(), -1, SQLITE_STATIC);
        std::shared_ptr<MeshContext> ctx;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            ctx = std::make_shared<MeshContext>();
            ctx->config.mesh_id = mesh_id;
            ctx->display_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            ctx->config.display_name = ctx->display_name;  // Keep config in sync
            ctx->paths = make_mesh_paths(config_.base_data_dir, mesh_id);
            // Parse config_json for full MeshConfig fields
            const char* json_cstr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            if (json_cstr) {
                std::string config_json(json_cstr);
                ctx->config.cipher_suite_id = static_cast<CryptoSuiteID>(
                    json_read_int(config_json, "cipher_suite_id", kSuitePurePQC));
                ctx->config.listen_address = json_read_string(config_json, "listen_address");
                if (ctx->config.listen_address.empty())
                    ctx->config.listen_address = "0.0.0.0:7777";
                ctx->config.bootstrap_configured =
                    json_read_string(config_json, "bootstrap_configured") == "true";
                ctx->config.hmac_secret = json_read_string(config_json, "hmac_secret");
                ctx->config.authority_pubkey = json_read_string(config_json, "authority_pubkey");
                ctx->config.root_pubkey = json_read_string(config_json, "root_pubkey");
                ctx->config.epoch = json_read_int(config_json, "epoch", 1);
                // Parse advertise_addresses
                auto adv = config_json.find("\"advertise_addresses\"");
                if (adv != std::string::npos) {
                    auto colon = config_json.find(':', adv);
                    auto arr_start = config_json.find('[', colon);
                    if (arr_start != std::string::npos) {
                        auto arr_end = config_json.find(']', arr_start);
                        if (arr_end != std::string::npos) {
                            std::string arr = config_json.substr(arr_start + 1, arr_end - arr_start - 1);
                            size_t p = 0;
                            while (true) {
                                auto q1 = arr.find('"', p);
                                if (q1 == std::string::npos) break;
                                auto q2 = arr.find('"', q1 + 1);
                                if (q2 == std::string::npos) break;
                                ctx->config.advertise_addresses.push_back(arr.substr(q1 + 1, q2 - q1 - 1));
                                p = q2 + 1;
                            }
                        }
                    }
                }
                // Parse bootstrap_endpoints
                auto boot = config_json.find("\"bootstrap_endpoints\"");
                if (boot != std::string::npos) {
                    auto colon = config_json.find(':', boot);
                    auto arr_start = config_json.find('[', colon);
                    if (arr_start != std::string::npos) {
                        auto arr_end = config_json.find(']', arr_start);
                        if (arr_end != std::string::npos) {
                            std::string arr = config_json.substr(arr_start + 1, arr_end - arr_start - 1);
                            size_t p = 0;
                            while (true) {
                                auto q1 = arr.find('"', p);
                                if (q1 == std::string::npos) break;
                                auto q2 = arr.find('"', q1 + 1);
                                if (q2 == std::string::npos) break;
                                ctx->config.bootstrap_endpoints.push_back(arr.substr(q1 + 1, q2 - q1 - 1));
                                p = q2 + 1;
                            }
                        }
                    }
                }
            }
        }
        sqlite3_finalize(stmt);
        return ctx;
    }

    Result<std::shared_ptr<MeshContext>> get_mesh_by_name(const std::string& display_name) {
        auto found = find_mesh(display_name, display_name);
        if (!found) {
            return SMO_ERR_STORAGE(404, Info, NoRetry, None, "Mesh not found: " + display_name);
        }
        return load_mesh_context(found.value());
    }

private:
    std::optional<std::string> find_mesh(const std::string& id, const std::string& name) const {
        std::string sql = "SELECT mesh_id FROM meshes WHERE mesh_id = ? OR display_name = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_STATIC);
        std::optional<std::string> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        }
        sqlite3_finalize(stmt);
        return result;
    }

    void ensure_default_mesh() {
        std::string sql = "SELECT COUNT(*) FROM meshes WHERE display_name = 'default'";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return;
        bool exists = false;
        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) > 0) exists = true;
        sqlite3_finalize(stmt);
        if (exists) return;
        MeshConfig cfg;
        cfg.display_name = "default";
        cfg.authority_pubkey = "placeholder";
        cfg.root_pubkey = cfg.authority_pubkey;
        cfg.epoch = 1;
        cfg.created_at = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        create_mesh(cfg, "default");
    }
};

MeshManager::MeshManager(const Config& config)
    : impl_(std::make_unique<Impl>(config)) {}

MeshManager::~MeshManager() = default;

Result<void> MeshManager::initialize() { return impl_->open(); }

Result<void> MeshManager::create_mesh(const MeshConfig& config, const std::string& name) {
    return impl_->create_mesh(config, name);
}

Result<void> MeshManager::delete_mesh(const std::string& mesh_id) {
    (void)mesh_id;
    return SMO_ERR_STORAGE(905, Error, NoRetry, None, "Not implemented");
}

Result<void> MeshManager::join_mesh(const std::string& mesh_id, const std::string& seed_address) {
    (void)mesh_id; (void)seed_address;
    return SMO_ERR_STORAGE(905, Error, NoRetry, None, "Not implemented");
}

Result<void> MeshManager::leave_mesh(const std::string& mesh_id) {
    (void)mesh_id;
    return SMO_ERR_STORAGE(905, Error, NoRetry, None, "Not implemented");
}

Result<void> MeshManager::switch_mesh(const std::string& id_or_name) {
    return impl_->switch_mesh(id_or_name);
}

Result<std::shared_ptr<MeshContext>> MeshManager::get_mesh(const std::string& mesh_id) {
    return impl_->get_mesh(mesh_id);
}

Result<std::shared_ptr<MeshContext>> MeshManager::get_mesh_by_name(const std::string& display_name) {
    return impl_->get_mesh_by_name(display_name);
}

Result<std::shared_ptr<MeshContext>> MeshManager::get_current_mesh() const {
    if (impl_->get_current_mesh_id().empty()) {
        return SMO_ERR_STORAGE(404, Info, NoRetry, None, "No current mesh");
    }
    return impl_->get_mesh(impl_->get_current_mesh_id());
}

std::vector<std::string> MeshManager::list_meshes() const {
    return impl_->list_meshes();
}

std::string MeshManager::get_current_mesh_id() const {
    return impl_->get_current_mesh_id();
}

std::string MeshManager::get_current_mesh_name() const {
    return impl_->get_current_mesh_name();
}

Result<MeshManager::MeshHandle> MeshManager::open_mesh(const std::string& mesh_id) {
    MeshHandle handle;
    auto ctx = impl_->get_mesh(mesh_id);
    if (ctx) handle.context = ctx.value();
    return handle;
}

MeshManager::MeshHandle::~MeshHandle() = default;

Result<const CryptoProvider*> MeshManager::cipher_suite(const std::string& mesh_id) const {
    auto ctx = impl_->get_mesh(mesh_id);
    if (!ctx) return SMO_ERR_STORAGE(404, Info, NoRetry, None,
                                     "mesh not found: " + mesh_id);
    // TODO: read cipher_suite_id from ctx->config once config is populated
    // For now default to kSuitePurePQC
    return CryptoRegistry::instance().get_suite(kSuitePurePQC);
}

namespace {

int64_t parse_duration(const std::string& dur) {
    if (dur.empty()) return 3600; // default 1h
    char unit = dur.back();
    int64_t num = 0;
    try { num = std::stoll(dur.substr(0, dur.size() - 1)); } catch (...) { return 3600; }
    switch (unit) {
        case 's': return num;
        case 'm': return num * 60;
        case 'h': return num * 3600;
        case 'd': return num * 86400;
        default:  return num * 3600; // assume hours if no unit
    }
}

} // anonymous namespace

Result<std::string> MeshManager::generate_invite(
    const std::string& mesh_id,
    const std::string& role,
    const std::string& expiry_duration,
    const std::vector<std::string>& bootstrap_endpoints)
{
    auto ctx = impl_->get_mesh(mesh_id);
    if (!ctx) return SMO_ERR_STORAGE(404, Info, NoRetry, None,
                                     "mesh not found: " + mesh_id);

    auto& config = ctx.value()->config;

    // Use provided endpoints, or fall back to mesh config bootstrap_endpoints
    std::vector<std::string> endpoints = bootstrap_endpoints;
    if (endpoints.empty()) {
        endpoints = config.bootstrap_endpoints;
    }

    if (endpoints.empty()) {
        return SMO_ERR_STORAGE(223, Error, NoRetry, None,
                               "bootstrap not configured: run 'smo-admin mesh publish' first");
    }

    auto crypto_result = cipher_suite(mesh_id);
    if (!crypto_result) return crypto_result.error();
    const auto* crypto = crypto_result.value();

    Bytes hmac_secret;
    for (size_t i = 0; i + 1 < config.hmac_secret.size(); i += 2) {
        char buf[3] = {config.hmac_secret[i], config.hmac_secret[i+1], 0};
        hmac_secret.push_back(static_cast<uint8_t>(std::strtoul(buf, nullptr, 16)));
    }

    int64_t duration_sec = parse_duration(expiry_duration);
    auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t expiry = duration_sec > 0 ? now_sec + duration_sec : 0;

    auto token_result = enroll::generate_token(
        config.mesh_id,
        config.epoch,
        static_cast<int>(config.cipher_suite_id),
        endpoints,
        role,
        expiry,
        hmac_secret,
        crypto->hash
    );
    if (!token_result) return token_result.error();

    return enroll::encode_token_wire(token_result.value());
}

// ---------------------------------------------------------------------------
// Publish mesh: configure bootstrap endpoints after mesh creation
// ---------------------------------------------------------------------------
Result<void> MeshManager::publish_mesh(
    const std::string& mesh_id,
    const std::string& listen_address,
    const std::vector<std::string>& advertise_addresses,
    const std::vector<std::string>& bootstrap_endpoints)
{
    auto ctx_result = impl_->get_mesh(mesh_id);
    if (!ctx_result) return ctx_result.error();
    auto ctx = ctx_result.value();

    ctx->config.listen_address = listen_address;
    ctx->config.advertise_addresses = advertise_addresses;
    ctx->config.bootstrap_endpoints = bootstrap_endpoints;
    ctx->config.bootstrap_configured = true;

    // Update mesh.json on disk
    auto paths = make_mesh_paths(impl_->config_.base_data_dir, mesh_id);
    std::ofstream mf(paths.mesh_json);
    if (!mf) {
        return SMO_ERR_STORAGE(905, Error, NoRetry, None,
                               "Failed to write mesh.json at " + paths.mesh_json);
    }
    mf << serialize_config(ctx->config);
    mf.close();

    return {};
}

// ---------------------------------------------------------------------------
// Load full MeshConfig from mesh.json (static, used by smo-admin standalone)
// ---------------------------------------------------------------------------
Result<MeshConfig> MeshManager::load_mesh_config(const std::string& mesh_dir) {
    std::string path = mesh_dir + "/mesh.json";
    std::ifstream f(path);
    if (!f) {
        return SMO_ERR_STORAGE(404, Info, NoRetry, None,
                               "mesh.json not found: " + path);
    }
    std::string json((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    f.close();

    MeshConfig cfg;
    cfg.mesh_id        = json_read_string(json, "mesh_id");
    cfg.display_name   = json_read_string(json, "display_name");
    cfg.authority_pubkey = json_read_string(json, "authority_pubkey");
    cfg.root_pubkey    = json_read_string(json, "root_pubkey");
    cfg.hmac_secret    = json_read_string(json, "hmac_secret");
    cfg.cipher_suite_id = static_cast<CryptoSuiteID>(json_read_int(json, "cipher_suite_id", kSuitePurePQC));
    cfg.epoch          = json_read_int(json, "epoch", 1);
    cfg.created_at     = json_read_int(json, "created_at", 0);
    cfg.listen_address = json_read_string(json, "listen_address");
    if (cfg.listen_address.empty()) cfg.listen_address = "0.0.0.0:7777";
    cfg.bootstrap_configured = json_read_string(json, "bootstrap_configured") == "true";

    // Parse advertise_addresses array
    auto adv_start = json.find("\"advertise_addresses\"");
    if (adv_start != std::string::npos) {
        auto colon = json.find(':', adv_start);
        auto arr_start = json.find('[', colon);
        if (arr_start != std::string::npos) {
            auto arr_end = json.find(']', arr_start);
            if (arr_end != std::string::npos) {
                std::string arr = json.substr(arr_start + 1, arr_end - arr_start - 1);
                size_t pos = 0;
                while (true) {
                    auto q1 = arr.find('"', pos);
                    if (q1 == std::string::npos) break;
                    auto q2 = arr.find('"', q1 + 1);
                    if (q2 == std::string::npos) break;
                    cfg.advertise_addresses.push_back(arr.substr(q1 + 1, q2 - q1 - 1));
                    pos = q2 + 1;
                }
            }
        }
    }

    // Parse bootstrap_endpoints array
    auto boot_start = json.find("\"bootstrap_endpoints\"");
    if (boot_start != std::string::npos) {
        auto colon = json.find(':', boot_start);
        auto arr_start = json.find('[', colon);
        if (arr_start != std::string::npos) {
            auto arr_end = json.find(']', arr_start);
            if (arr_end != std::string::npos) {
                std::string arr = json.substr(arr_start + 1, arr_end - arr_start - 1);
                size_t pos = 0;
                while (true) {
                    auto q1 = arr.find('"', pos);
                    if (q1 == std::string::npos) break;
                    auto q2 = arr.find('"', q1 + 1);
                    if (q2 == std::string::npos) break;
                    cfg.bootstrap_endpoints.push_back(arr.substr(q1 + 1, q2 - q1 - 1));
                    pos = q2 + 1;
                }
            }
        }
    }

    return cfg;
}

} // namespace smo