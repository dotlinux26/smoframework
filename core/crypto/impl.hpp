#pragma once

#include "fwd.hpp"
#include "../errors/error.hpp"

#include <cstddef>
#include <cstdint>

namespace smo {

// ---------------------------------------------------------------------------
// RngRef — lightweight, non-owning reference to an RNG
// ---------------------------------------------------------------------------
class RngRef {
public:
    using FillFn = void (*)(void* ctx, uint8_t* buf, size_t len);

    RngRef() = default;
    RngRef(void* ctx, FillFn fn) noexcept : ctx_(ctx), fn_(fn) {}

    void fill(BytesMutView buf) { fn_(ctx_, buf.data(), buf.size()); }

    explicit operator bool() const noexcept { return fn_ != nullptr; }

private:
    void* ctx_ = nullptr;
    FillFn fn_ = nullptr;
};

// ---------------------------------------------------------------------------
// HashImpl — function-pointer table for cryptographic hash operations
// ---------------------------------------------------------------------------
struct HashImpl {
    Result<Bytes> (*hash)(BytesView data)                              = nullptr;
    Result<Bytes> (*hmac)(BytesView key, BytesView data)               = nullptr;
};

// ---------------------------------------------------------------------------
// PerformanceHashImpl — non-cryptographic hash (cache keys, hash tables)
// ---------------------------------------------------------------------------
struct PerformanceHashImpl {
    uint64_t (*hash64)(BytesView data)                                 = nullptr;
    uint32_t (*hash32)(BytesView data)                                 = nullptr;
};

// ---------------------------------------------------------------------------
// AeadImpl — function-pointer table for AEAD encrypt/decrypt
// ---------------------------------------------------------------------------
struct AeadImpl {
    Result<Bytes> (*encrypt)(BytesView plaintext, BytesView aad,
                             BytesView key, BytesView nonce)           = nullptr;
    Result<Bytes> (*decrypt)(BytesView ciphertext, BytesView aad,
                             BytesView key, BytesView nonce)           = nullptr;
};

// ---------------------------------------------------------------------------
// KemImpl — function-pointer table for KEM encapsulate/decapsulate
// ---------------------------------------------------------------------------
struct KemImpl {
    Result<EncapsResult> (*encapsulate)(BytesView pubkey, RngRef& rng) = nullptr;
    Result<Bytes>        (*decapsulate)(BytesView privkey,
                                        BytesView ciphertext)          = nullptr;
};

// ---------------------------------------------------------------------------
// SignerImpl — function-pointer table for signing operations
// ---------------------------------------------------------------------------
struct SignerImpl {
    Result<KeypairResult> (*generate_keypair)(RngRef& rng)             = nullptr;
    Result<Bytes>         (*sign)(BytesView msg, BytesView secret_key,
                                  RngRef& rng)                         = nullptr;
    Result<bool>          (*verify)(BytesView msg, BytesView signature,
                                    BytesView public_key)             = nullptr;
};

// ---------------------------------------------------------------------------
// CryptoProvider — bundles all interfaces for a single Crypto Suite
// ---------------------------------------------------------------------------
struct CryptoProvider {
    CryptoSuiteID suite_id;
    const char*   name;

    // RNG (stateful: context pointer + fill function)
    void*  rng_ctx;
    void (*rng_fill)(void* ctx, uint8_t* buf, size_t len);

    RngRef default_rng() const { return RngRef(rng_ctx, rng_fill); }

    HashImpl            hash;
    PerformanceHashImpl perf_hash;       // non-cryptographic hash (may be null)
    AeadImpl            aead;
    KemImpl             kem;
    SignerImpl          signer;
};

} // namespace smo
