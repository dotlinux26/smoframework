#include "suite3_purepqc_provider.hpp"

#include "core/crypto/registry.hpp"
#include "core/crypto/random/getrandom.hpp"
#include "core/crypto/signer/mldsa_provider.hpp"
#include "core/crypto/kem/mlkem_provider.hpp"
#include "core/crypto/aead/xchacha20_provider.hpp"
#include "providers/blake3_provider/blake3_provider.hpp"

#include <cstring>

namespace smo {
namespace providers {

namespace {

void rng_fill(void*, uint8_t* buf, size_t len) {
    random::fill(BytesMutView{buf, len});
}

Result<KeypairResult> mldsa_gen(RngRef& rng) {
    return signer::MLDSAProvider::generate_keypair(rng);
}

Result<Bytes> mldsa_sign(BytesView msg, BytesView sk, RngRef& rng) {
    return signer::MLDSAProvider::sign(msg, sk, rng);
}

Result<bool> mldsa_verify(BytesView msg, BytesView sig, BytesView pk) {
    return signer::MLDSAProvider::verify(msg, sig, pk);
}

Result<KeypairResult> mlkem_gen(RngRef& rng) {
    return kem::MLKEMProvider::generate_keypair(rng);
}

Result<EncapsResult> mlkem_encap(BytesView pk, RngRef& rng) {
    return kem::MLKEMProvider::encapsulate(pk, rng);
}

Result<Bytes> mlkem_decap(BytesView sk, BytesView ct) {
    return kem::MLKEMProvider::decapsulate(sk, ct);
}

Result<Bytes> blake3_hash_fn(BytesView data) {
    Blake3Provider p;
    return p.hash(data);
}

Result<Bytes> blake3_hmac_fn(BytesView key, BytesView data) {
    constexpr size_t BLOCK_SIZE = 64;
    Bytes key_block(BLOCK_SIZE, 0);
    Blake3Provider h;

    if (key.size() > BLOCK_SIZE) {
        auto hashed = h.hash(key);
        std::memcpy(key_block.data(), hashed.data(), hashed.size());
    } else {
        std::memcpy(key_block.data(), key.data(), key.size());
    }

    Bytes ipad(BLOCK_SIZE), opad(BLOCK_SIZE);
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        ipad[i] = key_block[i] ^ 0x36;
        opad[i] = key_block[i] ^ 0x5c;
    }

    Bytes inner(ipad.size() + data.size());
    std::memcpy(inner.data(), ipad.data(), ipad.size());
    std::memcpy(inner.data() + ipad.size(), data.data(), data.size());
    auto inner_hash = h.hash(inner);

    Bytes outer(opad.size() + inner_hash.size());
    std::memcpy(outer.data(), opad.data(), opad.size());
    std::memcpy(outer.data() + opad.size(), inner_hash.data(), inner_hash.size());
    return h.hash(outer);
}

Result<Bytes> xchacha20_encrypt_fn(BytesView pt, BytesView aad, BytesView key, BytesView nonce) {
    return aead::XChaCha20Provider::encrypt(pt, aad, key, nonce);
}

Result<Bytes> xchacha20_decrypt_fn(BytesView ct, BytesView aad, BytesView key, BytesView nonce) {
    return aead::XChaCha20Provider::decrypt(ct, aad, key, nonce);
}

} // anonymous namespace

const CryptoProvider& get_suite3_purepqc_provider() noexcept {
    static const CryptoProvider provider = {
        .suite_id   = kSuitePurePQC,
        .name       = "PurePQC",
        .rng_ctx    = nullptr,
        .rng_fill   = &rng_fill,
        .hash = HashImpl{
            .hash = blake3_hash_fn,
            .hmac = blake3_hmac_fn,
        },
        .perf_hash = PerformanceHashImpl{},
        .aead = AeadImpl{
            .encrypt = xchacha20_encrypt_fn,
            .decrypt = xchacha20_decrypt_fn,
        },
        .kem = KemImpl{
            .encapsulate = mlkem_encap,
            .decapsulate = mlkem_decap,
        },
        .signer = SignerImpl{
            .generate_keypair = mldsa_gen,
            .sign             = mldsa_sign,
            .verify           = mldsa_verify,
        },
    };
    return provider;
}

void register_suite3_purepqc() {
    auto& registry = CryptoRegistry::instance();
    auto& provider = get_suite3_purepqc_provider();
    auto result = registry.register_suite(provider);
    if (!result) {
        throw std::runtime_error("Suite 3 PurePQC registration failed");
    }
}

} // namespace providers
} // namespace smo
