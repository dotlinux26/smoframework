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

#include <providers/suite1_classical/suite1_classical_provider.hpp>
#include <providers/suite3_purepqc/suite3_purepqc_provider.hpp>

#include <tooling/clipboard.hpp>

#include <chrono>
#include <csignal>
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

    // Initialize core components
    smo::MembershipTable membership;
    smo::HealthMonitor health_monitor;
    auto* tcp_transport_ptr = smo::TransportRegistry::instance().get("tcp");
    smo::Transport& transport_ref = *tcp_transport_ptr;
    smo::DiscoveryEngine discovery_engine(membership, health_monitor, transport_ref);
    auto gossip_cfg = smo::GossipEngine::default_config();
    smo::GossipEngine gossip_engine(membership, gossip_cfg);
    smo::network::sync::MembershipSync membership_sync(membership, health_monitor);

    // Register transports
    smo::TransportRegistry::instance().register_transport(
        std::make_unique<smo::TcpTransport>(), "tcp");
    smo::TransportRegistry::instance().register_transport(
        std::make_unique<smo::network::udp::UdpTransport>(), "udp");

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
            last_tick = now_ns;
        }

        if (now_ns - last_gossip > 5000000000LL) {
            gossip_engine.tick(now_ns);
            auto events = membership_sync.pending_events(gossip_engine.current_sequence());
            if (!events.empty()) {
                // TODO: Send gossip messages to fanout peers
            }
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
            std::printf("[smo-node] Accepted TCP connection from %s\n",
                        tcp_session.value()->remote_endpoint().to_string().c_str());

            auto data = tcp_session.value()->recv(4096);
            if (data) {
                std::printf("[smo-node] Received %zu bytes from %s\n",
                            data.value().size(),
                            tcp_session.value()->remote_endpoint().to_string().c_str());
                tcp_session.value()->send(data.value());
            }
            tcp_session.value()->close();
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
