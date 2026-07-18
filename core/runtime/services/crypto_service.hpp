#pragma once

#include "core/types.hpp"
#include "core/errors/error.hpp"

#include <string>
#include <vector>
#include <cstdint>

namespace smo::runtime {

// ── Key types ────────────────────────────────────────────────────────
using KeyID = std::string;
using PublicKey = std::vector<uint8_t>;
using Signature = std::vector<uint8_t>;
enum class KeyType : uint8_t { Ed25519, Secp256k1, RSA4096, AES256 };

// ── CryptoService (RFC 0037 §2.5) ────────────────────────────────────
class CryptoService {
public:
    virtual ~CryptoService() = default;
    virtual Result<Signature> sign(const std::vector<uint8_t>& data, const KeyID& key) = 0;
    virtual Result<bool> verify(const std::vector<uint8_t>& data, const Signature& sig, const PublicKey& pk) = 0;
    virtual Result<std::vector<uint8_t>> encrypt(const std::vector<uint8_t>& plaintext, const PublicKey& pk) = 0;
    virtual Result<std::vector<uint8_t>> decrypt(const std::vector<uint8_t>& ciphertext, const KeyID& key) = 0;
    virtual Result<KeyID> generate_key(KeyType type) = 0;
};

} // namespace smo::runtime
