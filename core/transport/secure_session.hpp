#pragma once

#include "../errors/error.hpp"
#include "../types.hpp"
#include "../crypto/impl.hpp"
#include "../crypto/suite.hpp"

#include <cstdint>
#include <vector>

namespace smo {

// ── Constants for nonce construction ─────────────────────────────────
inline constexpr size_t kNoncePrefixLen = 16;
inline constexpr size_t kCounterLen     = 8;
inline constexpr size_t kSecureNonceLen = 24; // XChaCha20 needs 24-byte nonce

// ── SecureSession ────────────────────────────────────────────────────
//
// Per DISCUSSION_0039 §5.5: PQ-secure handshake + AEAD transport.
// Uses the configured CryptoProvider's KEM + AEAD + Signer + KDF.
//
// Handshake (1-RTT PQ key exchange):
//   Client → Server: [pk_len(2)][kem_pk_client(use_pk_len)]
//   Server → Client: [pk][ct][cert][sig] (each 2-byte length-prefixed)
//   Client → Server: [ct_len(2)][ct2]
//   Both derive session keys via HKDF(ss1 || ss2 || pk_client || pk_server)
//
// Post-handshake: all data encrypted with XChaCha20-Poly1305.
// Wire format per message: [4-byte len][24-byte nonce][AEAD output]
// ──────────────────────────────────────────────────────────────────────
class SecureSession {
public:
    enum class Role { Client, Server };

    struct Config {
        Role    role             = Role::Client;

        // Server authentication (required for Role::Server)
        Bytes   server_cert;         // signed certificate to present
        Bytes   signing_secret_key;  // identity key matching cert's pubkey
    };

    // Take ownership of a connected socket fd.
    SecureSession(int fd, Config config, const CryptoProvider& crypto);
    ~SecureSession();

    SecureSession(SecureSession&&) noexcept;
    SecureSession& operator=(SecureSession&&) noexcept;
    SecureSession(const SecureSession&) = delete;
    SecureSession& operator=(const SecureSession&) = delete;

    // Perform the PQ handshake. Returns error on failure.
    Result<void> handshake();

    // Encrypted send: encrypts plaintext, writes to socket.
    Result<void> send(BytesView plaintext);

    // Encrypted recv: reads from socket, decrypts, returns plaintext.
    Result<Bytes> recv();

    // Accessors
    bool is_secure() const { return secure_; }
    BytesView peer_certificate() const { return peer_cert_; }
    BytesView peer_public_key() const  { return peer_pk_; }
    int fd() const { return fd_; }

private:
    int fd_ = -1;
    bool secure_ = false;
    Config config_;
    const CryptoProvider& crypto_;

    // Handshake transcript state
    Bytes peer_pk_;   // peer's KEM public key
    Bytes peer_cert_; // peer's certificate (server cert for client)
    Bytes local_pk_;  // our KEM public key
    Bytes local_sk_;  // our KEM secret key

    // Session keys
    Bytes tx_key_;         // 32 bytes
    Bytes rx_key_;         // 32 bytes
    Bytes tx_nonce_pre_;   // 16 bytes
    Bytes rx_nonce_pre_;   // 16 bytes
    uint64_t tx_counter_ = 0;
    uint64_t rx_counter_ = 0;

    Result<void> client_handshake();
    Result<void> server_handshake();

    void derive_keys(BytesView ss1, BytesView ss2,
                     BytesView pk_client, BytesView pk_server);
    void build_nonce(BytesView prefix, uint64_t counter, BytesMutView out);
};

} // namespace smo
