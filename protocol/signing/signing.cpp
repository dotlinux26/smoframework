#include "signing.h"

#include "../../core/crypto/registry.hpp"
#include <cstring>

namespace smo {

void keypair_generate(PublicKey& pk, SecretKey& sk) {
    auto& reg = CryptoRegistry::instance();
    auto prov_res = reg.get_suite(kSuiteClassical);
    if (!prov_res) return;

    const auto* prov = prov_res.value();
    auto rng = prov->default_rng();
    if (!rng) return;

    auto result = prov->signer.generate_keypair(rng);
    if (!result) return;

    auto& kp = result.value();
    std::memcpy(pk.value.data(), kp.public_key.data(),
                std::min(pk.value.size(), kp.public_key.size()));
    std::memcpy(sk.value.data(), kp.secret_key.data(),
                std::min(sk.value.size(), kp.secret_key.size()));
}

Signature sign(std::span<const uint8_t> msg, const SecretKey& sk) {
    Signature sig{};
    auto& reg = CryptoRegistry::instance();
    auto prov_res = reg.get_suite(kSuiteClassical);
    if (!prov_res) return sig;

    const auto* prov = prov_res.value();
    auto rng = prov->default_rng();
    if (!rng) return sig;

    auto result = prov->signer.sign(msg, sk.value, rng);
    if (!result) return sig;

    auto& sig_bytes = result.value();
    std::memcpy(sig.value.data(), sig_bytes.data(),
                std::min(sig.value.size(), sig_bytes.size()));
    return sig;
}

bool verify(std::span<const uint8_t> msg, const Signature& sig, const PublicKey& pk) {
    auto& reg = CryptoRegistry::instance();
    auto prov_res = reg.get_suite(kSuiteClassical);
    if (!prov_res) return false;

    const auto* prov = prov_res.value();
    auto result = prov->signer.verify(msg, sig.value, pk.value);
    if (!result) return false;
    return result.value();
}

} // namespace smo
