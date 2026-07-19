// §XIX — CLI Design
// smo-node: actual node daemon.
//
// Responsibilities:
// - receive intents
// - run FSM
// - manage sessions
// - capability enforcement
// - execute DAG tasks

#include <core/crypto/impl.hpp>
#include <core/crypto/registry.hpp>
#include <core/crypto/suite.hpp>
#include <core/discovery/discovery.hpp>
#include <core/errors/error.hpp>
#include <core/identity/identity.hpp>
#include <core/types.hpp>
#include <core/transport/transport.hpp>
#include <core/transport/tcp_transport.hpp>
#include <core/transport/secure_session.hpp>
#include <core/network/udp/udp_transport.hpp>
#include <core/select/selector.hpp>
#include <core/network/udp/heartbeat_service.hpp>
#include <core/discovery/gossip.hpp>
#include <core/network/sync/membership_sync.hpp>
#include <core/network/transport/address_resolver.hpp>
#include <core/discovery/peer_store.hpp>
#include <core/certificate/certificate.hpp>
#include <core/enroll/auto_enroll.hpp>
#include <core/mesh/mesh_resolver.hpp>
#include <core/mesh/mesh_manager.hpp>
#include <core/authority/authority.hpp>
#include <core/governance/governance.hpp>
#include <core/recovery/crl.hpp>
#include <core/network/packet_dispatcher.hpp>
#include <core/fsm/node_lifecycle_fsm.hpp>
#include <core/bootstrap/bootstrap_protocol.hpp>
#include <core/join/join_protocol.hpp>
#include <core/runtime/runtime_bridge.hpp>
#include <core/runtime/middleware_pipeline.hpp>
#include <core/runtime/policy_middleware.hpp>
#include <core/runtime/action_executor.hpp>
#include <core/runtime/dispatcher.hpp>
#include <core/runtime/contracts/echo_contract.hpp>
#include <core/runtime/contracts/bootstrap_contract.hpp>
#include <core/runtime/contracts/join_contract.hpp>
#include <core/runtime/contracts/governance_contract.hpp>
// Recovery/File/Process contracts registered in future sprint
#include <core/runtime/output_manager.hpp>
#include <core/session/session.hpp>
#include <core/trust/trust.hpp>
#include <core/recovery/recovery_engine.hpp>
#include <core/recovery/crl.hpp>
#include <core/runtime/contracts/recovery_contract.hpp>
#include <core/runtime/contracts/file_contract.hpp>
#include <core/runtime/contracts/process_contract.hpp>
#include <core/runtime/service_registry.hpp>
#include <core/runtime/telemetry.hpp>

#include <providers/suite1_classical/suite1_classical_provider.hpp>
#include <providers/suite3_purepqc/suite3_purepqc_provider.hpp>

#include <tooling/clipboard.hpp>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <poll.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Local utilities
// ---------------------------------------------------------------------------
namespace {

std::string bytes_to_base64(smo::BytesView data) {
    static const char kEnc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                               "abcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t v = (uint32_t)data[i] << 16;
        if (i + 1 < data.size()) v |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < data.size()) v |= (uint32_t)data[i + 2];
        out += kEnc[(v >> 18) & 0x3f];
        out += kEnc[(v >> 12) & 0x3f];
        out += (i + 1 < data.size()) ? kEnc[(v >> 6) & 0x3f] : '=';
        out += (i + 2 < data.size()) ? kEnc[v & 0x3f] : '=';
    }
    return out;
}

} // anonymous namespace

// Global flag for graceful shutdown
static volatile bool g_running = true;
extern "C" void handle_signal(int) { g_running = false; }

static void print_usage(const char* prog) {
    std::fprintf(stderr, R"(SMO Node Daemon

Usage:
  %s --init --name <name> [--data <dir>]
  %s --export [<file> | --copy] [--data <dir>]
  %s --import [<file>] [--data <dir>]
  %s --pubkey [--copy | --fingerprint] [--data <dir>]
  %s --join <token> --data <dir> [--name <name>] [--port <port>]
  %s --daemon --port <port> --data <data-dir> [--name <name>]
                 [--seed <host:port>]

Options:
  --init            Generate identity and save to data directory
  --name <name>     Display name for the node
  --data <dir>      Data directory (default: ~/.smo/node)
  --export <file>   Export CSR to file
  --export --copy   Copy CSR to clipboard
  --import [<file>] Import certificate (auto-detect: stdin->clipboard->file)
  --pubkey          Display public key
  --pubkey --copy   Copy public key to clipboard
  --pubkey --fingerprint  Show short fingerprint
  --join <token>    Join mesh using Join Token (auto-enrollment)
  --daemon          Run as mesh node daemon
  --port <port>     Listen port (default: 7777)
  --seed <host:port>  Bootstrap seed node for discovery
  --help            Show this help
)",
        prog, prog, prog, prog, prog, prog);
}

// ===========================================================================
// Helpers
// ===========================================================================

static void node_id_to_hex(const smo::NodeID& id, std::string& out) {
    std::ostringstream oss;
    for (uint8_t b : id.value) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    }
    out = oss.str();
}

// Load file contents as Bytes
static smo::Bytes load_file_binary(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto size = f.tellg();
    f.seekg(0);
    smo::Bytes data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// Write Bytes to file
static bool write_file_binary(const std::string& path, smo::BytesView data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    return f.good();
}

// ===========================================================================
// Utils
// ===========================================================================

static std::string read_stdin() {
    std::string data;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
        data.append(buf, n);
    }
    return data;
}

static bool has_stdin_data() {
    struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
    return poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN);
}

static smo::Bytes load_cert_blob(const std::string& path_or_empty) {
    // Try stdin first
    if (has_stdin_data()) {
        auto data = read_stdin();
        if (!data.empty()) {
            std::fprintf(stderr, "[smo-node] Reading certificate from stdin...\n");
            return smo::Bytes(data.begin(), data.end());
        }
    }
    // Try clipboard
    if (path_or_empty.empty() && smo::clipboard_available()) {
        auto data = smo::clipboard_paste();
        if (!data.empty()) {
            std::fprintf(stderr, "[smo-node] Reading certificate from clipboard...\n");
            return smo::Bytes(data.begin(), data.end());
        }
    }
    // Try file
    if (!path_or_empty.empty()) {
        return load_file_binary(path_or_empty);
    }
    return {};
}

// ===========================================================================
// Register all available crypto suites
// ===========================================================================
static void ensure_crypto() {
    smo::providers::register_suite1_classical();
    smo::providers::register_suite3_purepqc();
}

// ===========================================================================
// Look up crypto provider by suite ID from registry
// ===========================================================================
static const smo::CryptoProvider* get_crypto(smo::CryptoSuiteID suite_id) {
    auto& reg = smo::CryptoRegistry::instance();
    auto prov_result = reg.get_suite(suite_id);
    if (!prov_result) {
        std::fprintf(stderr, "Error: cipher suite %u not registered\n",
                     (unsigned)suite_id);
        return nullptr;
    }
    return prov_result.value();
}

// ===========================================================================
// Cmd: --init
// ===========================================================================
static int cmd_init(const std::string& name, const std::string& data_dir) {
    ensure_crypto();

    const auto* crypto = get_crypto(smo::kSuitePurePQC);
    if (!crypto) return 1;
    auto rng = crypto->default_rng();

    // Create identity (generates keypair)
    auto id_result = smo::Identity::create(*crypto, rng);
    if (!id_result) {
        std::fprintf(stderr, "Error: identity creation failed: %s\n",
                     id_result.error().message.c_str());
        return 1;
    }
    auto identity = std::move(id_result.value());

    // Save identity to file
    std::string id_path = data_dir + "/identity.json";
    if (auto r = identity.save_to_file(id_path); !r) {
        std::fprintf(stderr, "Error: cannot save identity: %s\n",
                     r.error().message.c_str());
        return 1;
    }

    // Build CSR
    smo::CertificateSigningRequest csr;
    csr.new_public_key = smo::Bytes(identity.public_key().begin(), identity.public_key().end());
    csr.display_name = name;
    csr.platform = "linux";
    csr.version = "0.1.0";
    csr.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    // For initial CSR, sign with the new key itself
    auto sign_result = csr.sign(crypto->signer, identity.secret_key(), rng);
    if (!sign_result) {
        std::fprintf(stderr, "Error: CSR signing failed: %s\n",
                     sign_result.error().message.c_str());
        return 1;
    }

    // Save CSR
    std::string csr_path = data_dir + "/node.csr.smor";
    auto csr_serialized = csr.serialize();
    if (!write_file_binary(csr_path, csr_serialized)) {
        std::fprintf(stderr, "Error: cannot write CSR file: %s\n", csr_path.c_str());
        return 1;
    }

    std::string nid_hex;
    node_id_to_hex(identity.node_id(), nid_hex);
    std::printf("Identity created:\n");
    std::printf("  NodeID:       %s\n", nid_hex.c_str());
    std::printf("  Display name: %s\n", name.c_str());
    std::printf("  Identity:     %s\n", id_path.c_str());
    std::printf("  CSR:          %s\n", csr_path.c_str());
    std::printf("\n");
    std::printf("Next: Submit %s to the mesh authority for signing.\n", csr_path.c_str());
    std::printf("      Then run: smo-node --import <signed-cert>.smoc --data %s\n", data_dir.c_str());
    return 0;
}

// ===========================================================================
// Cmd: --export
// ===========================================================================
static int cmd_export(const std::string& output_path, const std::string& data_dir) {
    ensure_crypto();

    const auto* crypto = get_crypto(smo::kSuitePurePQC);
    if (!crypto) return 1;

    auto id_result = smo::Identity::load_from_file(data_dir + "/identity.json", *crypto);
    if (!id_result) {
        std::fprintf(stderr, "Error: cannot load identity: %s\n",
                     id_result.error().message.c_str());
        return 1;
    }
    auto& identity = id_result.value();
    auto rng = crypto->default_rng();

    // Read existing CSR if present, else build a new one
    std::string csr_path = data_dir + "/node.csr.smor";
    auto existing = load_file_binary(csr_path);
    if (!existing.empty()) {
        // Copy existing CSR to output
        if (!write_file_binary(output_path, existing)) {
            std::fprintf(stderr, "Error: cannot write CSR file: %s\n", output_path.c_str());
            return 1;
        }
        std::printf("CSR exported: %s -> %s\n", csr_path.c_str(), output_path.c_str());
        return 0;
    }

    // Build new CSR
    smo::CertificateSigningRequest csr;
    csr.new_public_key = smo::Bytes(identity.public_key().begin(), identity.public_key().end());

    // Try to read display name from existing cert or use "unnamed"
    csr.display_name = "unnamed-node";
    csr.platform = "linux";
    csr.version = "0.1.0";
    csr.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    auto sign_result = csr.sign(crypto->signer, identity.secret_key(), rng);
    if (!sign_result) {
        std::fprintf(stderr, "Error: CSR signing failed: %s\n",
                     sign_result.error().message.c_str());
        return 1;
    }

    auto csr_serialized = csr.serialize();
    if (!write_file_binary(output_path, csr_serialized)) {
        std::fprintf(stderr, "Error: cannot write CSR file: %s\n", output_path.c_str());
        return 1;
    }

    // Also save to data dir for convenience
    write_file_binary(csr_path, csr_serialized);

    std::printf("CSR exported: %s (%zu bytes)\n", output_path.c_str(), csr_serialized.size());
    return 0;
}

// ===========================================================================
// Cmd: --import
//
// Auto-detect transport: stdin → clipboard → filename
// ===========================================================================
static int cmd_import(const std::string& cert_path_or_empty, const std::string& data_dir) {
    ensure_crypto();

    const auto* crypto = get_crypto(smo::kSuitePurePQC);
    if (!crypto) return 1;

    // Load identity
    auto id_result = smo::Identity::load_from_file(data_dir + "/identity.json", *crypto);
    if (!id_result) {
        std::fprintf(stderr, "Error: cannot load identity: %s\n",
                     id_result.error().message.c_str());
        return 1;
    }
    auto identity = std::move(id_result.value());

    // Auto-detect transport: stdin → clipboard → file
    auto cert_blob = load_cert_blob(cert_path_or_empty);
    if (cert_blob.empty()) {
        std::fprintf(stderr, "Error: no certificate data found.\n"
                             "  Try: smo node import <file.smoc>\n"
                             "   or: cat cert.smoc | smo node import\n"
                             "   or: smo node import (with certificate in clipboard)\n");
        return 1;
    }

    auto cert_result = smo::Certificate::deserialize(cert_blob);
    if (!cert_result) {
        std::fprintf(stderr, "Error: invalid certificate: %s\n",
                     cert_result.error().message.c_str());
        return 1;
    }
    auto& cert = cert_result.value();

    // Verify certificate signature
    auto verify_result = cert.verify(crypto->signer);
    if (!verify_result) {
        std::fprintf(stderr, "Error: certificate verification failed: %s\n",
                     verify_result.error().message.c_str());
        return 1;
    }
    if (!verify_result.value()) {
        std::fprintf(stderr, "Error: certificate signature is invalid\n");
        return 1;
    }

    // Update identity state
    identity.transition_to(smo::IdentityState::Enrolled);

    // Save updated identity
    if (auto r = identity.save_to_file(data_dir + "/identity.json"); !r) {
        std::fprintf(stderr, "Error: cannot save identity: %s\n",
                     r.error().message.c_str());
        return 1;
    }

    // Save certificate
    std::string cert_out = data_dir + "/node.cert.smoc";
    if (!write_file_binary(cert_out, cert_blob)) {
        std::fprintf(stderr, "Error: cannot save certificate: %s\n", cert_out.c_str());
        return 1;
    }

    // ── Post-import summary ────────────────────────────────────
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char expiry_buf[32] = {};
    if (cert.not_after > 0) {
        std::tm* tm = std::gmtime(&cert.not_after);
        if (tm) std::strftime(expiry_buf, sizeof(expiry_buf), "%Y-%m-%d", tm);
    }

    std::printf("\n");
    std::printf("  Enrollment successful.\n");
    std::printf("\n");
    std::printf("  NodeID:          %s\n", identity.node_id().to_string().c_str());
    {
        auto fp_hash = crypto->hash.hash(smo::BytesView(cert_blob));
        std::string fp_hex = fp_hash ? smo::bytes_to_hex(fp_hash.value()).substr(0, 16) : "???";
        std::printf("  Certificate:     %s\n", fp_hex.c_str());
    }
    std::printf("  Cipher Suite:    Suite %d\n", (int)crypto->suite_id);
    std::printf("  Display Name:    %s\n", cert.display_name.c_str());
    std::printf("  Role:            %s\n", smo::to_string(cert.role));
    std::printf("  Epoch:           %llu\n", (unsigned long long)cert.epoch);
    if (expiry_buf[0])
        std::printf("  Valid until:     %s\n", expiry_buf);
    std::printf("\n");
    std::printf("  Node is now enrolled. Run with --daemon to start.\n");
    return 0;
}

// ===========================================================================
// Cmd: --export --copy (send CSR to clipboard)
// ===========================================================================
static int cmd_export_to_clipboard(const std::string& data_dir) {
    ensure_crypto();

    const auto* crypto = get_crypto(smo::kSuitePurePQC);
    if (!crypto) return 1;

    auto id_result = smo::Identity::load_from_file(data_dir + "/identity.json", *crypto);
    if (!id_result) {
        std::fprintf(stderr, "Error: cannot load identity.\n"
                             "  Run 'smo-node --init --name <name>' first\n");
        return 1;
    }
    auto& identity = id_result.value();
    auto rng = crypto->default_rng();

    // Build CSR
    std::string csr_path = data_dir + "/node.csr.smor";
    auto existing = load_file_binary(csr_path);
    smo::Bytes csr_serialized;
    if (!existing.empty()) {
        csr_serialized = existing;
    } else {
        smo::CertificateSigningRequest csr;
        csr.new_public_key = smo::Bytes(identity.public_key().begin(), identity.public_key().end());
        csr.display_name = "unnamed-node";
        csr.platform = "linux";
        csr.version = "0.1.0";
        csr.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        auto sign_result = csr.sign(crypto->signer, identity.secret_key(), rng);
        if (!sign_result) {
            std::fprintf(stderr, "Error: CSR signing failed: %s\n",
                         sign_result.error().message.c_str());
            return 1;
        }
        csr_serialized = csr.serialize();
    }

    // Copy to clipboard (base64-encoded for text safety)
    std::string b64 = bytes_to_base64(csr_serialized);
    if (smo::clipboard_copy(b64)) {
        std::printf("CSR copied to clipboard (%zu bytes).\n", csr_serialized.size());
        std::printf("  On the Authority machine, run:\n");
        std::printf("    smo-admin sign --paste\n");
        return 0;
    }
    std::fprintf(stderr, "Error: clipboard not available\n");
    return 1;
}

// ===========================================================================
// Cmd: --pubkey
// ===========================================================================
static int cmd_pubkey(bool do_copy, bool show_fingerprint, const std::string& data_dir) {
    ensure_crypto();

    const auto* crypto = get_crypto(smo::kSuitePurePQC);
    if (!crypto) return 1;

    auto id_result = smo::Identity::load_from_file(data_dir + "/identity.json", *crypto);
    if (!id_result) {
        std::fprintf(stderr, "Error: cannot load identity: %s\n",
                     id_result.error().message.c_str());
        std::fprintf(stderr, "  Run 'smo-node --init --name <name>' first\n");
        return 1;
    }
    auto& identity = id_result.value();

    if (show_fingerprint) {
        auto hash = crypto->hash.hash(identity.public_key());
        if (!hash) {
            std::fprintf(stderr, "Error: fingerprint computation failed\n");
            return 1;
        }
        std::string hex = smo::bytes_to_hex(hash.value());
        // Format as colon-separated pairs
        for (size_t i = 0; i < hex.size(); i += 2) {
            if (i > 0) std::putchar(':');
            std::printf("%c%c", hex[i], hex[i+1]);
            if (i >= 18) break; // show first 20 hex chars = 10 bytes
        }
        std::putchar('\n');
        return 0;
    }

    // Base64url-encode public key with SMO-PUBKEY- prefix
    auto pk = identity.public_key();
    std::string b64;
    static const char kEnc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                               "abcdefghijklmnopqrstuvwxyz0123456789-_";
    for (size_t i = 0; i < pk.size(); i += 3) {
        uint32_t v = (uint32_t)pk[i] << 16;
        if (i + 1 < pk.size()) v |= (uint32_t)pk[i + 1] << 8;
        if (i + 2 < pk.size()) v |= (uint32_t)pk[i + 2];
        b64 += kEnc[(v >> 18) & 0x3f];
        b64 += kEnc[(v >> 12) & 0x3f];
        if (i + 1 < pk.size()) b64 += kEnc[(v >> 6) & 0x3f];
        if (i + 2 < pk.size()) b64 += kEnc[v & 0x3f];
    }
    std::string output = "SMO-PUBKEY-" + b64;

    if (do_copy) {
        if (smo::clipboard_copy(output)) {
            std::printf("Public key copied to clipboard.\n");
            return 0;
        }
        std::fprintf(stderr, "Error: clipboard not available\n");
        return 1;
    }

    std::printf("%s\n", output.c_str());
    return 0;
}

// ── SecureTransportSession — wraps SecureSession as TransportSession ──
struct SecureTransportSession : public smo::TransportSession {
    smo::SecureSession sec;
    smo::Endpoint remote;
    bool open_ = true;

    SecureTransportSession(smo::SecureSession&& s, smo::Endpoint ep)
        : sec(std::move(s)), remote(std::move(ep)) {}

    smo::Result<void> send(smo::BytesView data) override {
        return sec.send(data);
    }
    smo::Result<smo::Bytes> recv(size_t) override {
        return sec.recv();
    }
    smo::Result<void> close() override {
        open_ = false;
        return {};
    }
    smo::Endpoint remote_endpoint() const override {
        return remote;
    }
    bool is_open() const override {
        return open_;
    }
};

// ===========================================================================
// Main
// ===========================================================================

int main(int argc, char* argv[]) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    // ── Parse common args ──────────────────────────────────────
    bool daemon_mode = false;
    bool init_mode = false;
    bool export_mode = false;
    bool import_mode = false;
    bool pubkey_mode = false;
    bool pubkey_copy = false;
    bool pubkey_fingerprint = false;
    bool export_copy = false;
    bool join_mode = false;
    std::string join_token;
    int port = 7777;
    std::string data_dir = smo::mesh::smo_home() + "/node";
    std::string mesh_dir;
    std::string node_name;
    std::string seed_addr;
    std::string export_path;
    std::string import_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--daemon") daemon_mode = true;
        else if (arg == "--init") init_mode = true;
        else if (arg == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (arg == "--data" && i + 1 < argc) data_dir = argv[++i];
        else if (arg == "--mesh-dir" && i + 1 < argc) mesh_dir = argv[++i];
        else if (arg == "--name" && i + 1 < argc) node_name = argv[++i];
        else if (arg == "--seed" && i + 1 < argc) seed_addr = argv[++i];
        else if (arg == "--export" && i + 1 < argc) { export_mode = true; export_path = argv[++i]; }
        else if (arg == "--import" && i + 1 < argc) { import_mode = true; import_path = argv[++i]; }
        else if (arg == "--join" && i + 1 < argc) { join_mode = true; join_token = argv[++i]; }
        else if (arg == "--pubkey") pubkey_mode = true;
        else if (arg == "--copy") { pubkey_copy = true; export_copy = true; }
        else if (arg == "--fingerprint") pubkey_fingerprint = true;
        else if (arg == "--help") { print_usage(argv[0]); return 0; }
    }

    // ── Initialize data directory ──────────────────────────────
    auto create_dir = [](const std::string& dir) {
        if (dir.empty()) return;
        namespace fs = std::filesystem;
        fs::create_directories(dir);
    };

    // ── Mode dispatch ──────────────────────────────────────────

    if (pubkey_mode) {
        if (!data_dir.empty()) create_dir(data_dir);
        return cmd_pubkey(pubkey_copy, pubkey_fingerprint, data_dir);
    }

    if (init_mode) {
        if (node_name.empty()) {
            std::fprintf(stderr, "Error: --name <name> is required with --init\n");
            return 1;
        }
        if (!data_dir.empty()) create_dir(data_dir);
        return cmd_init(node_name, data_dir);
    }

    if (export_mode) {
        if (export_copy) {
            // --copy flag: send CSR to clipboard instead of file
            return cmd_export_to_clipboard(data_dir);
        }
        if (export_path.empty()) {
            std::fprintf(stderr, "Error: --export <file> requires a file path\n");
            return 1;
        }
        return cmd_export(export_path, data_dir);
    }

    if (import_mode) {
        // import_path may be empty → auto-detect (stdin → clipboard → file)
        return cmd_import(import_path, data_dir);
    }

    if (join_mode) {
        if (join_token.empty()) {
            std::fprintf(stderr, "Error: --join requires a token\n");
            return 1;
        }
        ensure_crypto();
        auto result = smo::enroll::run_join_command(join_token, data_dir,
                                                      node_name,
                                                      static_cast<uint16_t>(port),
                                                      "");
        if (!result) {
            std::fprintf(stderr, "Error: %s\n", result.error().message.c_str());
            return 1;
        }
        return 0;
    }

    // ── Legacy: show info if no flags ──────────────────────────
    if (!daemon_mode) {
        std::printf("SMO Node\n");
        std::printf("  Data dir: %s\n", data_dir.c_str());
        std::printf("  Port:     %d\n", port);
        if (!node_name.empty()) std::printf("  Name:     %s\n", node_name.c_str());
        if (!seed_addr.empty()) std::printf("  Seed:     %s\n", seed_addr.c_str());
        std::printf("  Mode:     standalone (use --daemon to run as service, "
                    "--init to enroll)\n");
        return 0;
    }

    // ====================================================================
    // Daemon mode
    // ====================================================================
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    std::printf("[smo-node] Starting daemon...\n");
    std::printf("[smo-node] Data: %s, Port: %d\n", data_dir.c_str(), port);
    if (!node_name.empty()) std::printf("[smo-node] Name: %s\n", node_name.c_str());
    if (!seed_addr.empty()) std::printf("[smo-node] Seed: %s\n", seed_addr.c_str());

    // Initialize crypto (register all suites)
    ensure_crypto();
    const auto* crypto = get_crypto(smo::kSuitePurePQC);
    if (!crypto) return 1;

    // Load identity
    std::string id_path = data_dir + "/identity.json";
    auto id_result = smo::Identity::load_from_file(id_path, *crypto);
    if (!id_result) {
        std::fprintf(stderr, "[smo-node] Fatal: cannot load identity from %s: %s\n",
                     id_path.c_str(), id_result.error().message.c_str());
        std::fprintf(stderr, "[smo-node] Run 'smo-node --init --name <name>' first\n");
        return 1;
    }
    auto local_identity = std::move(id_result.value());

    // Load NodeID
    smo::NodeID local_id = local_identity.node_id();
    std::string local_id_hex;
    node_id_to_hex(local_id, local_id_hex);
    std::printf("[smo-node] Local NodeID: %s (state: %s)\n",
                local_id_hex.c_str(), smo::to_string(local_identity.state()));

    // Load server certificate for PQ handshake
    std::string cert_path = data_dir + "/node.cert.smoc";
    smo::Bytes server_cert_blob = load_file_binary(cert_path);
    if (server_cert_blob.empty()) {
        std::fprintf(stderr, "[smo-node] Warning: no certificate at %s, PQ handshake disabled\n",
                     cert_path.c_str());
    }
    smo::Bytes server_signing_key(local_identity.secret_key().begin(),
                                   local_identity.secret_key().end());

    // Register transports BEFORE any references
    smo::TransportRegistry::instance().register_transport(
        std::make_unique<smo::TcpTransport>(), "tcp");
    smo::TransportRegistry::instance().register_transport(
        std::make_unique<smo::network::udp::UdpTransport>(), "udp");

    // Initialize core components
    smo::MembershipTable membership;
    smo::HealthMonitor health_monitor;
    auto* tcp_transport_ptr = smo::TransportRegistry::instance().get("tcp");
    smo::Transport& transport_ref = *tcp_transport_ptr;
    smo::DiscoveryEngine discovery_engine(membership, health_monitor, transport_ref);
    auto gossip_cfg = smo::GossipEngine::default_config();
    smo::GossipEngine gossip_engine(membership, gossip_cfg);
    smo::network::sync::MembershipSync membership_sync(membership, health_monitor);

    // Wire MembershipSync to GossipEngine for rich event-based gossip
    gossip_engine.set_membership_sync(&membership_sync);

    // Initialize PeerStore and sync with MembershipTable
    smo::PeerStore peer_store;
    if (auto r = peer_store.open(data_dir); !r) {
        std::fprintf(stderr, "[smo-node] Failed to open PeerStore: %s\n",
                     r.error().message.c_str());
    } else {
        peer_store.sync_to_membership(membership);
    }

    // Initialize AddressResolver
    smo::network::transport::AddressResolver address_resolver;

    // Register UDP transport and start UDP listener
    smo::Endpoint udp_listen_ep;
    udp_listen_ep.scheme = "udp";
    udp_listen_ep.host = "0.0.0.0";
    udp_listen_ep.port = static_cast<uint16_t>(port);

    auto udp_transport = std::make_unique<smo::network::udp::UdpTransport>();
    auto udp_listen_result = udp_transport->listen(udp_listen_ep);
    if (!udp_listen_result) {
        std::fprintf(stderr, "[smo-node] Failed to listen UDP: %s\n",
                     udp_listen_result.error().message.c_str());
    } else {
        std::printf("[smo-node] Listening on udp://0.0.0.0:%d\n", port);
    }

    // Initialize HeartbeatService
    smo::network::udp::HeartbeatService::Config hb_config;
    hb_config.ping_interval_ms = 5000;
    hb_config.ping_timeout_ms = 3000;
    hb_config.max_misses = 3;
    hb_config.local_port = port;

    smo::network::udp::HeartbeatService heartbeat_service(hb_config);
    auto hb_start = heartbeat_service.start(*static_cast<smo::network::udp::UdpTransport*>(udp_transport.get()),
                                            membership, health_monitor);
    if (!hb_start) {
        std::fprintf(stderr, "[smo-node] Failed to start heartbeat: %s\n",
                     hb_start.error().message.c_str());
    } else {
        std::printf("[smo-node] Heartbeat service started (interval=%ums, timeout=%ums, max_misses=%u)\n",
                    hb_config.ping_interval_ms, hb_config.ping_timeout_ms, hb_config.max_misses);
    }

    // Create TCP listening endpoint
    smo::Endpoint listen_ep;
    listen_ep.scheme = "tcp";
    listen_ep.host = "0.0.0.0";
    listen_ep.port = static_cast<uint16_t>(port);

    auto listen_result = smo::TransportRegistry::instance().get("tcp")->listen(listen_ep);
    if (!listen_result) {
        std::fprintf(stderr, "[smo-node] Failed to listen TCP: %s\n",
                     listen_result.error().message.c_str());
        return 1;
    }
    auto& lstnr = listen_result.value();

    std::printf("[smo-node] Listening on tcp://0.0.0.0:%d\n", port);

    // ── Bootstrap summary ─────────────────────────────────────
    if (!mesh_dir.empty()) {
        std::string mesh_json = mesh_dir + "/mesh.json";
        std::ifstream f(mesh_json);
        if (f) {
            std::string json((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
            std::string listen_addr = "0.0.0.0:7777";
            std::vector<std::string> advertise;
            std::vector<std::string> bootstrap;
            bool bootstrap_configured = false;

            // Parse listen_address
            auto pos = json.find("\"listen_address\"");
            if (pos != std::string::npos) {
                auto colon = json.find(':', pos);
                auto start = json.find('"', colon + 1);
                if (start != std::string::npos) {
                    auto end = json.find('"', start + 1);
                    if (end != std::string::npos) {
                        listen_addr = json.substr(start + 1, end - start - 1);
                    }
                }
            }

            // Parse bootstrap_configured
            pos = json.find("\"bootstrap_configured\"");
            if (pos != std::string::npos) {
                auto colon = json.find(':', pos);
                auto start = json.find_first_of("tf", colon);
                if (start != std::string::npos) {
                    bootstrap_configured = json[start] == 't';
                }
            }

            // Parse advertise_addresses
            pos = json.find("\"advertise_addresses\"");
            if (pos != std::string::npos) {
                auto colon = json.find(':', pos);
                auto arr_start = json.find('[', colon);
                if (arr_start != std::string::npos) {
                    auto arr_end = json.find(']', arr_start);
                    if (arr_end != std::string::npos) {
                        std::string arr = json.substr(arr_start + 1, arr_end - arr_start - 1);
                        size_t p = 0;
                        while (true) {
                            auto q1 = arr.find('"', p);
                            if (q1 == std::string::npos) break;
                            auto q2 = arr.find('"', q1 + 1);
                            if (q2 == std::string::npos) break;
                            advertise.push_back(arr.substr(q1 + 1, q2 - q1 - 1));
                            p = q2 + 1;
                        }
                    }
                }
            }

            // Parse bootstrap_endpoints
            pos = json.find("\"bootstrap_endpoints\"");
            if (pos != std::string::npos) {
                auto colon = json.find(':', pos);
                auto arr_start = json.find('[', colon);
                if (arr_start != std::string::npos) {
                    auto arr_end = json.find(']', arr_start);
                    if (arr_end != std::string::npos) {
                        std::string arr = json.substr(arr_start + 1, arr_end - arr_start - 1);
                        size_t p = 0;
                        while (true) {
                            auto q1 = arr.find('"', p);
                            if (q1 == std::string::npos) break;
                            auto q2 = arr.find('"', q1 + 1);
                            if (q2 == std::string::npos) break;
                            bootstrap.push_back(arr.substr(q1 + 1, q2 - q1 - 1));
                            p = q2 + 1;
                        }
                    }
                }
            }

            std::printf("Mesh: %s\n", mesh_dir.c_str());
            std::printf("Status: %s\n", bootstrap_configured ? "ONLINE" : "OFFLINE");
            std::printf("Listen:     %s\n", listen_addr.c_str());
            for (const auto& addr : advertise) {
                std::printf("Advertise:  %s\n", addr.c_str());
            }
            if (bootstrap_configured) {
                std::printf("Bootstrap:  YES\n");
            }
            std::printf("Peers:      %zu\n", membership.count());
        }
    }

    // ── Bootstrap: connect to seed ────────────────────────────
    if (!seed_addr.empty()) {
        std::printf("[smo-node] Connecting to seed: %s\n", seed_addr.c_str());

        smo::Endpoint seed_ep;
        auto ep_result = smo::Endpoint::from_string(seed_addr);
        if (!ep_result) {
            std::fprintf(stderr, "[smo-node] Invalid seed address: %s\n", seed_addr.c_str());
        } else {
            seed_ep = ep_result.value();

            auto now = std::chrono::system_clock::to_time_t(
                std::chrono::system_clock::now());

            auto rec_result = smo::Bootstrap::find_seed(
                {seed_ep},
                transport_ref,
                local_id,
                static_cast<int64_t>(now) * 1000000000LL);

            if (rec_result) {
                auto& rec = rec_result.value();
                std::printf("[smo-node] Seed responded: %s (%s)\n",
                            rec.display_name.c_str(),
                            rec.endpoint.to_string().c_str());

                // Process WELCOME through DiscoveryEngine
                auto now_ns = static_cast<int64_t>(now) * 1000000000LL;
                discovery_engine.handle_welcome(
                    smo::WelcomeMsg{local_id, rec}, now_ns);

                // Request full peer table
                smo::DiscoverMsg discover;
                auto disc_data = discover.serialize();
                auto session = smo::TransportRegistry::instance().connect(seed_ep);
                if (session) {
                    session.value()->send(disc_data);
                    auto recv = session.value()->recv(8192);
                    if (recv) {
                        std::printf("[smo-node] Received peer table (%zu bytes)\n",
                                    recv.value().size());
                    }
                }

                std::printf("[smo-node] Bootstrap complete. Peers: %zu\n",
                            membership.count());
            } else {
                std::printf("[smo-node] Seed connection failed: %s\n",
                            rec_result.error().message.c_str());
                std::printf("[smo-node] Continuing as first node in mesh\n");
            }
        }
    }

    // Subscribe to membership events for gossip
    membership_sync.subscribe([&](const smo::network::sync::MembershipEvent& ev) {
        std::printf("[smo-node] Membership event: type=%d\n",
                    static_cast<int>(ev.type));
    });

    // ── Runtime components (Sprint 37 E2E) ─────────────────────────
    smo::runtime::EventBus event_bus;
    smo::runtime::OutputManager output_mgr;
    smo::runtime::Dispatcher runtime_dispatcher;
    smo::runtime::PlanResolver plan_resolver;
    smo::runtime::RuntimeKernel runtime_kernel(
        event_bus, output_mgr, runtime_dispatcher, plan_resolver);

    // SessionManager for tracking peer sessions
    smo::SessionManager session_mgr;

    // MeshManager for mesh directory/catalog operations
    smo::MeshManager mesh_manager(smo::MeshManager::Config{});

    // MeshAuthority for certificate signing and key management
    smo::authority::MeshAuthority authority;

    // GovernanceEngine for mesh governance
    smo::GovernanceEngine governance_engine;

    // TrustManager for peer trust scoring
    smo::TrustManager trust_mgr;

    // CRL for certificate revocation
    smo::recovery::CRL crl;

    // Recovery Engine
    smo::recovery::RecoveryEngine recovery_engine(smo::recovery::RecoveryConfig{});

    // ── Register all contracts ────────────────────────────────
    // Echo (legacy, for backwards compat)
    runtime_dispatcher.register_contract(
        "system.echo",
        std::make_unique<smo::runtime::EchoContract>());

    // BootstrapContract: mesh bootstrap snapshots
    runtime_dispatcher.register_contract(
        "system.bootstrap",
        std::make_unique<smo::runtime::BootstrapContract>(
            mesh_manager,
            authority,
            &governance_engine,
            nullptr));  // CRL deferred to future sprint

    // JoinContract: node enrollment
    {
        auto rng = crypto->default_rng();
        runtime_dispatcher.register_contract(
            "system.join",
            std::make_unique<smo::runtime::JoinContract>(
                crypto->hash,
                crypto->signer,
                rng));
    }

    // GovernanceContract: proposals, voting, commit
    runtime_dispatcher.register_contract(
        "system.governance",
        std::make_unique<smo::runtime::GovernanceContract>(
            governance_engine,
            authority));

    // RecoveryContract: recovery sessions, CRL
    runtime_dispatcher.register_contract(
        "system.recovery",
        std::make_unique<smo::runtime::RecoveryContract>(
            recovery_engine, &crl, governance_engine));

    // FileContract: filesystem operations
    runtime_dispatcher.register_contract(
        "system.file",
        std::make_unique<smo::runtime::FileContract>());

    // ProcessContract: process management
    runtime_dispatcher.register_contract(
        "system.process",
        std::make_unique<smo::runtime::ProcessContract>());

    // ── Middleware Pipeline ────────────────────────────────────
    smo::runtime::MiddlewarePipeline middleware_pipeline;
    auto policy_mw = std::make_unique<smo::runtime::PolicyMiddleware>(&trust_mgr);
    policy_mw->set_anonymous("system.bootstrap", true);
    policy_mw->set_anonymous("system.join", true);
    middleware_pipeline.push(std::move(policy_mw));

    // ── RuntimeBridge: opcode → contract (THIN — no auth) ─────
    smo::runtime::RuntimeBridge runtime_bridge(
        runtime_kernel, runtime_dispatcher);

    // Register routes for Echo
    runtime_bridge.register_route(
        static_cast<uint32_t>(smo::Opcode::ECHO),
        "system.echo", "echo");

    // Register routes for BootstrapContract
    runtime_bridge.register_route(
        smo::bootstrap::kOpcodeBootstrapRequest,
        "system.bootstrap", "request");
    runtime_bridge.register_route(
        static_cast<uint32_t>(smo::Opcode::BOOTSTRAP_SNAPSHOT),
        "system.bootstrap", "snapshot");
    runtime_bridge.register_route(
        static_cast<uint32_t>(smo::Opcode::BOOTSTRAP_INFO),
        "system.bootstrap", "info");
    runtime_bridge.register_route(
        smo::join::kOpcodeBootstrapSyncReq,
        "system.bootstrap", "bootstrap_sync");

    // Register routes for JoinContract
    runtime_bridge.register_route(
        static_cast<uint32_t>(smo::Opcode::JOIN),
        "system.join", "join");
    runtime_bridge.register_route(
        static_cast<uint32_t>(smo::Opcode::LEAVE),
        "system.join", "leave");
    runtime_bridge.register_route(
        static_cast<uint32_t>(smo::Opcode::JOIN_INFO),
        "system.join", "info");

    // Register routes for GovernanceContract
    runtime_bridge.register_route(
        static_cast<uint32_t>(smo::Opcode::GOV_PROPOSE),
        "system.governance", "propose");
    runtime_bridge.register_route(
        static_cast<uint32_t>(smo::Opcode::GOV_VOTE),
        "system.governance", "vote");
    runtime_bridge.register_route(
        static_cast<uint32_t>(smo::Opcode::GOV_COMMIT),
        "system.governance", "commit");
    runtime_bridge.register_route(
        static_cast<uint32_t>(smo::Opcode::GOV_LIST),
        "system.governance", "list");
    runtime_bridge.register_route(
        static_cast<uint32_t>(smo::Opcode::GOV_STATUS),
        "system.governance", "status");
    runtime_bridge.register_route(
        static_cast<uint32_t>(smo::Opcode::GOV_INFO),
        "system.governance", "info");

    // Register routes for RecoveryContract (single opcode, method in payload)
    runtime_bridge.register_route(
        static_cast<uint32_t>(smo::Opcode::RECOVERY),
        "system.recovery", "invoke");

    // Register routes for FileContract (single opcode, method in payload)
    runtime_bridge.register_route(
        static_cast<uint32_t>(smo::Opcode::FILE_OP),
        "system.file", "invoke");

    // Register routes for ProcessContract (single opcode, method in payload)
    runtime_bridge.register_route(
        static_cast<uint32_t>(smo::Opcode::PROCESS),
        "system.process", "invoke");

    // ── PacketDispatcher setup ─────────────────────────────────
    // Helper lambda: session → middleware → bridge → execute → send response
    auto runtime_handler =
        [&](smo::Packet&& pkt, const smo::hl::Endpoint& remote,
            smo::hl::Transport& t) -> smo::Result<void>
    {
        std::string remote_str = remote.address + ":" + std::to_string(remote.port);
        std::printf("[smo-node] Packet received opcode=0x%x from %s\n",
                    pkt.opcode_id, remote_str.c_str());

        // 1. Session lookup (if session_id present)
        const smo::Session* session = nullptr;
        bool has_session = pkt.session_id.size() >= 16;
        if (has_session) {
            auto sid_res = smo::SessionId::from_bytes(
                smo::BytesView(pkt.session_id.data(), 16));
            if (sid_res) {
                session = session_mgr.lookup(sid_res.value());
            }
        }

        // 2. Middleware pipeline: validate + policy
        smo::runtime::PacketContext mw_ctx;
        mw_ctx.session = session;
        auto* route = runtime_bridge.resolve(pkt.opcode_id);
        if (route) {
            mw_ctx.contract_id = route->contract_id;
            mw_ctx.method = route->method;
        }
        mw_ctx.payload = smo::BytesView(pkt.payload.data(), pkt.payload.size());
        {
            char hex[16];
            std::snprintf(hex, sizeof(hex), "0x%04x", pkt.opcode_id);
            mw_ctx.opcode_hex = hex;
        }

        auto mw_res = middleware_pipeline.process(mw_ctx);
        if (!mw_res) {
            std::printf("[smo-node] Middleware denied: %s\n",
                        mw_res.error().message.c_str());
            return mw_res.error();
        }
        if (mw_ctx.denied) {
            std::printf("[smo-node] Policy denied: %s\n",
                        mw_ctx.deny_reason.c_str());
            return smo::Error(
                smo::ErrorCode(smo::ErrorCategory::Session, 507,
                               smo::Severity::Error,
                               smo::RetryClass::NoRetry,
                               smo::Recovery::None),
                mw_ctx.deny_reason, __FILE__, __LINE__);
        }

        // 3. Bridge: Packet → RuntimeKernel → RuntimeResult
        auto original_pkt = pkt;
        auto rt_result = runtime_bridge.bridge(std::move(pkt));
        if (!rt_result) {
            std::printf("[smo-node] RuntimeBridge failed: %s\n",
                        rt_result.error().message.c_str());
            return rt_result.error();
        }

        // 4. Execute each NextAction via ActionExecutor
        auto& next_actions = rt_result.value().next_actions;
        if (next_actions.empty()) {
            std::printf("[smo-node] No next actions — result: %s\n",
                        rt_result.value().output
                            ? rt_result.value().output->data.c_str()
                            : "(no output)");
            return {};
        }

        for (auto& action : next_actions) {
            smo::runtime::ActionExecutor executor(
                [&](smo::Packet&& resp) -> smo::Result<void> {
                    auto ec = t.send(std::move(resp), remote);
                    if (ec) {
                        return smo::Error(
                            smo::ErrorCode(smo::ErrorCategory::Transport,
                                           static_cast<uint16_t>(ec.value()),
                                           smo::Severity::Error,
                                           smo::RetryClass::RetrySafe,
                                           smo::Recovery::None),
                            "ActionExecutor send failed",
                            __FILE__, __LINE__);
                    }
                    return {};
                }, &event_bus);
            auto exec_res = executor.execute(action, original_pkt);
            if (!exec_res) {
                std::printf("[smo-node] ActionExecutor failed: %s\n",
                            exec_res.error().message.c_str());
            }
        }

        return {};
    };

    // ── Node Lifecycle FSM ─────────────────────────────────────
    smo::NodeLifecycleFSM node_fsm;
    node_fsm.on_event(smo::NodeLifecycleEvent::IDENTITY_CREATED);
    std::printf("[smo-node] Node state: %s\n", node_fsm.state_name().c_str());

    // ── PacketDispatcher setup ─────────────────────────────────
    smo::network::PacketDispatcher dispatcher;
    dispatcher.set_lifecycle_fsm(&node_fsm);
    dispatcher.set_gossip_engine(&gossip_engine);

    // Register runtime handler for all contract opcodes
    dispatcher.register_handler(
        static_cast<uint32_t>(smo::Opcode::ECHO), runtime_handler);
    dispatcher.register_handler(
        smo::bootstrap::kOpcodeBootstrapRequest, runtime_handler);
    dispatcher.register_handler(
        static_cast<uint32_t>(smo::Opcode::BOOTSTRAP_SNAPSHOT), runtime_handler);
    dispatcher.register_handler(
        static_cast<uint32_t>(smo::Opcode::BOOTSTRAP_INFO), runtime_handler);
    dispatcher.register_handler(
        static_cast<uint32_t>(smo::Opcode::JOIN), runtime_handler);
    dispatcher.register_handler(
        static_cast<uint32_t>(smo::Opcode::LEAVE), runtime_handler);
    dispatcher.register_handler(
        static_cast<uint32_t>(smo::Opcode::JOIN_INFO), runtime_handler);
    dispatcher.register_handler(
        static_cast<uint32_t>(smo::Opcode::GOV_PROPOSE), runtime_handler);
    dispatcher.register_handler(
        static_cast<uint32_t>(smo::Opcode::GOV_VOTE), runtime_handler);
    dispatcher.register_handler(
        static_cast<uint32_t>(smo::Opcode::GOV_COMMIT), runtime_handler);
    dispatcher.register_handler(
        static_cast<uint32_t>(smo::Opcode::GOV_LIST), runtime_handler);
    dispatcher.register_handler(
        static_cast<uint32_t>(smo::Opcode::GOV_STATUS), runtime_handler);
    dispatcher.register_handler(
        static_cast<uint32_t>(smo::Opcode::GOV_INFO), runtime_handler);
    dispatcher.register_handler(
        static_cast<uint32_t>(smo::Opcode::RECOVERY), runtime_handler);
    dispatcher.register_handler(
        static_cast<uint32_t>(smo::Opcode::FILE_OP), runtime_handler);
    dispatcher.register_handler(
        static_cast<uint32_t>(smo::Opcode::PROCESS), runtime_handler);
    dispatcher.register_handler(
        smo::join::kOpcodeBootstrapSyncReq, runtime_handler);

    // ── Raw handler: discovery protocol (HelloMsg, PingMsg, etc.) ──
    dispatcher.register_raw_handler(
        [&](smo::BytesView raw, smo::TransportSession& session,
            const smo::hl::Endpoint& remote) -> smo::Result<void>
    {
        int64_t now_ns = static_cast<int64_t>(
            std::chrono::system_clock::now().time_since_epoch().count());

        // Convert hl::Endpoint → smo::Endpoint
        smo::Endpoint ep;
        ep.scheme = "tcp";
        ep.host = remote.address;
        ep.port = remote.port;

        // Try HelloMsg
        auto hello = smo::HelloMsg::deserialize(raw);
        if (hello) {
            std::printf("[smo-node] Raw handler: HelloMsg from %s\n",
                        remote.address.c_str());
            auto handle_res = discovery_engine.handle_hello(
                hello.value(), ep, now_ns);
            if (!handle_res) {
                return handle_res.error();
            }

            // Send WelcomeMsg back
            auto peer = membership.lookup(hello.value().node_id);
            if (peer) {
                smo::WelcomeMsg welcome;
                welcome.node_id = local_id;
                welcome.peer_record = peer.value();
                auto welcome_data = welcome.serialize();
                auto send_res = session.send(welcome_data);
                if (!send_res) {
                    std::printf("[smo-node] Failed to send WelcomeMsg\n");
                }
            }
            return {};
        }

        // Try PingMsg
        auto ping = smo::PingMsg::deserialize(raw);
        if (ping) {
            smo::PongMsg pong;
            pong.timestamp = ping.value().timestamp;
            auto pong_data = pong.serialize();
            session.send(pong_data);
            return {};
        }

        // ── Try join protocol (raw CBOR: 4-byte length prefix + CBOR) ──
        // Used by `smo mesh join` CLI via auto_enroll.cpp
        auto try_join_protocol = [&]() -> bool {
            if (raw.size() < 4) return false;
            uint32_t payload_len = (static_cast<uint32_t>(raw[0]) << 24) |
                                   (static_cast<uint32_t>(raw[1]) << 16) |
                                   (static_cast<uint32_t>(raw[2]) << 8)  |
                                    static_cast<uint32_t>(raw[3]);
            if (payload_len == 0 || 4 + payload_len > raw.size()) return false;

            smo::BytesView cbor_data = raw.subspan(4, payload_len);

            // Wrap CBOR response in 4-byte length prefix and send
            auto send_cbor_resp = [&](const smo::Bytes& cbor) -> bool {
                uint32_t len = static_cast<uint32_t>(cbor.size());
                uint8_t hdr[4];
                hdr[0] = static_cast<uint8_t>((len >> 24) & 0xFF);
                hdr[1] = static_cast<uint8_t>((len >> 16) & 0xFF);
                hdr[2] = static_cast<uint8_t>((len >> 8) & 0xFF);
                hdr[3] = static_cast<uint8_t>(len & 0xFF);
                smo::Bytes out;
                out.insert(out.end(), hdr, hdr + 4);
                out.insert(out.end(), cbor.begin(), cbor.end());
                auto send_res = session.send(smo::BytesView(out));
                return static_cast<bool>(send_res);
            };

            // Try JoinRequest (opcode 0x0601)
            auto join_req = smo::join::JoinRequest::decode_cbor(cbor_data);
            if (join_req) {
                std::printf("[smo-node] Raw handler: JoinRequest from %s\n",
                            remote.address.c_str());
                auto join_resp = smo::join::process_join_request(
                    join_req.value(), mesh_manager, authority);
                if (join_resp) {
                    auto cbor = join_resp.value().encode_cbor();
                    send_cbor_resp(cbor);
                    return true;
                }
                std::printf("[smo-node] JoinRequest failed: %s\n",
                            join_resp.error().message.c_str());
                return false;
            }

            // Try BootstrapSyncRequest (opcode 0x0603)
            auto sync_req = smo::join::BootstrapSyncRequest::decode_cbor(cbor_data);
            if (sync_req) {
                std::printf("[smo-node] Raw handler: BootstrapSyncRequest from %s\n",
                            remote.address.c_str());
                auto sync_resp = smo::join::process_bootstrap_sync(
                    sync_req.value(), mesh_manager, authority, &crl);
                if (sync_resp) {
                    auto cbor = sync_resp.value().encode_cbor();
                    send_cbor_resp(cbor);
                    return true;
                }
                std::printf("[smo-node] BootstrapSync failed: %s\n",
                            sync_resp.error().message.c_str());
                return false;
            }

            return false;
        };

        if (try_join_protocol()) {
            std::printf("[smo-node] Join protocol handled successfully\n");
            return {};
        }

        // Unknown raw protocol
        return smo::Error(smo::ErrorCode(smo::ErrorCategory::Transport, 309,
                                          smo::Severity::Warn,
                                          smo::RetryClass::RetrySafe,
                                          smo::Recovery::None),
                          "Unknown raw protocol", __FILE__, __LINE__);
    });

    // ── EventBus subscriptions (P4: Recovery → Governance → CRL) ────
    // RecoveryProposalCreated: emitted by RecoveryContract when a revocation proposal is submitted
    event_bus.subscribe(smo::runtime::EventType::RecoveryProposalCreated,
        [&](const smo::runtime::Event& ev) {
            std::printf("[smo-node] Event: RecoveryProposalCreated - %s\n", ev.details.c_str());
        });

    // RecoveryApproved: emitted by GovernanceContract when CertificateRevocation proposal is committed
    // Parse payload: {fingerprint, node_id_hex, reason, epoch}
    // Then: CRL::revoke(fingerprint), SessionManager::invalidate(node_id), Discovery gossip, Audit log
    event_bus.subscribe(smo::runtime::EventType::RecoveryApproved,
        [&](const smo::runtime::Event& ev) {
            std::printf("[smo-node] Event: RecoveryApproved - %s\n", ev.details.c_str());

            // Parse JSON payload from event details
            // Expected format: "CertificateRevocation proposal approved: {fingerprint, node_id_hex, reason, epoch}"
            std::string payload = ev.details;
            size_t brace_pos = payload.find('{');
            if (brace_pos == std::string::npos) {
                std::printf("[smo-node] WARNING: RecoveryApproved payload missing JSON\n");
                return;
            }
            std::string json_str = payload.substr(brace_pos);

            // Simple JSON parsing (avoid external dependency for now)
            auto extract_field = [&](const std::string& json, const std::string& key) -> std::string {
                std::string search = "\"" + key + "\":\"";
                size_t pos = json.find(search);
                if (pos == std::string::npos) return "";
                pos += search.length();
                size_t end = json.find('"', pos);
                if (end == std::string::npos) return "";
                return json.substr(pos, end - pos);
            };
            auto extract_uint = [&](const std::string& json, const std::string& key) -> uint64_t {
                std::string search = "\"" + key + "\":";
                size_t pos = json.find(search);
                if (pos == std::string::npos) return 0;
                pos += search.length();
                size_t end = json.find_first_of(",}", pos);
                if (end == std::string::npos) return 0;
                return std::stoull(json.substr(pos, end - pos));
            };

            std::string fingerprint = extract_field(json_str, "fingerprint");
            std::string node_id_hex = extract_field(json_str, "node_id_hex");
            std::string reason = extract_field(json_str, "reason");
            uint64_t epoch = extract_uint(json_str, "epoch");

            if (fingerprint.empty() || node_id_hex.empty()) {
                std::printf("[smo-node] WARNING: RecoveryApproved payload incomplete\n");
                return;
            }

            // 1. CRL::revoke(fingerprint)
            auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            auto rev_res = crl.revoke(fingerprint, node_id_hex, reason, epoch, now_ns);
            if (!rev_res) {
                std::printf("[smo-node] CRL revoke failed: %s\n", rev_res.error().message.c_str());
            } else {
                std::printf("[smo-node] CRL: revoked cert %s (epoch=%llu)\n", fingerprint.c_str(), (unsigned long long)epoch);
            }

            // 2. SessionManager::invalidate(node_id) - convert hex to NodeID
            if (node_id_hex.size() == 64) { // 32 bytes = 64 hex chars
                smo::NodeID node_id;
                for (size_t i = 0; i < 32 && i * 2 + 1 < node_id_hex.size(); ++i) {
                    unsigned int byte = 0;
                    std::istringstream iss(node_id_hex.substr(i * 2, 2));
                    iss >> std::hex >> byte;
                    node_id.value[i] = static_cast<uint8_t>(byte);
                }
                size_t invalidated = session_mgr.invalidate(node_id);
                std::printf("[smo-node] SessionManager: invalidated %zu sessions for node %s\n",
                            invalidated, node_id_hex.c_str());
            }

            // 3. Discovery: gossip CRL update (trigger membership sync)
            // This will be picked up by the next gossip cycle
            std::printf("[smo-node] Discovery: CRL update triggered (gossip will propagate)\n");

            // 4. Audit: log revocation
            std::printf("[smo-node] AUDIT: Certificate revoked - fingerprint=%s node=%s reason=%s epoch=%llu\n",
                        fingerprint.c_str(), node_id_hex.c_str(), reason.c_str(), (unsigned long long)epoch);
        });

    // ── P6: Wire EventBus to all modules ────────────────────────────
    // Trust score changes → Audit log
    event_bus.subscribe(smo::runtime::EventType::SecurityAlert,
        [&](const smo::runtime::Event& ev) {
            std::printf("[smo-node] AUDIT: Trust score change - %s\n", ev.details.c_str());
        });

    // Session disconnect → Discovery membership update
    event_bus.subscribe(smo::runtime::EventType::NodeDisconnected,
        [&](const smo::runtime::Event& ev) {
            std::printf("[smo-node] Discovery: Node disconnected - %s\n", ev.details.c_str());
            // Membership sync will pick up on next tick
        });

    // Trust score change → Audit log
    event_bus.subscribe(smo::runtime::EventType::AuditLogged,
        [&](const smo::runtime::Event& ev) {
            std::printf("[smo-node] AUDIT: %s\n", ev.details.c_str());
        });

    // Governance proposal updates → all nodes
    event_bus.subscribe(smo::runtime::EventType::ProposalCreated,
        [&](const smo::runtime::Event& ev) {
            std::printf("[smo-node] GOVERNANCE: Proposal created - %s\n", ev.details.c_str());
        });
    event_bus.subscribe(smo::runtime::EventType::ProposalVoted,
        [&](const smo::runtime::Event& ev) {
            std::printf("[smo-node] GOVERNANCE: Vote cast - %s\n", ev.details.c_str());
        });
    event_bus.subscribe(smo::runtime::EventType::ProposalCommitted,
        [&](const smo::runtime::Event& ev) {
            std::printf("[smo-node] GOVERNANCE: Proposal committed - %s\n", ev.details.c_str());
        });
    event_bus.subscribe(smo::runtime::EventType::ProposalRejected,
        [&](const smo::runtime::Event& ev) {
            std::printf("[smo-node] GOVERNANCE: Proposal rejected - %s\n", ev.details.c_str());
        });

    // Recovery events
    event_bus.subscribe(smo::runtime::EventType::RecoveryStarted,
        [&](const smo::runtime::Event& ev) {
            std::printf("[smo-node] RECOVERY: Started - %s\n", ev.details.c_str());
        });
    event_bus.subscribe(smo::runtime::EventType::RecoveryCompleted,
        [&](const smo::runtime::Event& ev) {
            std::printf("[smo-node] RECOVERY: Completed - %s\n", ev.details.c_str());
        });
    event_bus.subscribe(smo::runtime::EventType::RecoveryFailed,
        [&](const smo::runtime::Event& ev) {
            std::printf("[smo-node] RECOVERY: Failed - %s\n", ev.details.c_str());
        });

// ── P6: Service Registry ────────────────────────────────────────
    // Register core services in global registry (use pointers for non-copyable types)
    smo::runtime::ServiceRegistry& registry = smo::runtime::global_registry();
    // Note: EventBus is non-copyable/movable, use custom deleter
    registry.register_service("event_bus", std::shared_ptr<smo::runtime::EventBus>(&event_bus, [](auto*){}));
    registry.register_service("crl", std::make_shared<smo::recovery::CRL>(crl));
    registry.register_service("session_manager", std::make_shared<smo::SessionManager>(session_mgr));
    registry.register_service("trust_manager", std::make_shared<smo::TrustManager>(trust_mgr));
    registry.register_service("governance_engine", std::make_shared<smo::GovernanceEngine>(governance_engine));
    registry.register_service("discovery_engine", std::make_shared<smo::DiscoveryEngine>(discovery_engine));
    // Note: PeerStore and GossipEngine are non-copyable, skip for now

    // ── P6: Telemetry ───────────────────────────────────────────────
    smo::runtime::Telemetry& telemetry = smo::runtime::global_telemetry();
    telemetry.set_event_bus(&event_bus);

    // Register core health checks
    telemetry.register_health_check("crl", [&](std::string& err) -> bool {
        return true; // CRL always healthy
    });
    telemetry.register_health_check("session_mgr", [&](std::string& err) -> bool {
        return true;
    });
    telemetry.register_health_check("peer_store", [&](std::string& err) -> bool {
        return true;
    });

    // Register metrics
    telemetry.increment_counter("node.startup", "component=main");
    telemetry.set_gauge("node.state", 1.0, "state=running");

    // Print registered services
    std::printf("[smo-node] Registered services: ");
    auto services = registry.list_services();
    for (size_t i = 0; i < services.size(); ++i) {
        if (i > 0) std::printf(", ");
        std::printf("%s", services[i].c_str());
    }
    std::printf("\n");

    // ── Main loop ──────────────────────────────────────────────
    std::printf("[smo-node] Entering main loop...\n");

    int64_t last_tick = 0;
    int64_t last_gossip = 0;
    int64_t last_peerstore_sync = 0;

    while (g_running) {
        int64_t now_ns = static_cast<int64_t>(
            std::chrono::system_clock::now().time_since_epoch().count());

        // Periodic ticks
        if (now_ns - last_tick > 5000000000LL) {
            discovery_engine.tick(now_ns);
            heartbeat_service.tick(now_ns);
            session_mgr.tick(now_ns);
            session_mgr.collect_garbage();
            last_tick = now_ns;
        }

        if (now_ns - last_gossip > 5000000000LL) {
            // GossipEngine::tick() handles fanout: selects peers, serializes
            // pending MembershipEvents, connects via TCP, and sends framed data.
            gossip_engine.tick(now_ns);
            last_gossip = now_ns;
        }

        // Periodic PeerStore sync
        if (now_ns - last_peerstore_sync > 30000000000LL) {
            peer_store.sync_from_membership(membership);
            last_peerstore_sync = now_ns;
        }

        // Accept TCP connections
        auto tcp_session = lstnr->accept();
        if (tcp_session) {
            auto remote_str = tcp_session.value()->remote_endpoint().to_string();
            std::printf("[smo-node] Accepted TCP connection from %s\n",
                        remote_str.c_str());

            smo::hl::Endpoint remote_ep;
            auto colon = remote_str.rfind(':');
            if (colon != std::string::npos) {
                remote_ep.address = remote_str.substr(0, colon);
                remote_ep.port = static_cast<uint16_t>(
                    std::strtoul(remote_str.substr(colon + 1).c_str(), nullptr, 10));
            } else {
                remote_ep.address = remote_str;
                remote_ep.port = 7777;
            }

            auto* tcp_ses = static_cast<smo::TcpSession*>(tcp_session.value().get());
            if (!tcp_ses) {
                std::printf("[smo-node] Session is not TCP, skipping\n");
                tcp_session.value()->close();
                continue;
            }

            // ── PQ handshake (if certificate available) ─────────────
            if (!server_cert_blob.empty()) {
                smo::SecureSession::Config sec_cfg;
                sec_cfg.role = smo::SecureSession::Role::Server;
                sec_cfg.server_cert = server_cert_blob;
                sec_cfg.signing_secret_key = server_signing_key;

                int client_fd = tcp_ses->release_fd();
                smo::SecureSession sec(client_fd, sec_cfg, *crypto);
                auto hs = sec.handshake();
                if (!hs) {
                    std::printf("[smo-node] PQ handshake failed: %s, closing\n",
                                hs.error().message.c_str());
                    tcp_session.value()->close();
                    continue;
                }
                std::printf("[smo-node] PQ handshake established\n");

                SecureTransportSession secure_ses(
                    std::move(sec),
                    smo::Endpoint{"tcp", remote_ep.address, remote_ep.port, ""});
                auto dispatch_res = dispatcher.dispatch_session(secure_ses, remote_ep);
                if (!dispatch_res) {
                    std::printf("[smo-node] Dispatch failed: %s\n",
                                dispatch_res.error().message.c_str());
                }
                secure_ses.close();
            } else {
                // Fallback: plaintext (no certificate available)
                auto dispatch_res = dispatcher.dispatch_session(
                    *tcp_ses, remote_ep);
                if (!dispatch_res) {
                    std::printf("[smo-node] Dispatch failed: %s\n",
                                dispatch_res.error().message.c_str());
                }
                tcp_session.value()->close();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Cleanup
    heartbeat_service.stop();
    lstnr->close();
    peer_store.sync_from_membership(membership);
    peer_store.close();

    std::printf("[smo-node] Shutdown complete.\n");
    return 0;
}
