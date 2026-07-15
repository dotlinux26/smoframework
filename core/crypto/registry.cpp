#include "registry.hpp"

#include <algorithm>

namespace smo {

CryptoRegistry& CryptoRegistry::instance() {
    static CryptoRegistry reg;
    return reg;
}

Result<void> CryptoRegistry::register_suite(const CryptoProvider& provider) {
    if (provider.suite_id < kSuiteMin || provider.suite_id > kSuiteMax) {
        return SMO_ERR_CRYPTO(8, Error, NoRetry, None,
                              "invalid suite id");
    }
    if (provider.name == nullptr ||
        provider.rng_fill == nullptr ||
        provider.hash.hash == nullptr ||
        provider.hash.hmac == nullptr ||
        provider.aead.encrypt == nullptr ||
        provider.aead.decrypt == nullptr ||
        provider.kem.encapsulate == nullptr ||
        provider.kem.decapsulate == nullptr ||
        provider.signer.generate_keypair == nullptr ||
        provider.signer.sign == nullptr ||
        provider.signer.verify == nullptr)
    {
        return SMO_ERR_CRYPTO(8, Error, NoRetry, None,
                              "suite has null function pointers");
    }

    for (const auto& e : suites_) {
        if (e.id == provider.suite_id) {
            return SMO_ERR_CRYPTO(8, Error, NoRetry, None,
                                  "duplicate suite registration");
        }
    }

    suites_.push_back({provider.suite_id, &provider});
    return {};
}

Result<const CryptoProvider*> CryptoRegistry::get_suite(CryptoSuiteID id) const {
    for (const auto& e : suites_) {
        if (e.id == id) return e.provider;
    }
    return SMO_ERR_CRYPTO(8, Error, NoRetry, None,
                          "unsupported crypto suite");
}

std::vector<CryptoSuiteID> CryptoRegistry::available_suites() const {
    std::vector<CryptoSuiteID> ids;
    ids.reserve(suites_.size());
    for (const auto& e : suites_) ids.push_back(e.id);
    return ids;
}

} // namespace smo
