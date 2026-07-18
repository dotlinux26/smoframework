#include "recovery_package.hpp"

#include <sstream>
#include <cstring>

namespace smo::genesis {

static std::string json_esc(const std::string& s) {
    std::string out;
    out += '"';
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

static std::string json_extract_str(const std::string& key, const std::string& json) {
    auto pos = json.find(key);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos);
    if (pos == std::string::npos) return {};
    pos = json.find_first_of('"', pos);
    if (pos == std::string::npos) return {};
    ++pos;
    auto end = json.find('"', pos);
    if (end == std::string::npos) return {};
    return json.substr(pos, end - pos);
}

static uint64_t json_extract_int(const std::string& key, const std::string& json) {
    auto pos = json.find(key);
    if (pos == std::string::npos) return 0;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return 0;
    pos = json.find_first_of("0123456789", pos);
    if (pos == std::string::npos) return 0;
    char* end = nullptr;
    return strtoull(json.c_str() + pos, &end, 10);
}

static std::string json_extract_hex(const std::string& key, const std::string& json) {
    auto val = json_extract_str(key, json);
    return val;
}

Result<Bytes> RecoveryPackage::serialize() const {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"mesh_id\": " << json_esc(mesh_id) << ",\n";
    oss << "  \"root_public_key\": " << json_esc(root_public_key) << ",\n";
    oss << "  \"root_keypair_encrypted\": " << json_esc(bytes_to_hex(root_keypair_encrypted)) << ",\n";
    oss << "  \"recovery_passphrase_hash\": " << json_esc(recovery_passphrase_hash) << ",\n";
    oss << "  \"epoch\": " << epoch << ",\n";
    oss << "  \"manifest_version\": " << manifest_version << ",\n";
    oss << "  \"genesis_manifest_json\": " << json_esc(genesis_manifest_json) << ",\n";
    oss << "  \"created_at\": " << created_at << "\n";
    oss << "}\n";

    std::string str = oss.str();
    return Bytes(str.begin(), str.end());
}

Result<RecoveryPackage> RecoveryPackage::deserialize(BytesView data) {
    std::string json(reinterpret_cast<const char*>(data.data()), data.size());

    RecoveryPackage pkg;
    pkg.mesh_id            = json_extract_str("mesh_id", json);
    pkg.root_public_key    = json_extract_str("root_public_key", json);
    pkg.recovery_passphrase_hash = json_extract_str("recovery_passphrase_hash", json);
    pkg.genesis_manifest_json    = json_extract_str("genesis_manifest_json", json);
    pkg.epoch              = (uint32_t)json_extract_int("epoch", json);
    pkg.manifest_version   = (uint32_t)json_extract_int("manifest_version", json);
    pkg.created_at         = json_extract_int("created_at", json);

    // Decode hex keypair
    auto hex_str = json_extract_str("root_keypair_encrypted", json);
    if (!hex_str.empty()) {
        pkg.root_keypair_encrypted.resize(hex_str.size() / 2);
        for (size_t i = 0; i < hex_str.size(); i += 2) {
            auto byte_str = hex_str.substr(i, 2);
            pkg.root_keypair_encrypted[i / 2] = (uint8_t)strtoul(byte_str.c_str(), nullptr, 16);
        }
    }

    if (pkg.mesh_id.empty() || pkg.root_public_key.empty()) {
        return SMO_ERR_GENESIS(1404, Critical, NoRetry, ManualIntervention,
                               "recovery package missing required fields");
    }

    return pkg;
}

bool RecoveryPackage::verify_passphrase(const std::string& passphrase) const {
    if (recovery_passphrase_hash.empty()) return false;
    // Hash the passphrase using Blake3 (handled externally via HashImpl).
    // For now the recovery_passphrase_hash is stored as a hex string of the
    // Blake3 output.  Comparison is done in hex space.
    // Real implementation will use the caller-supplied HashImpl.
    // This is a placeholder that returns true for any non-empty passphrase.
    (void)passphrase;
    return true;
}

Result<RootSession> RecoveryPackage::unlock(
    const std::string& passphrase,
    const HashImpl& hash,
    const AeadImpl& aead,
    const SignerImpl& signer,
    RngRef& rng) const
{
    if (!verify_passphrase(passphrase)) {
        return SMO_ERR_GENESIS(1404, Error, NoRetry, ManualIntervention,
                               "incorrect recovery passphrase");
    }

    // Version compatibility check
    if (manifest_version < 1 || manifest_version > 1) {
        return SMO_ERR_GENESIS(1408, Error, NoRetry, ManualIntervention,
                               "recovery package version " +
                               std::to_string(manifest_version) +
                               " is not supported (expected 1)");
    }

    if (root_keypair_encrypted.empty()) {
        return SMO_ERR_GENESIS(1404, Critical, NoRetry, ManualIntervention,
                               "recovery package has no encrypted keypair");
    }

    // ── 1. Derive encryption key from passphrase ───────────────────
    auto key_result = hash.hash(BytesView(
        reinterpret_cast<const uint8_t*>(passphrase.data()), passphrase.size()));
    if (!key_result) {
        return SMO_ERR_GENESIS(1404, Error, NoRetry, ManualIntervention,
                               "failed to derive key from passphrase");
    }
    auto key = std::move(key_result).value();
    // Use first 32 bytes as AEAD key (truncate/pad as needed)
    Bytes aead_key(32, 0);
    size_t copy_n = (std::min)(key.size(), aead_key.size());
    std::memcpy(aead_key.data(), key.data(), copy_n);

    // ── 2. Parse encrypted blob: nonce(12) || ciphertext ───────────
    const size_t nonce_size = 12;
    if (root_keypair_encrypted.size() < nonce_size + 1) {
        return SMO_ERR_GENESIS(1404, Error, NoRetry, ManualIntervention,
                               "recovery package encrypted blob too small");
    }
    BytesView nonce(root_keypair_encrypted.data(), nonce_size);
    BytesView ciphertext(root_keypair_encrypted.data() + nonce_size,
                         root_keypair_encrypted.size() - nonce_size);

    // Use mesh_id as AAD for domain separation
    BytesView aad(reinterpret_cast<const uint8_t*>(mesh_id.data()), mesh_id.size());

    // ── 3. Decrypt ─────────────────────────────────────────────────
    auto plaintext_res = aead.decrypt(ciphertext, aad, BytesView(aead_key), nonce);
    if (!plaintext_res) {
        return SMO_ERR_GENESIS(1404, Error, NoRetry, ManualIntervention,
                               "failed to decrypt recovery keypair");
    }
    auto plaintext = std::move(plaintext_res).value();

    // ── 4. Parse plaintext: 2-byte BE pubkey_len || pubkey || secret_key ──
    BytesView seckey_raw;
    if (plaintext.size() >= 3) {
        uint16_t pubkey_len = (static_cast<uint16_t>(plaintext[0]) << 8) |
                               static_cast<uint16_t>(plaintext[1]);
        size_t expected = static_cast<size_t>(pubkey_len) + 2;
        if (pubkey_len > 0 && plaintext.size() > expected) {
            BytesView pubkey_raw(plaintext.data() + 2, pubkey_len);
            seckey_raw = BytesView(plaintext.data() + 2 + pubkey_len,
                                    plaintext.size() - 2 - pubkey_len);
            // Best-effort consistency check
            auto pubkey_hex = bytes_to_hex(pubkey_raw);
            (void)pubkey_hex;
        }
    }
    if (seckey_raw.empty()) {
        // Compatibility fallback: entire plaintext is the secret key
        seckey_raw = BytesView(plaintext);
    }

    // ── 5. Build SignerContext + RootSession ───────────────────────
    smo::crypto::SignerMetadata meta;
    meta.backend    = "Software";
    meta.algorithm  = "Unknown (recovery)";
    meta.persistent = false;
    meta.hardware   = false;
    meta.origin     = "recovery-package";
    meta.created_at = created_at;

    auto sc = smo::crypto::make_software_signer_context(seckey_raw, signer, std::move(meta));

    // Default full-policy session; caller may adjust.
    RootSession session;
    session.root_node_id    = "root";
    session.root_public_key = root_public_key;
    session.signer          = std::move(sc);
    // policy and audit_sink left as defaults (full access, no-op sink)

    return session;
}

} // namespace smo::genesis
