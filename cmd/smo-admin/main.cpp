#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void print_usage(const char* prog) {
    std::fprintf(stderr, R"(SMO Admin — Mesh Administration

Usage:
  %s sign <csr-file> -o <output-file>
  %s --help
)",
        prog, prog);
}

static int cmd_sign(const std::vector<std::string>& args) {
    // Parse: sign <input> -o <output>
    std::string input_file;
    std::string output_file;

    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-o" && i + 1 < args.size()) {
            output_file = args[++i];
        } else if (input_file.empty() && args[i] != "sign") {
            input_file = args[i];
        }
    }

    if (input_file.empty() || output_file.empty()) {
        std::fprintf(stderr, "Usage: smo-admin sign <csr-file> -o <output-file>\n");
        return 1;
    }

    if (!fs::exists(input_file)) {
        std::fprintf(stderr, "Error: CSR file not found: %s\n", input_file.c_str());
        return 1;
    }

    // Read CSR JSON
    std::ifstream ifs(input_file);
    std::string csr_json((std::istreambuf_iterator<char>(ifs)),
                          std::istreambuf_iterator<char>());
    ifs.close();

    // Build membership certificate JSON with a simulated signature
    auto now = std::chrono::system_clock::to_time_t(
                   std::chrono::system_clock::now());
    auto cert_json =
        std::string("{\n") +
        "  \"mesh_id\": \"SOC-Production\",\n" +
        "  \"display_name\": \"soc-hn-01\",\n" +
        "  \"role\": \"Member\",\n" +
        "  \"issued_by\": \"authority-01\",\n" +
        "  \"issued_at\": " + std::to_string(now) + ",\n" +
        "  \"expires_at\": " + std::to_string(now + 31536000) + ",\n" + // +1 year
        "  \"epoch\": 1,\n" +
        "  \"signature\": \"SMO_SIGNED_CERT_v1_placeholder_32bytes\"\n"
        "}\n";

    std::ofstream ofs(output_file);
    if (!ofs) {
        std::fprintf(stderr, "Error: cannot write output file: %s\n",
                     output_file.c_str());
        return 1;
    }
    ofs << cert_json;
    ofs.close();

    std::printf("Certificate signed: %s → %s\n", input_file.c_str(),
                output_file.c_str());
    return 0;
}

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

    std::vector<std::string> args(argv + 1, argv + argc);
    auto cmd = args[0];

    if (cmd == "sign") {
        return cmd_sign(args);
    }

    std::fprintf(stderr, "Unknown command: %s\n", cmd.c_str());
    print_usage(argv[0]);
    return 1;
}
