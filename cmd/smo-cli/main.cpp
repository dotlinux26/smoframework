// §XIX — CLI Design
// smo-cli: user-facing execution tool.
//
// smo exec --opcode ls --name backup-02 --path /var/log
// smo exec --opcode backup --tag Storage
// smo exec --opcode update --role Member
// smo exec --opcode sync --nearest
// smo mesh create --name "SOC-Production"
// smo mesh discover
// smo node init --name soc-hn-01
// smo node info
// smo node rename --name backup-storage
// smo node connect --address node-a:7777
// smo node import /path/to/cert.smoc
// smo export --format json > request.smor
// smo session open --name node-b

#include <core/crypto/impl.hpp>
#include <core/identity/identity.hpp>
#include <core/discovery/discovery.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void print_usage(const char* prog) {
    std::fprintf(stderr, R"(SMO CLI — Secure Mesh Operation

Usage:
  %s node init --name <display-name>
  %s node info
  %s node rename --name <new-name>
  %s node join --mesh <mesh> --token <token>
  %s node leave --mesh <mesh>
  %s node connect --address <host:port>
  %s node import <cert-file>

  %s exec --opcode <op> [--name <name>] [--id <nodeid>]
          [--role <role>] [--tag <tag>]... [--where <expr>]
          [--os <os>] [--arch <arch>] [--version <ver>]
          [--trust <range>] [--mesh <mesh>]
          [--nearest | --random <n> | --top <n>]
          [--scope single|mesh] [--param <key>=<val>]...
          [--dry-run]

  %s mesh create --name <mesh-name>
  %s mesh discover

  %s export --format json

  %s session open --name <name>

  %s discover
  %s --help
)",
        prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog);
}

static std::string get_data_dir() {
    const char* env = std::getenv("SMO_DATA_DIR");
    if (env) return env;
    return "/var/lib/smo";
}

static std::string get_etc_dir() {
    const char* env = std::getenv("SMO_ETC_DIR");
    if (env) return env;
    return "/etc/smo";
}

// ── node init ────────────────────────────────────────────────────
static int cmd_node_init(const std::vector<std::string>& args) {
    std::string name;
    bool force = false;

    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--name") { if (i + 1 < args.size()) name = args[++i]; }
        if (args[i] == "--force") force = true;
    }

    if (name.empty()) {
        std::fprintf(stderr, "Error: --name <display-name> is required\n");
        return 1;
    }

    auto data_dir = get_data_dir();
    fs::create_directories(data_dir);

    auto id_path = data_dir + "/identity.json";
    if (!force && fs::exists(id_path)) {
        std::fprintf(stderr, "Error: identity already exists at %s (use --force to overwrite)\n",
                     id_path.c_str());
        return 1;
    }

    // Create identity JSON
    std::string json =
        "{\n"
        "  \"display_name\": \"" + name + "\",\n"
        "  \"created_at\": \"" + std::to_string(std::time(nullptr)) + "\",\n"
        "  \"state\": \"KEY_GENERATED\"\n"
        "}\n";

    std::ofstream of(id_path);
    if (!of) {
        std::fprintf(stderr, "Error: cannot write %s\n", id_path.c_str());
        return 1;
    }
    of << json;
    of.close();

    // Also write node.json
    auto node_path = data_dir + "/node.json";
    std::ofstream nf(node_path);
    nf << "{\"name\":\"" << name << "\",\"role\":\"Member\",\"port\":7777}\n";
    nf.close();

    std::printf("Node initialized.\n");
    std::printf("  Display name: %s\n", name.c_str());
    std::printf("  Identity:     %s\n", id_path.c_str());
    std::printf("  State:        KEY_GENERATED\n");
    return 0;
}

// ── node info ────────────────────────────────────────────────────
static int cmd_node_info() {
    auto data_dir = get_data_dir();
    auto id_path = data_dir + "/identity.json";

    if (!fs::exists(id_path)) {
        std::fprintf(stderr, "Error: node not initialized (run 'smo node init' first)\n");
        return 1;
    }

    std::ifstream f(id_path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    std::printf("%s", content.c_str());
    return 0;
}

// ── node import ──────────────────────────────────────────────────
static int cmd_node_import(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::fprintf(stderr, "Usage: smo node import <cert-file>\n");
        return 1;
    }
    auto cert_path = args[2];
    auto data_dir = get_data_dir();

    if (!fs::exists(cert_path)) {
        std::fprintf(stderr, "Error: file not found: %s\n", cert_path.c_str());
        return 1;
    }

    // Copy certificate to data dir
    auto dest = data_dir + "/certificate.json";
    fs::copy_file(cert_path, dest, fs::copy_options::overwrite_existing);

    std::printf("Certificate imported: %s → %s\n", cert_path.c_str(), dest.c_str());

    // Update identity state
    auto id_path = data_dir + "/identity.json";
    if (fs::exists(id_path)) {
        std::ofstream f(id_path);
        f << "{\"state\":\"JOINED\",\"imported_at\":" << std::time(nullptr) << "}\n";
    }
    return 0;
}

// ── node connect ─────────────────────────────────────────────────
static int cmd_node_connect(const std::vector<std::string>& args) {
    std::string address;
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--address") { if (i + 1 < args.size()) address = args[++i]; }
    }
    if (address.empty()) {
        std::fprintf(stderr, "Usage: smo node connect --address <host:port>\n");
        return 1;
    }
    std::printf("Connecting to %s...\n", address.c_str());
    std::printf("(Connection via TCP transport — placeholder)\n");
    return 0;
}

// ── node rename ──────────────────────────────────────────────────
static int cmd_node_rename(const std::vector<std::string>& args) {
    std::string new_name;
    bool dry_run = false;
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--name") { if (i + 1 < args.size()) new_name = args[++i]; }
        if (args[i] == "--dry-run") dry_run = true;
    }
    if (new_name.empty()) {
        std::fprintf(stderr, "Usage: smo node rename --name <new-name>\n");
        return 1;
    }
    std::printf("Rename requested: %s\n", new_name.c_str());
    if (dry_run) std::printf("  (dry-run — no changes made)\n");
    return 0;
}

// ── exec ─────────────────────────────────────────────────────────
static int cmd_exec(const std::vector<std::string>& args) {
    std::string opcode, name, node_id, role, where, os, arch, version, trust_range, mesh, cap, scope;
    std::vector<std::string> tags, params;
    bool nearest = false, dry_run = false;
    int random_n = 0, top_n = 0;

    for (size_t i = 1; i < args.size(); ++i) {
        auto& a = args[i];
        // Positional opcode (first non-flag argument)
        if (a[0] != '-' && opcode.empty() && a != "exec") {
            opcode = a;
            continue;
        }
        if (a == "--opcode" || a == "--op") {
            if (i + 1 < args.size()) opcode = args[++i];
        } else if (a == "--name")       { if (i + 1 < args.size()) name = args[++i]; }
        else if (a == "--id")           { if (i + 1 < args.size()) node_id = args[++i]; }
        else if (a == "--role")         { if (i + 1 < args.size()) role = args[++i]; }
        else if (a == "--tag")          { if (i + 1 < args.size()) tags.push_back(args[++i]); }
        else if (a == "--where")        { if (i + 1 < args.size()) where = args[++i]; }
        else if (a == "--os" || a == "--platform") { if (i + 1 < args.size()) os = args[++i]; }
        else if (a == "--arch")         { if (i + 1 < args.size()) arch = args[++i]; }
        else if (a == "--version")      { if (i + 1 < args.size()) version = args[++i]; }
        else if (a == "--trust")        { if (i + 1 < args.size()) trust_range = args[++i]; }
        else if (a == "--mesh")         { if (i + 1 < args.size()) mesh = args[++i]; }
        else if (a == "--cap")          { if (i + 1 < args.size()) cap = args[++i]; }
        else if (a == "--scope")        { if (i + 1 < args.size()) scope = args[++i]; }
        else if (a == "--param")        { if (i + 1 < args.size()) params.push_back(args[++i]); }
        else if (a == "--nearest")      { nearest = true; }
        else if (a == "--random")       { if (i + 1 < args.size()) random_n = std::stoi(args[++i]); }
        else if (a == "--top")          { if (i + 1 < args.size()) top_n = std::stoi(args[++i]); }
        else if (a == "--dry-run")      { dry_run = true; }
    }

    // Support positional opcode (first non-flag arg)
    if (opcode.empty() && !args.empty() && args[0] != "exec") {
        opcode = args[0];
    }
    if (opcode.empty()) {
        std::fprintf(stderr, "Error: --opcode is required\n");
        return 1;
    }

    // Built-in contracts
    if (opcode == "ping") {
        std::printf("{\"status\":\"pong\",\"timestamp\":%ld,\"node\":\"%s\"}\n",
                    std::time(nullptr), name.empty() ? "localhost" : name.c_str());
        return 0;
    }

    if (opcode == "whoami") {
        auto data_dir = get_data_dir();
        auto id_path = data_dir + "/identity.json";
        if (fs::exists(id_path)) {
            std::ifstream f(id_path);
            std::printf("%s", std::string((std::istreambuf_iterator<char>(f)),
                                           std::istreambuf_iterator<char>()).c_str());
        } else {
            std::printf("{\"node_id\":\"unknown\"}\n");
        }
        return 0;
    }

    if (dry_run) {
        std::printf("Intent: %s\n", opcode.c_str());
        if (!name.empty())      std::printf("  -> name:       %s\n", name.c_str());
        if (!node_id.empty())   std::printf("  -> id:         %s\n", node_id.c_str());
        if (!role.empty())      std::printf("  -> role:       %s\n", role.c_str());
        if (!tags.empty())      { std::printf("  -> tags:       "); for (auto& t : tags) std::printf("%s ", t.c_str()); std::printf("\n"); }
        if (!where.empty())     std::printf("  -> where:      %s\n", where.c_str());
        if (!os.empty())        std::printf("  -> os:         %s\n", os.c_str());
        if (!arch.empty())      std::printf("  -> arch:       %s\n", arch.c_str());
        if (!version.empty())   std::printf("  -> version:    %s\n", version.c_str());
        if (!trust_range.empty()) std::printf("  -> trust:      %s\n", trust_range.c_str());
        if (!mesh.empty())      std::printf("  -> mesh:       %s\n", mesh.c_str());
        if (!cap.empty())       std::printf("  -> cap:        %s\n", cap.c_str());
        if (!scope.empty())     std::printf("  -> scope:      %s\n", scope.c_str());
        if (nearest)            std::printf("  -> mode:       nearest\n");
        if (random_n > 0)       std::printf("  -> mode:       random %d\n", random_n);
        if (top_n > 0)          std::printf("  -> mode:       top %d\n", top_n);
        std::printf("  (dry-run)\n");
        return 0;
    }

    std::printf("Executing %s...\n", opcode.c_str());
    std::printf("  (Execution via daemon — not yet implemented)\n");
    return 0;
}

// ── discover ─────────────────────────────────────────────────────
static int cmd_discover(const std::vector<std::string>& args) {
    bool full = false;
    int wait_sec = 0;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--full") full = true;
        if (args[i] == "--wait" && i + 1 < args.size()) wait_sec = std::stoi(args[++i]);
    }

    if (wait_sec > 0) {
        std::printf("Waiting %d seconds for peers...\n", wait_sec);
        // In real mode, would poll discovery engine
    }

    std::printf("%-8s %-14s %-12s %-20s %s\n",
                "ID", "NAME", "ROLE", "TAGS", "STATUS");
    std::printf("%-8s %-14s %-12s %-20s %s\n",
                "--------", "--------------", "------------", "--------------------", "-------");

    // Try to read peer table from data dir
    auto data_dir = get_data_dir();
    auto peers_path = data_dir + "/peers.json";
    if (fs::exists(peers_path)) {
        std::ifstream f(peers_path);
        std::string line;
        while (std::getline(f, line)) {
            std::printf("%s\n", line.c_str());
        }
    } else {
        // Show local node
        auto id_path = data_dir + "/identity.json";
        if (fs::exists(id_path)) {
            std::ifstream f(id_path);
            std::string content((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
            // Extract name from JSON (naive)
            auto pos = content.find("display_name");
            std::string name = "unknown";
            if (pos != std::string::npos) {
                auto start = content.find('"', pos + 14);
                auto end = content.find('"', start + 1);
                if (start != std::string::npos && end != std::string::npos)
                    name = content.substr(start + 1, end - start - 1);
            }
            std::printf("%-8s %-14s %-12s %-20s %s\n",
                        "local", name.c_str(), "Member", "-", "Online");
        }
        if (full) {
            std::printf("(No peer table yet — run 'smo node connect' to join mesh)\n");
        }
    }
    return 0;
}

// ── mesh ─────────────────────────────────────────────────────────
static int cmd_mesh(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::fprintf(stderr, "Usage: smo mesh create|discover\n");
        return 1;
    }
    auto sub = args[1];
    if (sub == "create") {
        std::string mesh_name;
        for (size_t i = 2; i < args.size(); ++i) {
            if (args[i] == "--name") { if (i + 1 < args.size()) mesh_name = args[++i]; }
        }
        if (mesh_name.empty()) {
            std::fprintf(stderr, "Usage: smo mesh create --name <mesh-name>\n");
            return 1;
        }
        auto etc_dir = get_etc_dir();
        fs::create_directories(etc_dir);
        auto mesh_path = etc_dir + "/mesh.json";
        std::ofstream f(mesh_path);
        f << "{\"name\":\"" << mesh_name << "\",\"created_at\":" << std::time(nullptr) << "}\n";
        f.close();
        std::printf("Mesh created: %s (%s)\n", mesh_name.c_str(), mesh_path.c_str());
        return 0;
    }
    if (sub == "discover") {
        // Forward to discover command
        std::vector<std::string> discover_args(args.begin() + 2, args.end());
        return cmd_discover(discover_args);
    }
    std::fprintf(stderr, "Unknown subcommand: mesh %s\n", sub.c_str());
    return 1;
}

// ── export ───────────────────────────────────────────────────────
static int cmd_export(const std::vector<std::string>& args) {
    std::string format = "json";
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--format" && i + 1 < args.size()) format = args[++i];
    }

    auto data_dir = get_data_dir();
    auto id_path = data_dir + "/identity.json";
    if (!fs::exists(id_path)) {
        std::fprintf(stderr, "Error: node not initialized\n");
        return 1;
    }

    // Build enrollment request JSON
    std::string enroll_req =
        "{\n"
        "  \"version\": 1,\n"
        "  \"mesh_id\": \"SOC-Production\",\n"
        "  \"display_name\": \"soc-hn-01\",\n"
        "  \"platform\": \"linux\",\n"
        "  \"version\": \"3.0.0\",\n"
        "  \"public_key\": \"\",\n"
        "  \"timestamp\": \"" + std::to_string(std::time(nullptr)) + "\",\n"
        "  \"nonce\": \"0000000000000000\"\n"
        "}\n";

    std::printf("%s", enroll_req.c_str());
    return 0;
}

// ── session ──────────────────────────────────────────────────────
static int cmd_session(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::fprintf(stderr, "Usage: smo session open|close|list\n");
        return 1;
    }
    auto sub = args[1];
    if (sub == "open") {
        std::string name;
        for (size_t i = 2; i < args.size(); ++i) {
            if (args[i] == "--name") { if (i + 1 < args.size()) name = args[++i]; }
        }
        if (name.empty()) {
            std::fprintf(stderr, "Usage: smo session open --name <node-name>\n");
            return 1;
        }
        std::printf("Session opened with %s\n", name.c_str());
        std::printf("  SessionID: demo-0001\n");
        return 0;
    }
    std::fprintf(stderr, "Unknown subcommand: session %s\n", sub.c_str());
    return 1;
}

// ===========================================================================
// Main
// ===========================================================================

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::vector<std::string> args(argv + 1, argv + argc);

    for (auto& a : args) {
        if (a == "--help" || a == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    auto cmd = args[0];

    if (cmd == "node") {
        if (args.size() < 2) {
            std::fprintf(stderr, "Usage: smo node init|info|rename|join|leave|connect|import\n");
            return 1;
        }
        auto sub = args[1];
        if (sub == "init") return cmd_node_init(args);
        if (sub == "info") return cmd_node_info();
        if (sub == "import") return cmd_node_import(args);
        if (sub == "connect") return cmd_node_connect(args);
        if (sub == "rename") return cmd_node_rename(args);
        std::fprintf(stderr, "Unknown subcommand: node %s\n", sub.c_str());
        return 1;
    }

    if (cmd == "exec") return cmd_exec(args);
    if (cmd == "mesh") return cmd_mesh(args);
    if (cmd == "export") return cmd_export(args);
    if (cmd == "session") return cmd_session(args);
    if (cmd == "discover") return cmd_discover(args);

    std::fprintf(stderr, "Unknown command: %s\n", cmd.c_str());
    print_usage(argv[0]);
    return 1;
}
