# RFC 0009 — Crypto Provider Architecture

## Status
ACCEPTED — incorporated into SPEC.md §6.4, §XVI (Suite ID model + Crypto Registry pattern).

## Problem
SMO Phase 1 uses Ed25519 + XChaCha20-Poly1305 + Blake3. Phase 2 must add post-quantum primitives (ML-DSA, ML-KEM). The runtime must never know which algorithm is in use. Crypto primitives must be swappable at the Suite ID level without runtime code changes.

## Decisions

### 1. Suite ID drives all algorithm selection
Every cryptographic operation references a `CryptoSuiteID` (uint16). The protocol never encodes algorithm names — only Suite IDs. The CryptoProvider registry maps SuiteID → concrete implementation.

### 2. Pre-defined Suite IDs

| Suite ID | Signing | KEM | AEAD | Hash | Status |
|---|---|---|---|---|---|
| 0x0000 | Ed25519 | X25519 | XChaCha20-Poly1305 | Blake3 | Phase 1 |
| 0x0001 | ML-DSA-65 | ML-KEM-768 | XChaCha20-Poly1305 | Blake3 | Phase 2 |
| 0x0002 | Ed25519 + ML-DSA-65 | X25519 + ML-KEM-768 | XChaCha20-Poly1305 | Blake3 | Phase 2 hybrid |

### 3. Interface-based abstraction (virtual dispatch at Suite granularity)
Each primitive family has an abstract interface:

- `Signer` — sign, verify, keypair_generate
- `KEM` — encapsulate, decapsulate
- `AEAD` — encrypt, decrypt
- `Hash` — hash, hmac, kdf
- `RNG` — fill_random

A `CryptoProvider` object bundles all five interfaces for a given Suite ID. Runtime obtains a provider via `CryptoRegistry::get_suite(0x0000)`.

### 4. Keypair is opaque
`Keypair` is a type-erased handle. Its internal representation is known only to the concrete provider. This ensures Suite ID migration does not require schema changes to `node_store`.

### 5. No global RNG
Every operation that needs randomness takes an explicit `RngRef` parameter. This enables deterministic testing and per-session RNG isolation.

## Interfaces

```cpp
using CryptoSuiteID = uint16_t;

struct Keypair {    // type-erased, ~32 bytes
    uint8_t data[32];
};

class Signer {
    virtual Result<Keypair> generate_keypair(RngRef rng) = 0;
    virtual Result<std::vector<uint8_t>> sign(
        const Keypair& keypair, std::span<const uint8_t> msg) = 0;
    virtual Result<bool> verify(
        std::span<const uint8_t> pubkey,
        std::span<const uint8_t> msg,
        std::span<const uint8_t> sig) = 0;
};

class KEM {
    virtual Result<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>
        encapsulate(std::span<const uint8_t> pubkey, RngRef rng) = 0;
    virtual Result<std::vector<uint8_t>>
        decapsulate(std::span<const uint8_t> privkey,
                    std::span<const uint8_t> ciphertext) = 0;
};

class CryptoProvider {
    virtual CryptoSuiteID suite_id() const = 0;
    virtual Signer& signer() = 0;
    virtual KEM& kem() = 0;
    virtual AEAD& aead() = 0;
    virtual Hash& hash() = 0;
    virtual RNG& rng() = 0;
};

class CryptoRegistry {
    static CryptoRegistry& global();
    void register_suite(std::unique_ptr<CryptoProvider> provider);
    Result<CryptoProvider*> get_suite(CryptoSuiteID id);
};
```

## Consequences
- Adding a new Suite ID requires only: implement the abstract interfaces, call `register_suite` at startup.
- Runtime code never imports a concrete crypto library header (libsodium, liboqs, etc.).
- All crypto operations are testable with a mock provider that returns deterministic outputs.
- Keypair type-erasure means node_store schema is Suite ID-independent.
- Explicit RngRef enables deterministic replay testing of any crypto operation.
