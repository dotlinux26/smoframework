#include "x25519_provider.hpp"

#include <monocypher.h>
#include <cstring>

namespace smo {
namespace kem {

KeypairResult X25519Provider::generate_keypair(RngRef& rng) {
    Bytes secret_key(32);
    rng.fill(BytesMutView{secret_key.data(), secret_key.size()});

    Bytes public_key(32);
    crypto_x25519_public_key(public_key.data(), secret_key.data());

    return KeypairResult{std::move(public_key), std::move(secret_key)};
}

EncapsResult X25519Provider::encapsulate(BytesView pubkey, RngRef& rng) {
    // Generate ephemeral keypair
    Bytes ephem_sk(32);
    rng.fill(BytesMutView{ephem_sk.data(), ephem_sk.size()});

    Bytes ephem_pk(32);
    crypto_x25519_public_key(ephem_pk.data(), ephem_sk.data());

    // Compute shared secret
    Bytes shared(32);
    crypto_x25519(shared.data(), ephem_sk.data(), pubkey.data());

    // Ciphertext = ephemeral public key
    return EncapsResult{std::move(ephem_pk), std::move(shared)};
}

Bytes X25519Provider::decapsulate(BytesView privkey, BytesView ciphertext) {
    Bytes shared(32);
    crypto_x25519(shared.data(), privkey.data(), ciphertext.data());
    return shared;
}

} // namespace kem
} // namespace smo
