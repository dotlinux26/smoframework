#pragma once

#include "core/crypto/impl.hpp"

#include <cstddef>
#include <cstdint>

namespace smo {
namespace aead {

struct XChaCha20Provider {
    static constexpr size_t kKeySize      = 32;
    static constexpr size_t kNonceSize    = 24;
    static constexpr size_t kMacSize      = 16;

    static Bytes encrypt(BytesView plaintext, BytesView aad,
                         BytesView key, BytesView nonce);
    static Bytes decrypt(BytesView ciphertext, BytesView aad,
                         BytesView key, BytesView nonce);
};

} // namespace aead
} // namespace smo
