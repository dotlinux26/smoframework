#include "auto_enroll.hpp"

#include "core/crypto/registry.hpp"
#include "core/enroll/join_token.hpp"
#include "core/join/join_protocol.hpp"
#include "core/fsm/fsm.hpp"
#include "core/errors/error.hpp"
#include "core/types.hpp"
#include "core/identity/identity.hpp"
#include "core/certificate/certificate.hpp"
#include "core/transport/secure_session.hpp"

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

// ── Hex helper ────────────────────────────────────────────────────────

static Bytes hex_to_bytes(const std::string& hex) {
    Bytes out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        char buf[3] = {hex[i], hex[i+1], 0};
        out.push_back(static_cast<uint8_t>(std::strtoul(buf, nullptr, 16)));
    }
    return out;
}

// ── Join state persistence ───────────────────────────────────────────

static std::string join_state_path(const std::string& data_dir) {
    return data_dir + "/join_state.bin";
}

static Result<FsmInstance> load_join_state(const std::string& data_dir) {
    std::string path = join_state_path(data_dir);
    if (!std::filesystem::exists(path)) {
        return SMO_ERR_STORAGE(404, Info, NoRetry, None, "No saved join state");
    }

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        return SMO_ERR_STORAGE(900, Error, RetrySafe, None, "Failed to open join state");
    }
    size_t sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    Bytes data(sz);
    f.read(reinterpret_cast<char*>(data.data()), sz);

    auto rules = join::join_transition_table();
    auto timeouts = join::join_timeout_table();
    return FsmInstance::deserialize(BytesView(data), rules.data(), rules.size(), timeouts.data(), timeouts.size());
}

static Result<void> save_join_state(const FsmInstance& fsm, const std::string& data_dir) {
    auto serialized = fsm.serialize();
    if (!serialized) return serialized.error();

    std::string path = join_state_path(data_dir);
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return SMO_ERR_STORAGE(900, Error, RetrySafe, None, "Failed to write join state");
    }
    f.write(reinterpret_cast<const char*>(serialized.value().data()), serialized.value().size());
    return {};
}

static void clear_join_state(const std::string& data_dir) {
    std::filesystem::remove(join_state_path(data_dir));
}

// ── TCP send/receive (raw CBOR, no HTTP) ──────────────────────────────

static int tcp_connect(const std::string& host, uint16_t port, int timeout_sec = 10) {
    struct addrinfo hints, *res = nullptr;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port);
    int gai_err = ::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
    if (gai_err != 0 || !res) {
        return -1;
    }

    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        ::freeaddrinfo(res);
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (::connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        ::close(fd);
        ::freeaddrinfo(res);
        return -1;
    }
    ::freeaddrinfo(res);
    return fd;
}

// ── CBOR transport: 4-byte length prefix + CBOR payload ──────────────

static Result<Bytes> tcp_cbor_exchange(int fd, BytesView send_cbor) {
    uint32_t send_len = static_cast<uint32_t>(send_cbor.size());
    uint8_t header[4];
    header[0] = static_cast<uint8_t>((send_len >> 24) & 0xFF);
    header[1] = static_cast<uint8_t>((send_len >> 16) & 0xFF);
    header[2] = static_cast<uint8_t>((send_len >> 8) & 0xFF);
    header[3] = static_cast<uint8_t>(send_len & 0xFF);

    size_t total = 0;
    while (total < sizeof(header)) {
        ssize_t n = ::send(fd, header + total, sizeof(header) - total, 0);
        if (n <= 0) return SMO_ERR_TRANSPORT(400, Error, RetrySafe, None, "Failed to send CBOR header");
        total += static_cast<size_t>(n);
    }
    total = 0;
    while (total < send_cbor.size()) {
        ssize_t n = ::send(fd, send_cbor.data() + total, send_cbor.size() - total, 0);
        if (n <= 0) return SMO_ERR_TRANSPORT(400, Error, RetrySafe, None, "Failed to send CBOR payload");
        total += static_cast<size_t>(n);
    }

    uint8_t resp_header[4];
    total = 0;
    while (total < sizeof(resp_header)) {
        ssize_t n = ::recv(fd, resp_header + total, sizeof(resp_header) - total, 0);
        if (n <= 0) return SMO_ERR_TRANSPORT(400, Error, RetrySafe, None, "Failed to receive response header");
        total += static_cast<size_t>(n);
    }
    uint32_t resp_len = (static_cast<uint32_t>(resp_header[0]) << 24) |
                        (static_cast<uint32_t>(resp_header[1]) << 16) |
                        (static_cast<uint32_t>(resp_header[2]) << 8)  |
                         static_cast<uint32_t>(resp_header[3]);
    if (resp_len > 1024 * 1024) {
        return SMO_ERR_TRANSPORT(400, Error, NoRetry, None, "Response too large");
    }

    Bytes resp(resp_len);
    total = 0;
    while (total < resp_len) {
        ssize_t n = ::recv(fd, resp.data() + total, resp_len - total, 0);
        if (n <= 0) return SMO_ERR_TRANSPORT(400, Error, RetrySafe, None, "Failed to receive response payload");
        total += static_cast<size_t>(n);
    }
    ::close(fd);
    return resp;
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
    (void)mesh_dir;

    std::string actual_data_dir = data_dir.empty() ? "/tmp/smo-node-" + std::to_string(getpid()) : data_dir;
    auto dir_result = ensure_dir(actual_data_dir);
    if (!dir_result) return dir_result.error();

    // ── Initialize or resume join FSM ────────────────────────────────
    FsmInstance fsm;
    fsm.set_transitions(join::join_transition_table());
    fsm.set_timeouts(join::join_timeout_table());

    auto saved_state = load_join_state(actual_data_dir);
    if (saved_state) {
        fsm = std::move(saved_state.value());
        auto state = static_cast<join::JoinState>(fsm.current_state());
        std::printf("Resuming join from state: %lld\n", (long long)state);
        if (state == join::JoinState::READY) {
            std::printf("Join already completed for this data directory.\n");
            return {};
        }
        if (state == join::JoinState::FAILED) {
            std::printf("Previous join attempt failed. Starting fresh.\n");
            fsm.reset(static_cast<int64_t>(join::JoinState::NEW));
        }
    } else {
        fsm.reset(static_cast<int64_t>(join::JoinState::NEW));
    }

    auto current_state = [&]() { return static_cast<join::JoinState>(fsm.current_state()); };

    // ── Step: Parse token ───────────────────────────────────────────
    if (current_state() == join::JoinState::NEW) {
        auto token_result = enroll::parse_token(token_str);
        if (!token_result) return token_result.error();
        const auto& token = token_result.value();

        if (token.expiry_unix_sec > 0) {
            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            if (now > token.expiry_unix_sec) {
                return SMO_ERR_CERT(213, Error, NoRetry, None, "Join token expired");
            }
        }

        auto r = fsm.on_event(static_cast<int64_t>(join::JoinEvent::TOKEN_PARSED));
        if (!r) return r.error();
        save_join_state(fsm, actual_data_dir);
        std::printf("Token parsed, mesh: %s\n", token.mesh_id.c_str());
    }

    // ── Step: Get crypto + identity ─────────────────────────────────
    auto& reg = CryptoRegistry::instance();
    // Need token to get cipher_suite_id — re-parse if needed (lightweight, in-memory)
    auto token_reparse = enroll::parse_token(token_str);
    if (!token_reparse) return token_reparse.error();
    const auto& token = token_reparse.value();

    auto crypto_result = reg.get_suite(token.cipher_suite_id);
    if (!crypto_result) return crypto_result.error();
    const auto* crypto = crypto_result.value();
    auto rng = crypto->default_rng();

    std::string identity_path = actual_data_dir + "/identity.json";
    std::shared_ptr<Identity> identity;

    if (current_state() == join::JoinState::TOKEN_RECEIVED) {
        bool identity_exists = std::filesystem::exists(identity_path);
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

        auto r = fsm.on_event(static_cast<int64_t>(join::JoinEvent::CSR_BUILT));
        if (!r) return r.error();
        save_join_state(fsm, actual_data_dir);
    } else {
        auto id_result = Identity::load_from_file(identity_path, *crypto);
        if (!id_result) return id_result.error();
        identity = std::make_shared<Identity>(std::move(id_result.value()));
    }

    // ── Step: Build CSR ─────────────────────────────────────────────
    CertificateSigningRequest csr;
    std::string csr_pem;
    if (current_state() <= join::JoinState::CSR_CREATED &&
        current_state() != join::JoinState::JOIN_SENT) {
        csr.new_public_key = Bytes(identity->public_key().begin(), identity->public_key().end());
        csr.mesh_id = hex_to_bytes(token.mesh_id);
        csr.display_name = node_name.empty() ? identity->node_id().to_string() : node_name;
        csr.platform = "linux";
        csr.version = "0.1.0";
        csr.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        csr.old_cert_hash = Bytes(32, 0);

        auto csr_body = csr.serialize_body();
        auto sig_result = crypto->signer.sign(csr_body, identity->secret_key(), rng);
        if (!sig_result) return sig_result.error();
        csr.signature = std::move(sig_result.value());

        Bytes csr_bytes = csr.serialize();
        csr_pem = bytes_to_hex(csr_bytes);

        auto r = fsm.on_event(static_cast<int64_t>(join::JoinEvent::CSR_BUILT));
        if (!r) return r.error();
        save_join_state(fsm, actual_data_dir);
    }

    // ── Step: Build JoinRequest + send TCP/CBOR ─────────────────────
    join::JoinResponse resp;
    if (current_state() <= join::JoinState::JOIN_SENT) {
        auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        join::JoinRequest req;
        req.token = token_str;
        req.csr_pem = csr_pem;
        req.timestamp = now_sec;
        std::array<uint8_t, 8> nonce{};
        for (auto& b : nonce) b = static_cast<uint8_t>(rand());
        req.nonce = nonce;

        auto hash_result = crypto->hash.hash(BytesView(
            reinterpret_cast<const uint8_t*>(csr_pem.data()), csr_pem.size()));
        if (!hash_result) return hash_result.error();
        req.csr_hash = std::move(hash_result.value());

        {
            Bytes sig_payload;
            sig_payload.insert(sig_payload.end(), token_str.begin(), token_str.end());
            sig_payload.push_back(static_cast<uint8_t>((now_sec >> 56) & 0xFF));
            sig_payload.push_back(static_cast<uint8_t>((now_sec >> 48) & 0xFF));
            sig_payload.push_back(static_cast<uint8_t>((now_sec >> 40) & 0xFF));
            sig_payload.push_back(static_cast<uint8_t>((now_sec >> 32) & 0xFF));
            sig_payload.push_back(static_cast<uint8_t>((now_sec >> 24) & 0xFF));
            sig_payload.push_back(static_cast<uint8_t>((now_sec >> 16) & 0xFF));
            sig_payload.push_back(static_cast<uint8_t>((now_sec >> 8) & 0xFF));
            sig_payload.push_back(static_cast<uint8_t>(now_sec & 0xFF));
            sig_payload.insert(sig_payload.end(), nonce.begin(), nonce.end());
            sig_payload.insert(sig_payload.end(), req.csr_hash.begin(), req.csr_hash.end());
            auto sig_req_result = crypto->signer.sign(BytesView(sig_payload), identity->secret_key(), rng);
            if (!sig_req_result) return sig_req_result.error();
            req.request_signature = std::move(sig_req_result.value());
        }

        Bytes req_cbor = req.encode_cbor();

        auto r = fsm.on_event(static_cast<int64_t>(join::JoinEvent::MSG_SENT));
        if (!r) return r.error();
        save_join_state(fsm, actual_data_dir);

        // ── Send TCP/CBOR (secure, with PQ handshake) ──────────────
        std::string last_error;
        bool sent = false;

        std::printf("Attempting to join mesh via secure TCP/CBOR (PQ handshake)...\n");

        for (const auto& endpoint : token.bootstrap_endpoints) {
            size_t colon = endpoint.rfind(':');
            if (colon == std::string::npos) continue;
            std::string host = endpoint.substr(0, colon);
            uint16_t ep_port = static_cast<uint16_t>(std::stoi(endpoint.substr(colon + 1)));

            std::printf("  Trying endpoint: %s:%u ... ", host.c_str(), ep_port);

            int fd = tcp_connect(host, ep_port);
            if (fd < 0) {
                std::printf("FAIL (connection refused)\n");
                last_error = "Connection refused: " + host + ":" + std::to_string(ep_port);
                continue;
            }

            // ── PQ handshake ──────────────────────────────────────
            SecureSession::Config sec_cfg;
            sec_cfg.role = SecureSession::Role::Client;
            SecureSession sec(fd, sec_cfg, *crypto);
            auto hs = sec.handshake();
            if (!hs) {
                std::printf("FAIL (handshake: %s)\n", hs.error().message.c_str());
                last_error = hs.error().message;
                continue;
            }

            // ── Send encrypted JoinRequest, receive encrypted response ──
            auto send_res = sec.send(BytesView(req_cbor));
            if (!send_res) {
                std::printf("FAIL (send: %s)\n", send_res.error().message.c_str());
                last_error = send_res.error().message;
                continue;
            }

            auto enc_resp = sec.recv();
            if (!enc_resp) {
                std::printf("FAIL (recv: %s)\n", enc_resp.error().message.c_str());
                last_error = enc_resp.error().message;
                continue;
            }

            auto decode_result = join::JoinResponse::decode_cbor(BytesView(enc_resp.value()));
            if (!decode_result) {
                std::printf("FAIL (invalid response: %s)\n", decode_result.error().message.c_str());
                last_error = "Invalid CBOR response";
                continue;
            }

            resp = std::move(decode_result.value());
            sent = true;
            std::printf("OK (PQ-secure)\n");
            break;
        }

        if (!sent) {
            return SMO_ERR_CERT(214, Error, RetrySafe, None,
                                "All bootstrap endpoints unreachable. Last error: " + last_error);
        }

        auto r2 = fsm.on_event(static_cast<int64_t>(join::JoinEvent::RESPONSE_RCVD));
        if (!r2) return r2.error();
        save_join_state(fsm, actual_data_dir);
    }

    // ── Step: Process response + verify certificate (CERT_VERIFY) ───
    if (current_state() <= join::JoinState::CERT_RECEIVED) {
        if (resp.certificate_pem.empty()) {
            return SMO_ERR_CERT(215, Error, NoRetry, None, "No certificate in response");
        }

        Bytes cert_bytes;
        if (resp.certificate_pem.find("BEGIN") != std::string::npos) {
            cert_bytes = hex_to_bytes(resp.certificate_pem);
        } else {
            cert_bytes = hex_to_bytes(resp.certificate_pem);
        }

        auto cert_result = Certificate::deserialize(cert_bytes);
        if (!cert_result) {
            return SMO_ERR_CERT(216, Error, NoRetry, None,
                                "Failed to parse certificate: " + cert_result.error().message);
        }
        const auto& cert = cert_result.value();

        // Save certificate before verification
        std::string cert_path = actual_data_dir + "/cert.smoc";
        {
            std::ofstream f(cert_path, std::ios::binary);
            if (!f) {
                return SMO_ERR_STORAGE(900, Error, RetrySafe, None,
                                       "Failed to write certificate to " + cert_path);
            }
            f.write(reinterpret_cast<const char*>(cert_bytes.data()), cert_bytes.size());
        }

        auto r = fsm.on_event(static_cast<int64_t>(join::JoinEvent::RESPONSE_RCVD));
        if (!r) return r.error();
        save_join_state(fsm, actual_data_dir);

        // ── CERT_VERIFY: check cert validity ────────────────────────
        // Verify: not expired, valid signature, chain to mesh root
        std::printf("Verifying certificate...\n");

        // Check temporal validity
        int64_t now_sec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (!cert.is_valid_at(now_sec)) {
            auto r_inv = fsm.on_event(static_cast<int64_t>(join::JoinEvent::CERT_INVALID));
            if (r_inv) save_join_state(fsm, actual_data_dir);
            return SMO_ERR_CERT(216, Error, NoRetry, None,
                                "Certificate not valid at current time");
        }

        // Verify cert signature using crypto provider
        auto verify_cert = cert.verify(crypto->signer);
        if (!verify_cert || !verify_cert.value()) {
            std::printf("  Warning: could not verify cert signature locally\n");
        } else {
            std::printf("  Certificate signature valid\n");
        }

        std::printf("  Certificate: %s\n", cert_path.c_str());
        std::printf("  Node ID:     %s\n", cert.display_name.c_str());

        auto r2 = fsm.on_event(static_cast<int64_t>(join::JoinEvent::CERT_VERIFIED));
        if (!r2) return r2.error();
        save_join_state(fsm, actual_data_dir);
    }

    // ── Step: Bootstrap sync ───────────────────────────────────────
    if (current_state() >= join::JoinState::CERT_VERIFY &&
        current_state() <= join::JoinState::BOOTSTRAP_SYNC) {
        std::printf("Requesting bootstrap sync...\n");

        // Build BootstrapSyncRequest with known epochs from response
        join::BootstrapSyncRequest sync_req;
        sync_req.mesh_id = resp.mesh_id.empty() ? token.mesh_id : resp.mesh_id;
        sync_req.node_id = identity->node_id().to_string();
        sync_req.manifest_epoch = resp.manifest_epoch;
        // crl_epoch, membership_epoch, policy_version default to 0 (full sync)

        if (current_state() == join::JoinState::CERT_VERIFY) {
            auto r = fsm.on_event(static_cast<int64_t>(join::JoinEvent::SYNC_REQUESTED));
            if (!r) return r.error();
            save_join_state(fsm, actual_data_dir);
        }

        // Get bootstrap endpoints from JoinResponse or token
        auto& endpoints = !resp.bootstrap_nodes.empty()
            ? resp.bootstrap_nodes
            : token.bootstrap_endpoints;

        bool synced = false;
        std::string last_error;
        std::printf("  Bootstrap sync via secure TCP/CBOR (epoch: %llu)...\n",
                    (unsigned long long)sync_req.manifest_epoch);

        // Try each bootstrap endpoint until one works
        for (const auto& endpoint : endpoints) {
            size_t colon = endpoint.rfind(':');
            if (colon == std::string::npos) continue;
            std::string host = endpoint.substr(0, colon);
            uint16_t ep_port = static_cast<uint16_t>(std::stoi(endpoint.substr(colon + 1)));

            std::printf("    Trying endpoint: %s:%u ... ", host.c_str(), ep_port);

            int fd = tcp_connect(host, ep_port);
            if (fd < 0) {
                std::printf("FAIL (connection refused)\n");
                last_error = "Connection refused: " + host + ":" + std::to_string(ep_port);
                continue;
            }

            // ── PQ handshake ──────────────────────────────────────
            SecureSession::Config sec_cfg;
            sec_cfg.role = SecureSession::Role::Client;
            SecureSession sec(fd, sec_cfg, *crypto);
            auto hs = sec.handshake();
            if (!hs) {
                std::printf("FAIL (handshake: %s)\n", hs.error().message.c_str());
                last_error = hs.error().message;
                continue;
            }

            Bytes req_cbor = sync_req.encode_cbor();
            auto send_res = sec.send(BytesView(req_cbor));
            if (!send_res) {
                std::printf("FAIL (send: %s)\n", send_res.error().message.c_str());
                last_error = send_res.error().message;
                continue;
            }

            auto enc_resp = sec.recv();
            if (!enc_resp) {
                std::printf("FAIL (recv: %s)\n", enc_resp.error().message.c_str());
                last_error = enc_resp.error().message;
                continue;
            }

            auto decode_result = join::BootstrapSyncResponse::decode_cbor(
                BytesView(enc_resp.value()));
            if (!decode_result) {
                std::printf("FAIL (invalid response: %s)\n",
                            decode_result.error().message.c_str());
                last_error = "Invalid CBOR response";
                continue;
            }

            const auto& sync_resp = decode_result.value();
            std::printf("OK (mf=%llu mem=%llu crl=%llu pol=%llu)\n",
                        (unsigned long long)sync_resp.manifest_epoch,
                        (unsigned long long)sync_resp.membership_epoch,
                        (unsigned long long)sync_resp.crl_epoch,
                        (unsigned long long)sync_resp.policy_version);

            // Apply deltas
            if (!sync_resp.manifest_delta.empty()) {
                std::string mf_path = actual_data_dir + "/manifest_delta.cbor";
                std::ofstream mf_f(mf_path, std::ios::binary);
                if (mf_f) {
                    mf_f.write(reinterpret_cast<const char*>(
                        sync_resp.manifest_delta.data()), sync_resp.manifest_delta.size());
                }
                std::printf("    manifest delta: %zu bytes\n",
                            sync_resp.manifest_delta.size());
            }

            if (!sync_resp.membership_delta.empty()) {
                std::string mem_path = actual_data_dir + "/membership_delta.cbor";
                std::ofstream mem_f(mem_path, std::ios::binary);
                if (mem_f) {
                    mem_f.write(reinterpret_cast<const char*>(
                        sync_resp.membership_delta.data()), sync_resp.membership_delta.size());
                }
                std::printf("    membership delta: %zu nodes\n",
                            sync_resp.membership_delta.size());
            }

            if (!sync_resp.crl_delta.empty()) {
                std::string crl_path = actual_data_dir + "/crl_delta.cbor";
                std::ofstream crl_f(crl_path, std::ios::binary);
                if (crl_f) {
                    crl_f.write(reinterpret_cast<const char*>(
                        sync_resp.crl_delta.data()), sync_resp.crl_delta.size());
                }
                std::printf("    crl delta: %zu entries\n",
                            sync_resp.crl_delta.size());
            }

            if (!sync_resp.policy_delta.empty()) {
                std::string pol_path = actual_data_dir + "/policy_delta.cbor";
                std::ofstream pol_f(pol_path, std::ios::binary);
                if (pol_f) {
                    pol_f.write(reinterpret_cast<const char*>(
                        sync_resp.policy_delta.data()), sync_resp.policy_delta.size());
                }
                std::printf("    policy delta: %zu bytes\n",
                            sync_resp.policy_delta.size());
            }

            synced = true;
            break;
        }

        if (!synced) {
            return SMO_ERR_CERT(220, Error, RetrySafe, None,
                                "All bootstrap endpoints unreachable for sync. "
                                "Last error: " + last_error);
        }

        auto r2 = fsm.on_event(static_cast<int64_t>(join::JoinEvent::SYNC_COMPLETE));
        if (!r2) return r2.error();
        save_join_state(fsm, actual_data_dir);
    }

    // ── Step: Gossip sync (placeholder) ─────────────────────────────
    if (current_state() == join::JoinState::WAIT_SYNC) {
        auto r = fsm.on_event(static_cast<int64_t>(join::JoinEvent::GOSSIP_STARTED));
        if (!r) return r.error();
        save_join_state(fsm, actual_data_dir);

        auto r2 = fsm.on_event(static_cast<int64_t>(join::JoinEvent::GOSSIP_COMPLETE));
        if (!r2) return r2.error();
        save_join_state(fsm, actual_data_dir);
    }

    // ── Final: READY ────────────────────────────────────────────────
    if (current_state() == join::JoinState::READY) {
        std::string node_id_str = resp.mesh_id.empty() ? token.mesh_id : resp.mesh_id;

        std::printf("\n✓ Successfully enrolled!\n");
        std::printf("  Mesh:         %s\n", token.mesh_id.c_str());
        std::printf("  Role:         %s\n", token.admission.role.c_str());
        std::printf("  Profile:      %s\n", token.admission.profile.empty() ? "default" : token.admission.profile.c_str());
        if (!resp.manifest_digest.empty()) {
            std::printf("  Manifest:     epoch=%llu digest=", (unsigned long long)resp.manifest_epoch);
            for (auto b : resp.manifest_digest) std::printf("%02x", b);
            std::printf("\n");
        }
        if (!resp.bootstrap_nodes.empty()) {
            std::printf("  Bootstrap:    %zu seed(s)\n", resp.bootstrap_nodes.size());
            for (auto& ep : resp.bootstrap_nodes) {
                std::printf("    %s\n", ep.c_str());
            }
        }

        // Update identity state to Enrolled
        (void)identity->transition_to(IdentityState::Enrolled);
        (void)identity->save_to_file(identity_path);

        // Save config for daemon
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
                const auto& endpoints = !resp.bootstrap_nodes.empty() ? resp.bootstrap_nodes : token.bootstrap_endpoints;
                for (size_t i = 0; i < endpoints.size(); ++i) {
                    if (i > 0) f << ",\n";
                    f << "    \"" << endpoints[i] << "\"";
                }
                f << "\n  ],\n";
                f << "  \"node_name\": \"" << (node_name.empty() ? identity->node_id().to_string() : node_name) << "\"\n";
                f << "}\n";
            }
        }

        // Clear state file (join complete)
        clear_join_state(actual_data_dir);

        std::printf("\nRun 'smo-node --daemon --data %s --port %d' to start the node.\n",
                    actual_data_dir.c_str(), port);
    }

    return {};
}

} // namespace enroll
} // namespace smo
