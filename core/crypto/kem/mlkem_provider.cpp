#include "mlkem_provider.hpp"

#include <oqs/oqs.h>
#include <cstring>
#include <stdexcept>

namespace smo {
namespace kem {

KeypairResult MLKEMProvider::generate_keypair(RngRef& rng) {
    OQS_KEM* kem = OQS_KEM_new(kAlgorithm);
    if (!kem) throw std::runtime_error("ML-KEM: algorithm not available");

    Bytes public_key(kem->length_public_key);
    Bytes secret_key(kem->length_secret_key);

    OQS_STATUS ret = OQS_KEM_keypair(kem, public_key.data(), secret_key.data());
    OQS_KEM_free(kem);

    if (ret != OQS_SUCCESS)
        throw std::runtime_error("ML-KEM: keypair generation failed");

    return KeypairResult{std::move(public_key), std::move(secret_key)};
}

EncapsResult MLKEMProvider::encapsulate(BytesView pubkey, RngRef& rng) {
    OQS_KEM* kem = OQS_KEM_new(kAlgorithm);
    if (!kem) throw std::runtime_error("ML-KEM: algorithm not available");

    if (pubkey.size() != kem->length_public_key) {
        OQS_KEM_free(kem);
        throw std::runtime_error("ML-KEM: invalid public key size");
    }

    Bytes ciphertext(kem->length_ciphertext);
    Bytes shared_secret(kem->length_shared_secret);

    OQS_STATUS ret = OQS_KEM_encaps(
        kem,
        ciphertext.data(), shared_secret.data(),
        pubkey.data()
    );

    OQS_KEM_free(kem);

    if (ret != OQS_SUCCESS)
        throw std::runtime_error("ML-KEM: encapsulate failed");

    return EncapsResult{std::move(ciphertext), std::move(shared_secret)};
}

Bytes MLKEMProvider::decapsulate(BytesView privkey, BytesView ciphertext) {
    OQS_KEM* kem = OQS_KEM_new(kAlgorithm);
    if (!kem) throw std::runtime_error("ML-KEM: algorithm not available");

    if (privkey.size() != kem->length_secret_key) {
        OQS_KEM_free(kem);
        throw std::runtime_error("ML-KEM: invalid secret key size");
    }
    if (ciphertext.size() != kem->length_ciphertext) {
        OQS_KEM_free(kem);
        throw std::runtime_error("ML-KEM: invalid ciphertext size");
    }

    Bytes shared_secret(kem->length_shared_secret);

    OQS_STATUS ret = OQS_KEM_decaps(
        kem,
        shared_secret.data(),
        ciphertext.data(),
        privkey.data()
    );

    OQS_KEM_free(kem);

    if (ret != OQS_SUCCESS)
        throw std::runtime_error("ML-KEM: decapsulate failed");

    return shared_secret;
}

} // namespace kem
} // namespace smo
