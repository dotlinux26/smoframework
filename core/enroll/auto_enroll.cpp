#include "auto_enroll.hpp"

#include "core/crypto/registry.hpp"
#include "core/enroll/join_token.hpp"
#include "core/errors/error.hpp"
#include "core/types.hpp"
#include "core/identity/identity.hpp"
#include "core/certificate/certificate.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace smo {
namespace enroll {

// ── Hex helper (bytes_to_hex exists in types.hpp, but hex_to_bytes doesn't) ─

static Bytes hex_to_bytes(const std::string& hex) {
    Bytes out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        char buf[3] = {hex[i], hex[i+1], 0};
        out.push_back(static_cast<uint8_t>(std::strtoul(buf, nullptr, 16)));
    }
    return out;
}

// ── Minimal HTTP client ─────────────────────────────────────────────

static Result<std::string> http_post(const std::string& host, uint16_t port,
                                      const std::string& path,
                                      const std::string& content_type,
                                      const std::string& body,
                                      int timeout_sec = 10) {
    struct addrinfo hints, *res = nullptr;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port);
    int gai_err = ::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
    if (gai_err != 0 || !res) {
        return SMO_ERR_CERT(214, Error, RetrySafe, None,
                            "DNS resolution failed: " + host);
    }

    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        ::freeaddrinfo(res);
        return SMO_ERR_CERT(214, Error, RetrySafe, None, "Failed to create socket");
    }

    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (::connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        ::close(fd);
        ::freeaddrinfo(res);
        return SMO_ERR_CERT(214, Error, RetrySafe, None,
                            "Connection refused: " + host + ":" + port_str);
    }
    ::freeaddrinfo(res);

    std::ostringstream req;
    req << "POST " << path << " HTTP/1.1\r\n"
        << "Host: " << host << ":" << port << "\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;

    std::string request_str = req.str();
    size_t total_sent = 0;
    while (total_sent < request_str.size()) {
        ssize_t n = ::send(fd, request_str.data() + total_sent,
                           request_str.size() - total_sent, 0);
        if (n <= 0) {
            ::close(fd);
            return SMO_ERR_CERT(214, Error, RetrySafe, None,
                                "Failed to send HTTP request");
        }
        total_sent += static_cast<size_t>(n);
    }

    std::string response;
    char buf[4096];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) {
        response.append(buf, static_cast<size_t>(n));
    }
    ::close(fd);

    if (response.empty()) {
        return SMO_ERR_CERT(214, Error, RetrySafe, None, "Empty response from server");
    }

    auto line_end = response.find("\r\n");
    if (line_end == std::string::npos) {
        return SMO_ERR_CERT(214, Error, RetrySafe, None, "Invalid HTTP response");
    }

    std::string status_line = response.substr(0, line_end);
    auto space1 = status_line.find(' ');
    auto space2 = status_line.find(' ', space1 + 1);
    if (space1 == std::string::npos || space2 == std::string::npos) {
        return SMO_ERR_CERT(214, Error, RetrySafe, None, "Invalid HTTP status line");
    }

    int status_code = std::stoi(status_line.substr(space1 + 1, space2 - space1 - 1));

    auto body_start = response.find("\r\n\r\n");
    if (body_start == std::string::npos) {
        return SMO_ERR_CERT(214, Error, RetrySafe, None, "No HTTP body found");
    }
    body_start += 4;

    std::string response_body = response.substr(body_start);

    if (status_code != 200) {
        return SMO_ERR_CERT(215, Error, RetrySafe, None,
                            "Server returned " + std::to_string(status_code) + ": " + response_body);
    }

    return response_body;
}

// ── JSON field extraction (simple, no dependency) ────────────────────

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

// ── Directory helpers ────────────────────────────────────────────────

static Result<void> ensure_dir(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec) {
        return SMO_ERR_CERT(200, Error, RetrySafe, None,
                            "Failed to create directory: " + ec.message());
    }
    return {};
}

// ── Main join command ────────────────────────────────────────────────

Result<void> run_join_command(const std::string& token_str,
                               const std::string& data_dir,
                               const std::string& node_name,
                               uint16_t port,
                               const std::string& mesh_dir) {
    (void)mesh_dir; // unused when using HTTP enrollment

    // Parse token
    auto token_result = enroll::parse_token(token_str);
    if (!token_result) return token_result.error();

    const auto& token = token_result.value();

    // Validate expiry
    if (token.expiry_unix_sec > 0) {
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (now > token.expiry_unix_sec) {
            return SMO_ERR_CERT(213, Error, NoRetry, None, "Join token expired");
        }
    }

    // Setup data directory
    std::string actual_data_dir = data_dir.empty() ? "/tmp/smo-node-" + std::to_string(getpid()) : data_dir;
    auto dir_result = ensure_dir(actual_data_dir);
    if (!dir_result) return dir_result.error();

    // Get crypto once and reuse
    auto& reg = CryptoRegistry::instance();
    auto crypto_result = reg.get_suite(token.cipher_suite_id);
    if (!crypto_result) return crypto_result.error();
    const auto* crypto = crypto_result.value();
    auto rng = crypto->default_rng();

    // 1. Initialize identity (or load existing)
    std::string identity_path = actual_data_dir + "/identity.json";
    bool identity_exists = std::filesystem::exists(identity_path);

    std::shared_ptr<Identity> identity;
    if (identity_exists) {
        auto id_result = Identity::load_from_file(identity_path, *crypto);
        if (!id_result) return id_result.error();
        identity = std::make_shared<Identity>(std::move(id_result.value()));
        std::printf("Loaded existing identity: %s\n", identity->node_id().to_string().c_str());
    } else {
        auto id_result = Identity::create(*crypto, rng);
        if (!id_result) return id_result.error();
        identity = std::make_shared<Identity>(std::move(id_result.value()));

        auto save_result = identity->save_to_file(identity_path);
        if (!save_result) return save_result.error();
        std::printf("Identity created: %s\n", identity->node_id().to_string().c_str());
    }

    // 2. Build CSR
    CertificateSigningRequest csr;
    csr.new_public_key = Bytes(identity->public_key().begin(), identity->public_key().end());
    csr.mesh_id = hex_to_bytes(token.mesh_id);
    csr.display_name = node_name.empty() ? identity->node_id().to_string() : node_name;
    csr.platform = "linux";
    csr.version = "0.1.0";
    csr.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    csr.old_cert_hash = Bytes(32, 0); // no old cert for new enrollment

    // Sign CSR with node's secret key
    auto csr_body = csr.serialize_body();
    auto sig_result = crypto->signer.sign(csr_body, identity->secret_key(), rng);
    if (!sig_result) return sig_result.error();
    csr.signature = std::move(sig_result.value());

    // Serialize CSR to hex (body + signature)
    Bytes csr_bytes = csr.serialize();
    std::string csr_hex = bytes_to_hex(csr_bytes);

    // 3. Try to enroll via bootstrap endpoints
    std::string last_error;
    bool success = false;
    std::string cert_hex;
    std::string fingerprint;
    std::string node_id_hex;

    std::printf("Attempting to join mesh via bootstrap endpoints...\n");

    for (const auto& endpoint : token.bootstrap_endpoints) {
        size_t colon = endpoint.rfind(':');
        if (colon == std::string::npos) continue;
        std::string host = endpoint.substr(0, colon);
        uint16_t ep_port = static_cast<uint16_t>(std::stoi(endpoint.substr(colon + 1)));

        std::printf("  Trying endpoint: %s:%u ... ", host.c_str(), ep_port);

        // Build JSON body
        std::string json_body = R"({"join_token":")" + token_str + R"(","csr_hex":")" + csr_hex + R"("})";

        auto response = http_post(host, ep_port, "/enroll", "application/json", json_body);
        if (!response) {
            std::printf("FAIL (%s)\n", response.error().message.c_str());
            last_error = response.error().message;
            continue;
        }

        std::printf("OK\n");

        // Parse response
        std::string resp_status = json_read_string(response.value(), "status");
        if (resp_status != "ok") {
            std::string err_msg = json_read_string(response.value(), "message");
            last_error = "Server rejected enrollment: " + (err_msg.empty() ? resp_status : err_msg);
            std::printf("  Server error: %s\n", last_error.c_str());
            continue;
        }

        cert_hex = json_read_string(response.value(), "certificate_hex");
        fingerprint = json_read_string(response.value(), "fingerprint");
        node_id_hex = json_read_string(response.value(), "node_id");
        success = true;
        break;
    }

    if (!success) {
        return SMO_ERR_CERT(214, Error, RetrySafe, None,
                            "All bootstrap endpoints unreachable. Last error: " + last_error);
    }

    // 4. Import certificate
    if (cert_hex.empty()) {
        return SMO_ERR_CERT(215, Error, NoRetry, None, "No certificate in response");
    }

    Bytes cert_bytes = hex_to_bytes(cert_hex);

    auto cert_result = Certificate::deserialize(cert_bytes);
    if (!cert_result) {
        return SMO_ERR_CERT(216, Error, NoRetry, None,
                            "Failed to parse certificate: " + cert_result.error().message);
    }

    // Save certificate
    std::string cert_path = actual_data_dir + "/cert.smoc";
    {
        std::ofstream f(cert_path, std::ios::binary);
        if (!f) {
            return SMO_ERR_STORAGE(900, Error, RetrySafe, None,
                                   "Failed to write certificate to " + cert_path);
        }
        f.write(reinterpret_cast<const char*>(cert_bytes.data()), cert_bytes.size());
    }

    std::printf("\n✓ Successfully enrolled!\n");
    std::printf("  Node ID:      %s\n", node_id_hex.c_str());
    std::printf("  Mesh:         %s\n", token.mesh_id.c_str());
    std::printf("  Role:         %s\n", token.admission.role.c_str());
    std::printf("  Profile:      %s\n", token.admission.profile.empty() ? "default" : token.admission.profile.c_str());
    std::printf("  Certificate:  %s\n", cert_path.c_str());
    std::printf("  Fingerprint:  %s\n", fingerprint.c_str());

    // Update identity state to Enrolled
    (void)identity->transition_to(IdentityState::Enrolled);
    (void)identity->save_to_file(identity_path);

    // 5. Save bootstrap endpoints for daemon
    std::string config_path = actual_data_dir + "/config.json";
    {
        std::ofstream f(config_path);
        if (f) {
            f << "{\n";
            f << "  \"mesh_id\": \"" << token.mesh_id << "\",\n";
            f << "  \"role\": \"" << token.admission.role << "\",\n";
            f << "  \"profile\": \"" << (token.admission.profile.empty() ? "server" : token.admission.profile) << "\",\n";
            f << "  \"listen_port\": " << port << ",\n";
            f << "  \"bootstrap_endpoints\": [\n";
            for (size_t i = 0; i < token.bootstrap_endpoints.size(); ++i) {
                if (i > 0) f << ",\n";
                f << "    \"" << token.bootstrap_endpoints[i] << "\"";
            }
            f << "\n  ],\n";
            f << "  \"node_name\": \"" << (node_name.empty() ? identity->node_id().to_string() : node_name) << "\"\n";
            f << "}\n";
        }
    }

    std::printf("\nRun 'smo-node --daemon --data %s --port %d' to start the node.\n",
                actual_data_dir.c_str(), port);

    return {};
}

} // namespace enroll
} // namespace smo
