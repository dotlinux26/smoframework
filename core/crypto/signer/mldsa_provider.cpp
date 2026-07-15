#include "mldsa_provider.hpp"

#include <oqs/oqs.h>
#include <cstring>
#include <stdexcept>

namespace smo {
namespace signer {

KeypairResult MLDSAProvider::generate_keypair(RngRef& rng) {
    OQS_SIG* sig = OQS_SIG_new(kAlgorithm);
    if (!sig) throw std::runtime_error("ML-DSA: algorithm not available");

    Bytes public_key(sig->length_public_key);
    Bytes secret_key(sig->length_secret_key);

    OQS_STATUS ret = OQS_SIG_keypair(sig, public_key.data(), secret_key.data());
    OQS_SIG_free(sig);

    if (ret != OQS_SUCCESS)
        throw std::runtime_error("ML-DSA: keypair generation failed");

    return KeypairResult{std::move(public_key), std::move(secret_key)};
}

Bytes MLDSAProvider::sign(BytesView msg, BytesView secret_key, RngRef& rng) {
    OQS_SIG* sig = OQS_SIG_new(kAlgorithm);
    if (!sig) throw std::runtime_error("ML-DSA: algorithm not available");

    if (secret_key.size() != sig->length_secret_key) {
        OQS_SIG_free(sig);
        throw std::runtime_error("ML-DSA: invalid secret key size");
    }

    Bytes signature(sig->length_signature);
    size_t sig_len = sig->length_signature;

    OQS_STATUS ret = OQS_SIG_sign(
        sig, signature.data(), &sig_len,
        msg.data(), msg.size(),
        secret_key.data()
    );

    OQS_SIG_free(sig);

    if (ret != OQS_SUCCESS)
        throw std::runtime_error("ML-DSA: signing failed");

    signature.resize(sig_len);
    return signature;
}

bool MLDSAProvider::verify(BytesView msg, BytesView signature, BytesView public_key) {
    OQS_SIG* sig = OQS_SIG_new(kAlgorithm);
    if (!sig) throw std::runtime_error("ML-DSA: algorithm not available");

    if (public_key.size() != sig->length_public_key) {
        OQS_SIG_free(sig);
        return false;
    }

    OQS_STATUS ret = OQS_SIG_verify(
        sig,
        msg.data(), msg.size(),
        signature.data(), signature.size(),
        public_key.data()
    );

    OQS_SIG_free(sig);
    return ret == OQS_SUCCESS;
}

} // namespace signer
} // namespace smo
