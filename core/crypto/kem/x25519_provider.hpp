#pragma once

#include "core/crypto/impl.hpp"

namespace smo {
namespace kem {

struct X25519Provider {
    static KeypairResult generate_keypair(RngRef& rng);
    static EncapsResult encapsulate(BytesView pubkey, RngRef& rng);
    static Bytes decapsulate(BytesView privkey, BytesView ciphertext);
};

} // namespace kem
} // namespace smo
