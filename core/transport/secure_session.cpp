#include "secure_session.hpp"

#include "../crypto/kdf/hkdf.hpp"
#include "../certificate/certificate.hpp"

#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

namespace smo {

// ── Raw I/O helpers ─────────────────────────────────────────────────

static Result<void> write_all(int fd, const void* data, size_t len) {
    auto* ptr = static_cast<const uint8_t*>(data);
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t n = ::write(fd, ptr, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            return SMO_ERR_TRANSPORT(304, Error, RetrySafe, Reconnect,
                                    "secure write failed");
        }
        ptr += n;
        remaining -= static_cast<size_t>(n);
    }
    return {};
}

static Result<void> read_all(int fd, void* data, size_t len) {
    auto* ptr = static_cast<uint8_t*>(data);
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t n = ::read(fd, ptr, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            return SMO_ERR_TRANSPORT(305, Error, NoRetry, Reconnect,
                                    "secure read failed");
        }
        if (n == 0) {
            return SMO_ERR_TRANSPORT(303, Error, NoRetry, None,
                                    "connection closed during secure read");
        }
        ptr += n;
        remaining -= static_cast<size_t>(n);
    }
    return {};
}

// ── Length-prefixed field helpers ───────────────────────────────────

static void store_u16(uint8_t* buf, uint16_t v) {
    buf[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
    buf[1] = static_cast<uint8_t>(v & 0xFF);
}

static uint16_t load_u16(const uint8_t* buf) {
    return (static_cast<uint16_t>(buf[0]) << 8) | static_cast<uint16_t>(buf[1]);
}

static Result<void> write_field(int fd, BytesView data) {
    uint8_t len_hdr[2];
    store_u16(len_hdr, static_cast<uint16_t>(data.size()));
    SMO_TRY(write_all(fd, len_hdr, 2));
    return write_all(fd, data.data(), data.size());
}

static Result<Bytes> read_field(int fd) {
    uint8_t len_hdr[2];
    SMO_TRY(read_all(fd, len_hdr, 2));
    uint16_t sz = load_u16(len_hdr);
    Bytes buf(sz);
    SMO_TRY(read_all(fd, buf.data(), sz));
    return buf;
}

// ── Constructor / Destructor ────────────────────────────────────────

SecureSession::SecureSession(int fd, Config config, const CryptoProvider& crypto)
    : fd_(fd), config_(std::move(config)), crypto_(crypto) {}

SecureSession::~SecureSession() {
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
    }
}

SecureSession::SecureSession(SecureSession&& other) noexcept
    : fd_(other.fd_),
      secure_(other.secure_),
      config_(std::move(other.config_)),
      crypto_(other.crypto_),
      peer_pk_(std::move(other.peer_pk_)),
      peer_cert_(std::move(other.peer_cert_)),
      local_pk_(std::move(other.local_pk_)),
      local_sk_(std::move(other.local_sk_)),
      tx_key_(std::move(other.tx_key_)),
      rx_key_(std::move(other.rx_key_)),
      tx_nonce_pre_(std::move(other.tx_nonce_pre_)),
      rx_nonce_pre_(std::move(other.rx_nonce_pre_)),
      tx_counter_(other.tx_counter_),
      rx_counter_(other.rx_counter_)
{
    other.fd_ = -1;
    other.secure_ = false;
}

SecureSession& SecureSession::operator=(SecureSession&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) ::close(fd_);
        fd_ = other.fd_; other.fd_ = -1;
        secure_ = other.secure_; other.secure_ = false;
        config_ = std::move(other.config_);
        peer_pk_ = std::move(other.peer_pk_);
        peer_cert_ = std::move(other.peer_cert_);
        local_pk_ = std::move(other.local_pk_);
        local_sk_ = std::move(other.local_sk_);
        tx_key_ = std::move(other.tx_key_);
        rx_key_ = std::move(other.rx_key_);
        tx_nonce_pre_ = std::move(other.tx_nonce_pre_);
        rx_nonce_pre_ = std::move(other.rx_nonce_pre_);
        tx_counter_ = other.tx_counter_;
        rx_counter_ = other.rx_counter_;
    }
    return *this;
}

// ── Nonce builder: prefix(16) || counter(8 big-endian) ─────────────

void SecureSession::build_nonce(BytesView prefix, uint64_t counter, BytesMutView out) {
    std::memcpy(out.data(), prefix.data(), kNoncePrefixLen);
    uint8_t* c = out.data() + kNoncePrefixLen;
    c[0] = static_cast<uint8_t>((counter >> 56) & 0xFF);
    c[1] = static_cast<uint8_t>((counter >> 48) & 0xFF);
    c[2] = static_cast<uint8_t>((counter >> 40) & 0xFF);
    c[3] = static_cast<uint8_t>((counter >> 32) & 0xFF);
    c[4] = static_cast<uint8_t>((counter >> 24) & 0xFF);
    c[5] = static_cast<uint8_t>((counter >> 16) & 0xFF);
    c[6] = static_cast<uint8_t>((counter >> 8) & 0xFF);
    c[7] = static_cast<uint8_t>(counter & 0xFF);
}

// ── Key derivation via HKDF ─────────────────────────────────────────

void SecureSession::derive_keys(BytesView ss1, BytesView ss2,
                                BytesView pk_client, BytesView pk_server) {
    Bytes ikm;
    ikm.insert(ikm.end(), ss1.begin(), ss1.end());
    ikm.insert(ikm.end(), ss2.begin(), ss2.end());
    ikm.insert(ikm.end(), pk_client.begin(), pk_client.end());
    ikm.insert(ikm.end(), pk_server.begin(), pk_server.end());

    static const uint8_t kSalt[] = "smo-pq-handshake-v1";
    static const uint8_t kInfo[] = "session-v1";

    Bytes material = kdf::hkdf(
        BytesView(kSalt, sizeof(kSalt) - 1),
        BytesView(ikm),
        BytesView(kInfo, sizeof(kInfo) - 1),
        96);

    tx_key_.assign(material.begin(), material.begin() + 32);
    rx_key_.assign(material.begin() + 32, material.begin() + 64);
    tx_nonce_pre_.assign(material.begin() + 64, material.begin() + 80);
    rx_nonce_pre_.assign(material.begin() + 80, material.begin() + 96);
}

// ── Handshake ───────────────────────────────────────────────────────

Result<void> SecureSession::handshake() {
    if (secure_) return {};

    switch (config_.role) {
    case Role::Client: SMO_TRY(client_handshake()); break;
    case Role::Server: SMO_TRY(server_handshake()); break;
    }

    secure_ = true;
    return {};
}

Result<void> SecureSession::client_handshake() {
    auto rng = crypto_.default_rng();

    // 1. Generate our KEM keypair
    auto kp = crypto_.kem.generate_keypair(rng);
    if (!kp) return kp.error();
    local_pk_ = std::move(kp.value().public_key);
    local_sk_ = std::move(kp.value().secret_key);

    // 2. Send ClientHello: [pk_len(2)][kem_pk]
    SMO_TRY(write_field(fd_, BytesView(local_pk_)));

    // 3. Read ServerHello: [pk][ct][cert][sig]
    auto server_pk_res = read_field(fd_);
    if (!server_pk_res) return server_pk_res.error();
    peer_pk_ = std::move(server_pk_res.value());

    auto ct_res = read_field(fd_);
    if (!ct_res) return ct_res.error();
    Bytes ct = std::move(ct_res.value());

    auto cert_res = read_field(fd_);
    if (!cert_res) return cert_res.error();
    peer_cert_ = std::move(cert_res.value());

    auto sig_res = read_field(fd_);
    if (!sig_res) return sig_res.error();
    Bytes signature = std::move(sig_res.value());

    // 4. Verify server signature
    Bytes sig_msg;
    static const uint8_t kCtx[] = "smo-pq-handshake-v1";
    sig_msg.insert(sig_msg.end(), kCtx, kCtx + sizeof(kCtx) - 1);
    sig_msg.insert(sig_msg.end(), local_pk_.begin(), local_pk_.end());
    sig_msg.insert(sig_msg.end(), peer_pk_.begin(), peer_pk_.end());
    sig_msg.insert(sig_msg.end(), ct.begin(), ct.end());

    auto cert = Certificate::deserialize(BytesView(peer_cert_));
    if (!cert) return cert.error();

    auto sig_ok = crypto_.signer.verify(
        BytesView(sig_msg), BytesView(signature),
        BytesView(cert.value().subject_pubkey));
    if (!sig_ok || !sig_ok.value()) {
        return SMO_ERR_CERT(216, Error, NoRetry, None,
                           "Server handshake signature invalid");
    }

    // 5. Decapsulate ct → ss1
    auto ss1_res = crypto_.kem.decapsulate(BytesView(local_sk_), BytesView(ct));
    if (!ss1_res) return ss1_res.error();

    // 6. Encapsulate server_pk → ct2, ss2
    auto enc2 = crypto_.kem.encapsulate(BytesView(peer_pk_), rng);
    if (!enc2) return enc2.error();

    // 7. Send ClientFinish: [ct_len(2)][ct2]
    SMO_TRY(write_field(fd_, BytesView(enc2.value().ciphertext)));

    // 8. Derive session keys
    derive_keys(BytesView(ss1_res.value()), BytesView(enc2.value().shared_secret),
                BytesView(local_pk_), BytesView(peer_pk_));

    return {};
}

Result<void> SecureSession::server_handshake() {
    auto rng = crypto_.default_rng();

    // 1. Read ClientHello: [pk_len(2)][kem_pk]
    auto client_pk_res = read_field(fd_);
    if (!client_pk_res) return client_pk_res.error();
    peer_pk_ = std::move(client_pk_res.value());

    // 2. Generate our KEM keypair
    auto kp = crypto_.kem.generate_keypair(rng);
    if (!kp) return kp.error();
    local_pk_ = std::move(kp.value().public_key);
    local_sk_ = std::move(kp.value().secret_key);

    // 3. Encapsulate client_pk → ct, ss1
    auto enc = crypto_.kem.encapsulate(BytesView(peer_pk_), rng);
    if (!enc) return enc.error();
    Bytes ct = std::move(enc.value().ciphertext);
    Bytes ss1 = std::move(enc.value().shared_secret);

    // 4. Sign handshake
    Bytes sig_msg;
    static const uint8_t kCtx[] = "smo-pq-handshake-v1";
    sig_msg.insert(sig_msg.end(), kCtx, kCtx + sizeof(kCtx) - 1);
    sig_msg.insert(sig_msg.end(), peer_pk_.begin(), peer_pk_.end());
    sig_msg.insert(sig_msg.end(), local_pk_.begin(), local_pk_.end());
    sig_msg.insert(sig_msg.end(), ct.begin(), ct.end());

    auto sig_res = crypto_.signer.sign(
        BytesView(sig_msg),
        BytesView(config_.signing_secret_key), rng);
    if (!sig_res) return sig_res.error();

    // 5. Send ServerHello: [pk][ct][cert][sig]
    SMO_TRY(write_field(fd_, BytesView(local_pk_)));
    SMO_TRY(write_field(fd_, BytesView(ct)));
    SMO_TRY(write_field(fd_, BytesView(config_.server_cert)));
    SMO_TRY(write_field(fd_, BytesView(sig_res.value())));

    // 6. Read ClientFinish: [ct_len(2)][ct2]
    auto ct2_res = read_field(fd_);
    if (!ct2_res) return ct2_res.error();

    // 7. Decapsulate ct2 → ss2
    auto ss2_res = crypto_.kem.decapsulate(BytesView(local_sk_), BytesView(ct2_res.value()));
    if (!ss2_res) return ss2_res.error();

    // 8. Derive session keys
    derive_keys(BytesView(ss1), BytesView(ss2_res.value()),
                BytesView(peer_pk_), BytesView(local_pk_));

    return {};
}

// ── Encrypted send/recv ────────────────────────────────────────────

Result<void> SecureSession::send(BytesView plaintext) {
    if (!secure_) {
        return SMO_ERR_TRANSPORT(312, Error, NoRetry, Reconnect,
                                "send before handshake");
    }

    uint8_t nonce_bytes[kSecureNonceLen];
    build_nonce(tx_nonce_pre_, ++tx_counter_,
                BytesMutView(nonce_bytes, kSecureNonceLen));

    auto encrypted = crypto_.aead.encrypt(
        plaintext, BytesView{}, BytesView(tx_key_),
        BytesView(nonce_bytes, kSecureNonceLen));
    if (!encrypted) return encrypted.error();

    // Wire: [4-byte len][24-byte nonce][ciphertext+mac(16)]
    uint32_t enc_len = static_cast<uint32_t>(encrypted.value().size());
    uint32_t total_len = static_cast<uint32_t>(kSecureNonceLen) + enc_len;

    uint8_t hdr[4];
    hdr[0] = static_cast<uint8_t>((total_len >> 24) & 0xFF);
    hdr[1] = static_cast<uint8_t>((total_len >> 16) & 0xFF);
    hdr[2] = static_cast<uint8_t>((total_len >> 8) & 0xFF);
    hdr[3] = static_cast<uint8_t>(total_len & 0xFF);

    SMO_TRY(write_all(fd_, hdr, 4));
    SMO_TRY(write_all(fd_, nonce_bytes, kSecureNonceLen));
    SMO_TRY(write_all(fd_, encrypted.value().data(), encrypted.value().size()));
    return {};
}

Result<Bytes> SecureSession::recv() {
    if (!secure_) {
        return SMO_ERR_TRANSPORT(312, Error, NoRetry, Reconnect,
                                "recv before handshake");
    }

    uint8_t hdr[4];
    SMO_TRY(read_all(fd_, hdr, 4));
    uint32_t total_len = (static_cast<uint32_t>(hdr[0]) << 24) |
                         (static_cast<uint32_t>(hdr[1]) << 16) |
                         (static_cast<uint32_t>(hdr[2]) << 8)  |
                          static_cast<uint32_t>(hdr[3]);

    if (total_len > 1024 * 1024) {
        return SMO_ERR_TRANSPORT(400, Error, NoRetry, None,
                                "encrypted payload too large");
    }

    Bytes enc(total_len);
    SMO_TRY(read_all(fd_, enc.data(), enc.size()));

    if (enc.size() < kSecureNonceLen + 16) {
        return SMO_ERR_TRANSPORT(400, Error, NoRetry, None,
                                "truncated encrypted payload");
    }

    BytesView wire_nonce(enc.data(), kSecureNonceLen);
    BytesView ciphertext(enc.data() + kSecureNonceLen,
                         enc.size() - kSecureNonceLen);

    // Verify expected nonce
    uint8_t expected[kSecureNonceLen];
    build_nonce(rx_nonce_pre_, rx_counter_ + 1,
                BytesMutView(expected, kSecureNonceLen));
    if (std::memcmp(wire_nonce.data(), expected, kSecureNonceLen) != 0) {
        return SMO_ERR_TRANSPORT(314, Error, NoRetry, Reconnect,
                                "nonce mismatch");
    }
    ++rx_counter_;

    return crypto_.aead.decrypt(
        ciphertext, BytesView{}, BytesView(rx_key_), BytesView(wire_nonce));
}

} // namespace smo
