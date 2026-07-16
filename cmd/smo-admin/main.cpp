#include <core/authority/authority.hpp>
#include <core/authority/registry.hpp>
#include <core/certificate/certificate.hpp>
#include <core/crypto/impl.hpp>
#include <core/crypto/registry.hpp>
#include <core/crypto/suite.hpp>
#include <core/enroll/join_token.hpp>
#include <core/errors/error.hpp>
#include <core/types.hpp>
#include <core/mesh/mesh_manager.hpp>
#include <core/network/interface.hpp>
#include <core/network/public_ip.hpp>
#include <core/network/port_check.hpp>
#include <core/network/dns.hpp>
#include <core/network/nat_detect.hpp>

#include <providers/suite1_classical/suite1_classical_provider.hpp>
#include <providers/suite3_purepqc/suite3_purepqc_provider.hpp>

#include <tooling/clipboard.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using smo::Bytes;
using smo::BytesView;

static void print_usage(const char* prog) {
    std::fprintf(stderr, R"(SMO Admin — Mesh Administration

Usage:
  %s --mesh-dir <dir> sign <csr-file> -o <output-file>
  %s --mesh-dir <dir> create-mesh <name>
  %s --mesh-dir <dir> mesh publish [--listen <addr>] [--advertise <addr>...] [--dns <name>]
  %s --mesh-dir <dir> generate-invite <role> [--expire <dur>] [--endpoint <ep>]
  %s --help

Commands:
  sign            Sign a CSR (.smor) and issue a certificate (.smoc)
  create-mesh     Initialize a new mesh (generate root + authority keys)
  mesh publish    Configure network endpoints (bootstrap) for the mesh
  generate-invite Generate a Join Token for automated enrollment
)",
        prog, prog, prog, prog, prog);
}

// ---------------------------------------------------------------------------
// Load a binary file
// ---------------------------------------------------------------------------
static Bytes load_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto size = f.tellg();
    f.seekg(0);
    Bytes data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// ---------------------------------------------------------------------------
// Write a binary file
// ---------------------------------------------------------------------------
static bool write_file(const std::string& path, BytesView data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    return f.good();
}

// ---------------------------------------------------------------------------
// Register all available crypto suites once at startup
// ---------------------------------------------------------------------------
static void register_all_suites() {
    smo::providers::register_suite1_classical();
    smo::providers::register_suite3_purepqc();
}

// ---------------------------------------------------------------------------
// Get crypto provider for a given cipher_suite_id from the registry
// ---------------------------------------------------------------------------
static bool get_crypto(smo::CryptoSuiteID suite_id,
                        smo::CryptoProvider const*& provider_out,
                        smo::RngRef& rng_out) {
    auto& reg = smo::CryptoRegistry::instance();
    auto prov_result = reg.get_suite(suite_id);
    if (!prov_result) {
        std::fprintf(stderr, "Error: cipher suite %u not registered\n",
                     (unsigned)suite_id);
        return false;
    }
    provider_out = prov_result.value();
    rng_out = provider_out->default_rng();
    return true;
}

// ---------------------------------------------------------------------------
// Read a string field from JSON (simple parser — no JSON lib needed)
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

// Read integer field from JSON
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

// Read cipher_suite_id from mesh.json
// ---------------------------------------------------------------------------
static smo::CryptoSuiteID read_suite_from_mesh(const std::string& mesh_dir) {
    std::string path = mesh_dir + "/mesh.json";
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "Warning: no mesh.json found at %s, using default suite\n",
                     path.c_str());
        return smo::kSuitePurePQC;
    }
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    auto pos = content.find("\"cipher_suite_id\"");
    if (pos == std::string::npos) return smo::kSuitePurePQC;
    auto colon = content.find(':', pos);
    if (colon == std::string::npos) return smo::kSuitePurePQC;
    auto val_start = content.find_first_of("0123456789", colon);
    if (val_start == std::string::npos) return smo::kSuitePurePQC;
    auto val_end = content.find_first_not_of("0123456789", val_start);
    std::string num = content.substr(val_start, val_end - val_start);
    return static_cast<smo::CryptoSuiteID>(std::stoul(num));
}

// ---------------------------------------------------------------------------
// cmd_sign: Load CSR, sign via authority, output .smoc
//
// Transports:
//   smo-admin sign <csr-file> -o <cert-file>  (file)
//   smo-admin sign <csr-file> --copy           (clipboard)
//   smo-admin sign --paste -o <cert-file>      (clipboard in)
//   smo-admin sign <csr-file>                  (stdout)
// ---------------------------------------------------------------------------
static int cmd_sign(const std::vector<std::string>& args,
                     const std::string& mesh_dir) {
    std::string input_file;
    std::string output_file;
    bool do_copy = false;
    bool do_paste = false;

    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-o" && i + 1 < args.size()) {
            output_file = args[++i];
        } else if (args[i] == "--copy") {
            do_copy = true;
        } else if (args[i] == "--paste") {
            do_paste = true;
        } else if (input_file.empty() && !args[i].starts_with("-")) {
            input_file = args[i];
        }
    }

    // Load CSR
    smo::Bytes csr_blob;

    if (do_paste) {
        // Read CSR from clipboard
        auto clip = smo::clipboard_paste();
        if (clip.empty()) {
            std::fprintf(stderr, "Error: clipboard is empty or unavailable\n");
            return 1;
        }
        csr_blob = smo::Bytes(clip.begin(), clip.end());
        std::fprintf(stderr, "[smo-admin] Read CSR from clipboard (%zu bytes)\n", csr_blob.size());
    } else if (!input_file.empty()) {
        if (!fs::exists(input_file)) {
            std::fprintf(stderr, "Error: CSR file not found: %s\n", input_file.c_str());
            return 1;
        }
        csr_blob = load_file(input_file);
        if (csr_blob.empty()) {
            std::fprintf(stderr, "Error: cannot read CSR file: %s\n", input_file.c_str());
            return 1;
        }
    } else {
        std::fprintf(stderr, "Usage:\n"
                             "  smo-admin sign <csr-file> -o <cert-file>\n"
                             "  smo-admin sign <csr-file> --copy\n"
                             "  smo-admin sign --paste [-o <cert-file>]\n");
        return 1;
    }

    // Read cipher suite from mesh config and get crypto provider
    auto suite_id = read_suite_from_mesh(mesh_dir);
    const smo::CryptoProvider* crypto = nullptr;
    smo::RngRef rng;
    if (!get_crypto(suite_id, crypto, rng)) return 1;

    // Open mesh authority
    smo::authority::MeshAuthority authority;
    if (auto r = authority.init(*crypto, rng); !r) {
        std::fprintf(stderr, "Error: authority init failed: %s\n",
                     r.error().message.c_str());
        return 1;
    }

    smo::authority::MeshAuthority::Config cfg;
    cfg.mesh_id = fs::path(mesh_dir).filename().string();
    cfg.data_dir = mesh_dir;
    cfg.registry_path = mesh_dir + "/node_registry.db";

    if (auto r = authority.open(cfg); !r) {
        std::fprintf(stderr, "Error: cannot open mesh authority at %s: %s\n",
                     mesh_dir.c_str(), r.error().message.c_str());
        return 1;
    }

    // Sign CSR
    auto cert_result = authority.sign_csr(csr_blob, cfg.mesh_id);
    if (!cert_result) {
        std::fprintf(stderr, "Error: CSR signing failed: %s\n",
                     cert_result.error().message.c_str());
        return 1;
    }

    // Serialize certificate
    auto cert_serialized = cert_result.value().serialize_full();
    auto cert_fp = crypto->hash.hash(cert_serialized);
    std::string fp_hex = cert_fp ? smo::bytes_to_hex(cert_fp.value()).substr(0, 16) : "???";

    if (do_copy) {
        // Copy to clipboard
        if (smo::clipboard_copy(std::string(cert_serialized.begin(), cert_serialized.end()))) {
            std::printf("Certificate signed. Fingerprint: %s\n", fp_hex.c_str());
            std::printf("Copied to clipboard.\n");
            return 0;
        }
        std::fprintf(stderr, "Error: clipboard not available\n");
        return 1;
    }

    if (output_file.empty()) {
        // Write to stdout
        fwrite(cert_serialized.data(), 1, cert_serialized.size(), stdout);
        return 0;
    }

    // Write to file
    if (!write_file(output_file, cert_serialized)) {
        std::fprintf(stderr, "Error: cannot write output file: %s\n",
                     output_file.c_str());
        return 1;
    }

    std::printf("Certificate signed: %s (%zu bytes)\n", fp_hex.c_str(), cert_serialized.size());
    return 0;
}

// ---------------------------------------------------------------------------
// cmd_create_mesh: Initialize new mesh with root + authority keys
// ---------------------------------------------------------------------------
static int cmd_create_mesh(const std::string& name,
                            const std::string& output_dir) {
    fs::path base_data_dir = output_dir;
    fs::create_directories(base_data_dir);

    // Use default suite for mesh creation (can be overridden via --suite flag later)
    const smo::CryptoProvider* crypto = nullptr;
    smo::RngRef rng;
    if (!get_crypto(smo::kSuitePurePQC, crypto, rng)) return 1;

    // Create mesh via MeshManager (which writes mesh.json)
    smo::MeshManager::Config mgr_cfg;
    mgr_cfg.base_data_dir = base_data_dir.string();
    smo::MeshManager mgr(mgr_cfg);
    auto init_result = mgr.initialize();
    if (!init_result) {
        std::fprintf(stderr, "Error: failed to initialize mesh manager: %s\n",
                     init_result.error().message.c_str());
        return 1;
    }

    smo::MeshConfig mesh_cfg;
    mesh_cfg.display_name = name;
    mesh_cfg.cipher_suite_id = smo::kSuitePurePQC;
    mesh_cfg.created_at = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    auto create_result = mgr.create_mesh(mesh_cfg, name);
    if (!create_result) {
        std::fprintf(stderr, "Error: failed to create mesh: %s\n",
                     create_result.error().message.c_str());
        return 1;
    }

    // Debug: list all meshes
    std::fprintf(stderr, "Debug: listing meshes...\n");
    auto mesh_list = mgr.list_meshes();
    for (const auto& m : mesh_list) {
        std::fprintf(stderr, "  Found: %s\n", m.c_str());
    }

    // Load the created mesh to get its mesh_id and path
    auto mesh_result = mgr.get_mesh_by_name(name);
    if (!mesh_result) {
        std::fprintf(stderr, "Error: failed to load created mesh\n");
        return 1;
    }
    auto mesh_ctx = mesh_result.value();
    auto& mesh_config = mesh_ctx->config;

    // Now create authority keys via MeshAuthority
    if (!get_crypto(smo::kSuitePurePQC, crypto, rng)) return 1;

    smo::authority::MeshAuthority authority;
    if (auto r = authority.init(*crypto, rng); !r) {
        std::fprintf(stderr, "Error: authority init failed: %s\n",
                     r.error().message.c_str());
        return 1;
    }

    smo::authority::MeshAuthority::Config cfg;
    cfg.mesh_id = mesh_config.mesh_id;
    cfg.data_dir = mesh_ctx->paths.mesh_dir;
    cfg.registry_path = mesh_ctx->paths.peers_db;

    std::string root_pubkey_hex;
    if (auto r = authority.create_mesh_keys(cfg, *crypto, rng, root_pubkey_hex); !r) {
        std::fprintf(stderr, "Error: create mesh keys failed: %s\n",
                     r.error().message.c_str());
        return 1;
    }

    // Also write the root public key hex to a file for reference
    std::ofstream rpk(mesh_ctx->paths.mesh_dir + "/root.pub.hex");
    rpk << root_pubkey_hex;
    rpk.close();

    std::printf("Mesh '%s' created at %s\n", name.c_str(), mesh_ctx->paths.mesh_dir.c_str());
    std::printf("  Mesh ID: %s\n", mesh_config.mesh_id.c_str());
    std::printf("  Root public key: %s\n", root_pubkey_hex.c_str());
    std::printf("  Authority keys:  %s/authority.pub, %s/authority.sec\n",
                mesh_ctx->paths.mesh_dir.c_str(), mesh_ctx->paths.mesh_dir.c_str());
    std::printf("  Root cert:       %s/root.cert\n", mesh_ctx->paths.mesh_dir.c_str());
    std::printf("  Authority cert:  %s/authority.cert\n", mesh_ctx->paths.mesh_dir.c_str());
    std::printf("  Node registry:   %s/node_registry.db\n", mesh_ctx->paths.mesh_dir.c_str());
    return 0;
}

// ---------------------------------------------------------------------------
// cmd_generate_invite: Generate a Join Token
// ---------------------------------------------------------------------------
// Options:
//   <role>               — "Worker", "Validator", "Observer", "Relay"
//   --expire <dur>       — duration like "30m", "1h", "7d" (default: 1h)
//   --endpoint <ep>      — bootstrap endpoint (can be repeated)
//     e.g. authority.company.com:7777
// ---------------------------------------------------------------------------
static int cmd_generate_invite(const std::vector<std::string>& args,
                                const std::string& mesh_dir) {
    std::string role;
    std::string expire_dur = "1h";
    std::vector<std::string> endpoints;

    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--expire" && i + 1 < args.size()) {
            expire_dur = args[++i];
        } else if (args[i] == "--endpoint" && i + 1 < args.size()) {
            endpoints.push_back(args[++i]);
        } else if (role.empty() && !args[i].starts_with("-")) {
            role = args[i];
        }
    }

    if (role.empty()) {
        std::fprintf(stderr, "Usage: smo-admin --mesh-dir <dir> generate-invite <role> [--expire <dur>] [--endpoint <ep>...]\n");
        return 1;
    }

    // Read mesh.json
    std::string path = mesh_dir + "/mesh.json";
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "Error: no mesh.json found at %s\n", path.c_str());
        return 1;
    }
    std::string json((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());

    std::string mesh_id         = json_read_string(json, "mesh_id");
    if (mesh_id.empty()) mesh_id = fs::path(mesh_dir).filename().string();
    std::string hmac_secret_hex = json_read_string(json, "hmac_secret");
    int64_t mesh_epoch          = json_read_int(json, "epoch", 1);
    auto suite_id               = read_suite_from_mesh(mesh_dir);

    if (hmac_secret_hex.empty()) {
        std::fprintf(stderr, "Error: mesh.json has no hmac_secret\n");
        return 1;
    }

    // If no manual endpoints provided, read bootstrap_endpoints from mesh.json
    if (endpoints.empty()) {
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
                        endpoints.push_back(arr.substr(q1 + 1, q2 - q1 - 1));
                        pos = q2 + 1;
                    }
                }
            }
        }
    }

    if (endpoints.empty()) {
        std::fprintf(stderr, "Error: no bootstrap endpoints configured. Run 'smo-admin mesh publish' first.\n");
        return 1;
    }

    Bytes hmac_secret;
    for (size_t i = 0; i + 1 < hmac_secret_hex.size(); i += 2) {
        char buf[3] = {hmac_secret_hex[i], hmac_secret_hex[i+1], 0};
        hmac_secret.push_back(static_cast<uint8_t>(std::strtoul(buf, nullptr, 16)));
    }

    const smo::CryptoProvider* crypto = nullptr;
    smo::RngRef rng;
    if (!get_crypto(suite_id, crypto, rng)) return 1;

    int64_t expiry = 0;
    if (!expire_dur.empty() && expire_dur != "0") {
        char unit = expire_dur.back();
        int64_t num = 0;
        try { num = std::stoll(expire_dur.substr(0, expire_dur.size() - 1)); } catch (...) {}
        int64_t mult = 3600;
        switch (unit) {
            case 's': mult = 1; break;
            case 'm': mult = 60; break;
            case 'h': mult = 3600; break;
            case 'd': mult = 86400; break;
        }
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        expiry = now + num * mult;
    }

    auto token_result = smo::enroll::generate_token(
        mesh_id, mesh_epoch, static_cast<int>(suite_id),
        endpoints, role, expiry, hmac_secret, crypto->hash
    );
    if (!token_result) {
        std::fprintf(stderr, "Error: token generation failed: %s\n",
                     token_result.error().message.c_str());
        return 1;
    }

    auto wire = smo::enroll::encode_token_wire(token_result.value());
    std::printf("%s\n", wire.c_str());
    return 0;
}

// ---------------------------------------------------------------------------
// cmd_mesh_publish: Configure bootstrap endpoints for mesh
// ---------------------------------------------------------------------------
// Options:
//   --listen <addr>      Listen address [default: 0.0.0.0:7777]
//   --advertise <addr>   Advertise address (repeatable, overrides auto-detect)
//   --dns <name>         DNS name to use as advertise address (preferred)
//   --port <n>           Port [default: 7777]
// ---------------------------------------------------------------------------
static int cmd_mesh_publish(const std::vector<std::string>& args,
                            const std::string& mesh_dir) {
    std::string listen_addr = "0.0.0.0:7777";
    std::vector<std::string> manual_advertise;
    std::string manual_dns;
    uint16_t port = 7777;

    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--listen" && i + 1 < args.size()) {
            listen_addr = args[++i];
        } else if (args[i] == "--advertise" && i + 1 < args.size()) {
            manual_advertise.push_back(args[++i]);
        } else if (args[i] == "--dns" && i + 1 < args.size()) {
            manual_dns = args[++i];
        } else if (args[i] == "--port" && i + 1 < args.size()) {
            port = static_cast<uint16_t>(std::stoi(args[++i]));
        }
    }

    // Load existing mesh config
    auto config_result = smo::MeshManager::load_mesh_config(mesh_dir);
    if (!config_result) {
        std::fprintf(stderr, "Error: %s\n", config_result.error().message.c_str());
        return 1;
    }
    auto config = config_result.value();

    // If mesh_id is empty from JSON, use directory name
    if (config.mesh_id.empty()) {
        config.mesh_id = std::filesystem::path(mesh_dir).filename().string();
    }

    std::printf("Mesh: %s\n\n", config.display_name.empty() ? config.mesh_id.c_str() : config.display_name.c_str());

    // Step 1: Show listen address
    std::printf("Step 1: Listen Address\n");
    std::printf("  Bind address [%s]: ", listen_addr.c_str());
    // In non-interactive mode, just use the default/flag
    // For interactive, we'd read stdin. For now, just use the flag.
    std::printf("%s\n", listen_addr.c_str());

    // Step 2: Port check
    std::printf("\nStep 2: Port Check\n");
    std::printf("  Checking port %u...\n", port);
    std::error_code ec;
    bool port_free = smo::net::check_port_available(listen_addr.substr(0, listen_addr.find(':')), port, ec);
    if (port_free) {
        std::printf("  ✓ Port %u is available\n", port);
    } else {
        std::string who = smo::net::who_is_on_port(port);
        if (who.empty()) {
            std::fprintf(stderr, "  ✗ Port %u in use\n", port);
        } else {
            std::fprintf(stderr, "  ✗ Port %u in use by %s\n", port, who.c_str());
        }
        // Don't exit - just warn
    }

    // Step 3: Detect interfaces
    std::printf("\nStep 3: Interface Detection\n");
    std::error_code ec2;
    auto interfaces = smo::net::enumerate_interfaces(ec2);
    if (ec2) {
        std::fprintf(stderr, "  Warning: failed to enumerate interfaces: %s\n", ec2.message().c_str());
    }

    std::vector<smo::net::InterfaceInfo> private_ifs;
    std::vector<smo::net::InterfaceInfo> public_ifs;
    for (const auto& iface : interfaces) {
        if (iface.is_loopback) continue;
        if (iface.is_private) private_ifs.push_back(iface);
        else public_ifs.push_back(iface);
    }

    std::printf("  Detected interfaces:\n");
    int idx = 1;
    for (const auto& iface : private_ifs) {
        std::printf("    %d) %s (%s) [private]\n", idx++, iface.name.c_str(), iface.address.c_str());
    }
    for (const auto& iface : public_ifs) {
        std::printf("    %d) %s (%s) [public]\n", idx++, iface.name.c_str(), iface.address.c_str());
    }

    // Step 4: Choose advertise address
    std::vector<std::string> advertise_addresses;

    if (!manual_advertise.empty()) {
        // Manual override
        advertise_addresses = manual_advertise;
        std::printf("\nStep 4: Advertise Address (manual)\n");
        for (const auto& addr : advertise_addresses) {
            std::printf("  %s\n", addr.c_str());
        }
    } else if (!manual_dns.empty()) {
        // DNS name provided - resolve it
        std::printf("\nStep 4: Advertise Address (DNS)\n");
        std::printf("  Resolving %s...\n", manual_dns.c_str());
        std::error_code dns_ec;
        std::string resolved = smo::net::resolve_hostname(manual_dns, dns_ec);
        if (dns_ec) {
            std::fprintf(stderr, "  ✗ Failed to resolve %s: %s\n", manual_dns.c_str(), dns_ec.message().c_str());
            return 1;
        }
        std::string endpoint = resolved + ":" + std::to_string(port);
        advertise_addresses.push_back(endpoint);
        // Also add DNS name if it's different
        std::string dns_endpoint = manual_dns + ":" + std::to_string(port);
        if (dns_endpoint != endpoint) {
            advertise_addresses.insert(advertise_addresses.begin(), dns_endpoint);
        }
        std::printf("  Resolved to: %s\n", resolved.c_str());
        for (const auto& addr : advertise_addresses) {
            std::printf("  Will advertise: %s\n", addr.c_str());
        }
    } else {
        // Auto-detect
        std::printf("\nStep 4: Advertise Address (auto-detect)\n");

        // Try public IP detection
        std::string public_ip = smo::net::detect_public_ip(ec2);
        if (!public_ip.empty() && smo::net::is_public_address(public_ip)) {
            std::string endpoint = public_ip + ":" + std::to_string(port);
            advertise_addresses.push_back(endpoint);
            std::printf("  ✓ Public IP detected: %s\n", public_ip.c_str());
        } else {
            std::printf("  ⚠ No public IP detected\n");
        }

        // Add private IPs as fallback
        for (const auto& iface : private_ifs) {
            std::string endpoint = iface.address + ":" + std::to_string(port);
            advertise_addresses.push_back(endpoint);
            std::printf("  ✓ Private IP: %s\n", endpoint.c_str());
        }

        if (advertise_addresses.empty()) {
            // Last resort: loopback
            advertise_addresses.push_back("127.0.0.1:" + std::to_string(port));
            std::printf("  Using loopback as last resort\n");
        }
    }

    // Step 5: NAT detection
    std::printf("\nStep 5: NAT Detection\n");
    if (!advertise_addresses.empty() && !public_ifs.empty()) {
        std::string first_advertise = advertise_addresses[0];
        // Extract IP from endpoint
        size_t colon = first_advertise.rfind(':');
        std::string advertise_ip = (colon != std::string::npos) ? first_advertise.substr(0, colon) : first_advertise;

        std::string private_ip = private_ifs.empty() ? "" : private_ifs[0].address;
        auto nat = smo::net::detect_nat(private_ip, advertise_ip);
        if (nat.behind_nat) {
            std::printf("  ⚠ NAT detected:\n");
            std::printf("    Private:  %s\n", nat.private_ip.c_str());
            std::printf("    Public:   %s\n", nat.public_ip.c_str());
            if (nat.port_forwarding_required) {
                std::printf("    Port forwarding REQUIRED on your router/firewall\n");
            }
        } else {
            std::printf("  ✓ No NAT detected (direct connectivity)\n");
        }
    }

    // Step 6: Cloud firewall reminder
    std::printf("\nStep 6: Cloud Firewall\n");
    std::printf("  ┌────────────────────────────────────────────────────┐\n");
    std::printf("  │ Remember to open TCP port %u on your firewall:       │\n", port);
    std::printf("  │                                                    │\n");
    std::printf("  │   AWS:    EC2 → Security Groups → Inbound → %u     │\n", port);
    std::printf("  │   Azure:  NSG → Inbound security rule → %u         │\n", port);
    std::printf("  │   GCP:    VPC → Firewall → Ingress → tcp:%u        │\n", port);
    std::printf("  │   OCI:    Security List → Ingress → %u             │\n", port);
    std::printf("  │   UFW:    sudo ufw allow %u/tcp                    │\n", port);
    std::printf("  │   iptables: sudo iptables -A INPUT -p tcp          │\n");
    std::printf("  │             --dport %u -j ACCEPT                   │\n", port);
    std::printf("  └────────────────────────────────────────────────────┘\n");

    // Step 7: Confirm
    std::printf("\nStep 7: Confirm\n");
    std::printf("  Listen:     %s\n", listen_addr.c_str());
    std::printf("  Advertise:  ");
    for (size_t i = 0; i < advertise_addresses.size(); ++i) {
        if (i > 0) std::printf("               ");
        std::printf("%s\n", advertise_addresses[i].c_str());
    }
    std::printf("  Bootstrap:  YES\n");
    std::printf("\n  Publish? [Y/n]: ");

    // Non-interactive: assume Y
    std::string confirm = "y";
    // In real interactive mode, we'd read from stdin
    // For now, just proceed
    std::printf("%s\n", confirm.c_str());

    if (confirm.empty() || confirm[0] == 'y' || confirm[0] == 'Y') {
        // Prepare bootstrap_endpoints (same as advertise_addresses for now)
        std::vector<std::string> bootstrap_endpoints = advertise_addresses;

        // Use MeshManager to publish
        // base_data_dir should be the parent of meshes/ directory
        smo::MeshManager::Config mgr_cfg;
        mgr_cfg.base_data_dir = std::filesystem::path(mesh_dir).parent_path().parent_path().string();
        smo::MeshManager mgr(mgr_cfg);
        auto init_result = mgr.initialize();
        if (!init_result) {
            std::fprintf(stderr, "Error: failed to initialize mesh manager: %s\n",
                         init_result.error().message.c_str());
            return 1;
        }

        auto publish_result = mgr.publish_mesh(
            config.mesh_id,
            listen_addr,
            advertise_addresses,
            bootstrap_endpoints
        );
        if (!publish_result) {
            std::fprintf(stderr, "Error: publish failed: %s\n",
                         publish_result.error().message.c_str());
            return 1;
        }

        std::printf("\n✓ Mesh '%s' is now online.\n",
                    config.display_name.empty() ? config.mesh_id.c_str() : config.display_name.c_str());
        return 0;
    } else {
        std::printf("Cancelled.\n");
        return 1;
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 ||
            std::strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    // Parse --mesh-dir
    std::string mesh_dir;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mesh-dir") == 0 && i + 1 < argc) {
            mesh_dir = argv[++i];
            // Shift remaining args
            for (int j = i - 1; j + 2 < argc; ++j) argv[j] = argv[j + 2];
            argc -= 2;
            break;
        }
    }

    // Register all available crypto suites
    register_all_suites();

    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    auto cmd = args[0];

    if (cmd == "create-mesh") {
        std::string name = (args.size() > 1) ? args[1] : "";
        std::string output_dir = mesh_dir.empty()
            ? fs::current_path().string()
            : mesh_dir;
        if (name.empty()) {
            std::fprintf(stderr, "Usage: smo-admin --mesh-dir <dir> create-mesh <name>\n");
            return 1;
        }
        return cmd_create_mesh(name, output_dir);
    }

    if (cmd == "sign") {
        if (mesh_dir.empty()) {
            std::fprintf(stderr, "Error: --mesh-dir is required for sign command\n");
            return 1;
        }
        return cmd_sign(args, mesh_dir);
    }

    if (cmd == "generate-invite") {
        if (mesh_dir.empty()) {
            std::fprintf(stderr, "Error: --mesh-dir is required for generate-invite command\n");
            return 1;
        }
        return cmd_generate_invite(args, mesh_dir);
    }

    if (cmd == "mesh") {
        if (args.size() < 2) {
            std::fprintf(stderr, "Usage: smo-admin --mesh-dir <dir> mesh publish\n");
            return 1;
        }
        auto subcmd = args[1];
        if (subcmd == "publish") {
            if (mesh_dir.empty()) {
                std::fprintf(stderr, "Error: --mesh-dir is required for mesh publish\n");
                return 1;
            }
            return cmd_mesh_publish(args, mesh_dir);
        }
        std::fprintf(stderr, "Unknown mesh subcommand: %s\n", subcmd.c_str());
        return 1;
    }

    std::fprintf(stderr, "Unknown command: %s\n", cmd.c_str());
    print_usage(argv[0]);
    return 1;
}
