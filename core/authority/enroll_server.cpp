#include "enroll_server.hpp"
#include "core/enroll/join_token.hpp"
#include "core/errors/error.hpp"
#include "core/mesh/mesh_manager.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

namespace smo::authority {

// ── Minimal HTTP helpers ──────────────────────────────────────────────

namespace {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
};

struct HttpResponse {
    int status_code = 200;
    std::string status_text = "OK";
    std::string content_type = "application/json";
    std::string body;

    std::string to_string() const {
        std::ostringstream resp;
        resp << "HTTP/1.1 " << status_code << " " << status_text << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n"
             << "\r\n"
             << body;
        return resp.str();
    }
};

static bool recv_all(int fd, std::string& buf, size_t max_len) {
    char tmp[4096];
    while (buf.size() < max_len) {
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), MSG_DONTWAIT);
        if (n > 0) {
            buf.append(tmp, static_cast<size_t>(n));
        } else if (n == 0) {
            return true;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        } else {
            return false;
        }
    }
    return true;
}

static bool parse_http_request(const std::string& raw, HttpRequest& req) {
    // Parse request line: "METHOD /path HTTP/1.1\r\n"
    auto line_end = raw.find("\r\n");
    if (line_end == std::string::npos) return false;

    auto first_space = raw.find(' ');
    if (first_space == std::string::npos || first_space >= line_end) return false;
    auto second_space = raw.find(' ', first_space + 1);
    if (second_space == std::string::npos || second_space >= line_end) return false;

    req.method = raw.substr(0, first_space);
    req.path = raw.substr(first_space + 1, second_space - first_space - 1);

    // Find Content-Length header
    size_t content_length = 0;
    size_t header_start = line_end + 2;
    size_t headers_end = raw.find("\r\n\r\n", header_start);
    if (headers_end == std::string::npos) return false;

    std::string headers_section = raw.substr(header_start, headers_end - header_start);
    size_t cl_pos = headers_section.find("Content-Length:");
    if (cl_pos != std::string::npos) {
        cl_pos += 16; // skip "Content-Length:"
        while (cl_pos < headers_section.size() && headers_section[cl_pos] == ' ') ++cl_pos;
        size_t cl_end = cl_pos;
        while (cl_end < headers_section.size() && headers_section[cl_end] >= '0' && headers_section[cl_end] <= '9') ++cl_end;
        content_length = static_cast<size_t>(std::stoll(headers_section.substr(cl_pos, cl_end - cl_pos)));
    }

    // Read body
    size_t body_start = headers_end + 4;
    if (body_start < raw.size()) {
        req.body = raw.substr(body_start);
    }

    // If body not fully received, return false (caller must retry)
    if (req.body.size() < content_length) {
        return false;
    }

    return true;
}

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

static std::string json_response(const std::string& status, int code,
                                  const std::string& message,
                                  const std::string& extra_fields = "") {
    std::string json = R"({"status":")" + json_escape(status) + R"(")";
    if (code != 0) {
        json += R"(,"code":)" + std::to_string(code);
    }
    if (!message.empty()) {
        json += R"(,"message":")" + json_escape(message) + R"(")";
    }
    if (!extra_fields.empty()) {
        if (extra_fields[0] == ',') json += extra_fields;
        else json += "," + extra_fields;
    }
    json += "}";
    return json;
}

} // anonymous namespace

// ── EnrollServer::Impl ───────────────────────────────────────────────

class EnrollServer::Impl {
public:
    uint16_t port_ = 0;
    int server_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread thread_;

    MeshAuthority* authority_ = nullptr;
    Bytes hmac_secret_;
    const HashImpl* hash_ = nullptr;

    Result<void> start(uint16_t port,
                       MeshAuthority& authority,
                       const std::string& hmac_secret_hex,
                       const HashImpl& hash) {
        port_ = port;
        authority_ = &authority;
        hash_ = &hash;

        // Decode HMAC secret from hex
        hmac_secret_ = Bytes(hmac_secret_hex.size() / 2);
        for (size_t i = 0; i < hmac_secret_hex.size(); i += 2) {
            char buf[3] = {hmac_secret_hex[i], hmac_secret_hex[i+1], 0};
            hmac_secret_[i/2] = static_cast<uint8_t>(std::strtoul(buf, nullptr, 16));
        }

        // Create socket
        server_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (server_fd_ < 0) {
            return SMO_ERR_STORAGE(900, Error, RetrySafe, None,
                                    "Failed to create enroll server socket");
        }

        int opt = 1;
        ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (::bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            ::close(server_fd_);
            server_fd_ = -1;
            return SMO_ERR_STORAGE(224, Error, RetrySafe, None,
                                    "Failed to bind enroll server to port " + std::to_string(port));
        }

        if (::listen(server_fd_, 5) < 0) {
            ::close(server_fd_);
            server_fd_ = -1;
            return SMO_ERR_STORAGE(900, Error, RetrySafe, None,
                                    "Failed to listen on enroll server socket");
        }

        running_ = true;
        thread_ = std::thread([this]() { run(); });

        return {};
    }

    void stop() {
        running_ = false;
        if (server_fd_ >= 0) {
            ::shutdown(server_fd_, SHUT_RDWR);
            ::close(server_fd_);
            server_fd_ = -1;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    void run() {
        std::vector<struct pollfd> fds;
        fds.push_back({server_fd_, POLLIN, 0});

        while (running_) {
            int ret = ::poll(fds.data(), fds.size(), 100);
            if (ret < 0) {
                if (errno == EINTR) continue;
                break;
            }

            if (fds[0].revents & POLLIN) {
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                int client_fd = ::accept4(server_fd_, (struct sockaddr*)&client_addr,
                                          &addr_len, SOCK_NONBLOCK);
                if (client_fd >= 0) {
                    handle_client(client_fd);
                }
            }
        }
    }

    void handle_client(int client_fd) {
        std::string raw;
        if (!recv_all(client_fd, raw, 65536)) {
            ::close(client_fd);
            return;
        }

        HttpRequest req;
        HttpResponse resp;

        if (!parse_http_request(raw, req) || raw.find("\r\n\r\n") == std::string::npos) {
            // Wait for full request body by polling again
            // For simplicity, read available data and parse what we have
            struct pollfd pfd = {client_fd, POLLIN, 0};
            while (::poll(&pfd, 1, 5000) > 0 && (pfd.revents & POLLIN)) {
                if (!recv_all(client_fd, raw, 65536)) break;
                if (parse_http_request(raw, req)) break;
                pfd.revents = 0;
            }

            if (!parse_http_request(raw, req)) {
                resp.status_code = 400;
                resp.status_text = "Bad Request";
                resp.body = json_response("error", 400, "Invalid HTTP request");
                std::string out = resp.to_string();
                ::send(client_fd, out.data(), out.size(), 0);
                ::close(client_fd);
                return;
            }
        }

        // Route
        if (req.method == "POST" && req.path == "/enroll") {
            handle_enroll(req, resp);
        } else {
            resp.status_code = 404;
            resp.status_text = "Not Found";
            resp.body = json_response("error", 404, "Not found: " + req.method + " " + req.path);
        }

        std::string out = resp.to_string();
        ::send(client_fd, out.data(), out.size(), 0);
        ::close(client_fd);
    }

    void handle_enroll(const HttpRequest& req, HttpResponse& resp) {
        // Parse JSON body
        // Simple JSON parsing: find "join_token" and "csr_hex"
        auto find_json_field = [](const std::string& json, const std::string& key) -> std::string {
            auto pos = json.find("\"" + key + "\"");
            if (pos == std::string::npos) return {};
            auto colon = json.find(':', pos);
            if (colon == std::string::npos) return {};
            auto start = json.find_first_of("\"", colon);
            if (start == std::string::npos) return {};
            if (json[start] == '"') {
                auto end = json.find('"', start + 1);
                if (end == std::string::npos) return {};
                return json.substr(start + 1, end - start - 1);
            }
            return {};
        };

        std::string token_str = find_json_field(req.body, "join_token");
        std::string csr_hex = find_json_field(req.body, "csr_hex");

        if (token_str.empty() || csr_hex.empty()) {
            resp.status_code = 400;
            resp.status_text = "Bad Request";
            resp.body = json_response("error", 400, "Missing join_token or csr_hex");
            return;
        }

        // 1. Parse Join Token
        auto token_result = enroll::parse_token(token_str);
        if (!token_result) {
            resp.status_code = 400;
            resp.status_text = "Bad Request";
            resp.body = json_response("error", 212, "Invalid join token: " + token_result.error().message);
            return;
        }

        auto& token = token_result.value();

        // 2. Validate HMAC (v1 compat)
        auto validate_result = enroll::validate_token_v1(token, hmac_secret_, *hash_);
        if (!validate_result) {
            resp.status_code = 400;
            resp.status_text = "Bad Request";
            resp.body = json_response("error", 212, "Token validation failed: " + validate_result.error().message);
            return;
        }

        // 3. Decode CSR from hex
        Bytes csr_bytes(csr_hex.size() / 2);
        for (size_t i = 0; i < csr_hex.size(); i += 2) {
            char buf[3] = {csr_hex[i], csr_hex[i+1], 0};
            csr_bytes[i/2] = static_cast<uint8_t>(std::strtoul(buf, nullptr, 16));
        }

        // 4. Sign CSR
        auto cert_result = authority_->sign_csr(csr_bytes, token.mesh_id);
        if (!cert_result) {
            resp.status_code = 400;
            resp.status_text = "Bad Request";
            resp.body = json_response("error", 215, "CSR rejected: " + cert_result.error().message);
            return;
        }

        // 5. Serialize certificate
        auto cert_full = cert_result.value().serialize_full();
        std::string cert_hex = bytes_to_hex(cert_full);

        // Compute fingerprint
        auto fp_hash = hash_->hash(cert_result.value().serialize());
        std::string fp_hex;
        if (fp_hash) {
            fp_hex = bytes_to_hex(fp_hash.value());
        }

        // Compute node_id from public key
        auto node_id_result = node_id_from_public_key(
            cert_result.value().subject_pubkey, *hash_);
        std::string node_id_hex;
        if (node_id_result) {
            node_id_hex = node_id_result.value().to_string();
        }

        // 6. Return success
        resp.status_code = 200;
        resp.status_text = "OK";
        std::string extra = R"(,"certificate_hex":")" + cert_hex + R"(")";
        if (!fp_hex.empty()) {
            extra += R"(,"fingerprint":")" + fp_hex + R"(")";
        }
        if (!node_id_hex.empty()) {
            extra += R"(,"node_id":")" + node_id_hex + R"(")";
        }
        resp.body = json_response("ok", 0, "", extra);
    }
};

// ── EnrollServer ─────────────────────────────────────────────────────

EnrollServer::EnrollServer() : impl_(std::make_unique<Impl>()) {}
EnrollServer::~EnrollServer() { stop(); }

Result<void> EnrollServer::start(uint16_t port,
                                  MeshAuthority& authority,
                                  const std::string& hmac_secret_hex,
                                  const HashImpl& hash) {
    return impl_->start(port, authority, hmac_secret_hex, hash);
}

void EnrollServer::stop() { impl_->stop(); }
bool EnrollServer::is_running() const { return impl_->running_.load(); }

} // namespace smo::authority
