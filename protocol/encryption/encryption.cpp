#include "encryption.h"

#include "../../core/crypto/registry.hpp"
#include "../../core/errors/error.hpp"

namespace smo {

std::vector<uint8_t> encrypt(std::span<const uint8_t> plaintext,
                               const SymmetricKey& key,
                               const Nonce& nonce)
{
    auto& reg = CryptoRegistry::instance();
    auto prov_res = reg.get_suite(kSuiteClassical);
    if (!prov_res) return {};

    const auto* prov = prov_res.value();
    auto result = prov->aead.encrypt(plaintext, {}, key.value, nonce.value);
    if (!result) return {};
    return std::move(result.value());
}

std::vector<uint8_t> decrypt(std::span<const uint8_t> ciphertext,
                               const SymmetricKey& key,
                               const Nonce& nonce)
{
    auto& reg = CryptoRegistry::instance();
    auto prov_res = reg.get_suite(kSuiteClassical);
    if (!prov_res) return {};

    const auto* prov = prov_res.value();
    auto result = prov->aead.decrypt(ciphertext, {}, key.value, nonce.value);
    if (!result) return {};
    return std::move(result.value());
}

} // namespace smo
