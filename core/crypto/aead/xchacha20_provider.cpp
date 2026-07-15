#include "xchacha20_provider.hpp"

#include <monocypher.h>
#include <cstring>
#include <stdexcept>

namespace smo {
namespace aead {

Bytes XChaCha20Provider::encrypt(BytesView plaintext, BytesView aad,
                                 BytesView key, BytesView nonce) {
    if (key.size() != kKeySize)
        throw std::runtime_error("XChaCha20: invalid key size");
    if (nonce.size() != kNonceSize)
        throw std::runtime_error("XChaCha20: invalid nonce size");

    Bytes result(plaintext.size() + kMacSize);
    uint8_t mac[16];

    crypto_aead_lock(
        result.data(), mac,
        key.data(), nonce.data(),
        aad.data(), aad.size(),
        plaintext.data(), plaintext.size()
    );

    std::memcpy(result.data() + plaintext.size(), mac, kMacSize);
    return result;
}

Bytes XChaCha20Provider::decrypt(BytesView ciphertext, BytesView aad,
                                 BytesView key, BytesView nonce) {
    if (key.size() != kKeySize)
        throw std::runtime_error("XChaCha20: invalid key size");
    if (nonce.size() != kNonceSize)
        throw std::runtime_error("XChaCha20: invalid nonce size");
    if (ciphertext.size() < kMacSize)
        throw std::runtime_error("XChaCha20: truncated ciphertext");

    size_t pt_len = ciphertext.size() - kMacSize;
    Bytes result(pt_len);

    int ret = crypto_aead_unlock(
        result.data(),
        ciphertext.data() + pt_len, key.data(), nonce.data(),
        aad.data(), aad.size(),
        ciphertext.data(), pt_len
    );

    if (ret != 0)
        throw std::runtime_error("XChaCha20: decryption failed (MAC mismatch)");

    return result;
}

} // namespace aead
} // namespace smo
