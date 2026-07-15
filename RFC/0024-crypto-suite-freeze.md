# RFC 0024 — Crypto Suite Freeze

| Field | Value |
|---|---|
| Status | Accepted |
| Author | SMO Architecture |
| Date | 2026-07-15 |
| Supersedes | RFC 0009 (Crypto Provider) |

## Summary

SMO freezes exactly **3 Crypto Suites** for the lifetime of Stage 1–2.
No new suites will be added until Stage 3. No algorithms outside these suites
will be implemented.

## The Three Suites

| Suite | Hash | Signing | AEAD | KEM |
|---|---|---|---|---|
| 1 Classical | SHA-256 | Ed25519 | XChaCha20-Poly1305 | X25519 |
| 2 Modern | BLAKE3-256 | Ed25519 | XChaCha20-Poly1305 | X25519 |
| 3 PurePQC | BLAKE3-256 | ML-DSA-65 | XChaCha20-Poly1305 | ML-KEM-768 |

## Design Decisions

### Why SHA-256 in Suite 1 instead of BLAKE3?
Suite 1 = "Classical" = maximum compatibility. SHA-256 is the universal
baseline (Linux kernel, OpenSSL, TPM, HSM, PKCS#11, TLS, X.509, SSH, Git,
Docker, Bitcoin). BLAKE3 lives in Suite 2 as "Modern."

### Why not more suites?
- No SHA-3: Low ecosystem adoption, no hardware support, slower than BLAKE3.
- No BLAKE2: Strictly dominated by BLAKE3.
- No SHA-512: BLAKE3-256 already exceeds SHA-512 security.
- No FN-DSA (FALCON): ML-DSA is the NIST standard.
- No SLH-DSA: Stateless hash-based, useful but not needed for Stage 1–2.

### Why XChaCha20-Poly1305 across all suites?
- Faster than AES-GCM in software.
- No hardware acceleration dependency.
- Nonce misuse resistance (192-bit nonce vs. AES-GCM's 96-bit).
- Consistent across all suites reduces audit surface.

### Why HKDF?
- KEM shared secrets are NEVER used as session keys directly.
- HKDF-extract (salt) + HKDF-expand (info, length) derives independent keys.

## Libraries

| Suite | Hash | Signing | KEM | AEAD |
|---|---|---|---|---|
| 1 | core/crypto/hash/sha256.hpp (native) | Monocypher | Monocypher | Monocypher |
| 2 | Blake3 (third_party/blake3/) | Monocypher | Monocypher | Monocypher |
| 3 | Blake3 (third_party/blake3/) | liboqs | liboqs | Monocypher |

## Third-Party Dependencies

| Library | Version | License | Purpose |
|---|---|---|---|
| BLAKE3 C | 1.3.3 | CC0 | Blake3 hashing (Suite 2, 3) |
| Monocypher | 4.0.2 | CC0 | Ed25519, X25519, XChaCha20-Poly1305 |
| liboqs | 0.16.0 | MIT | ML-DSA, ML-KEM (Suite 3) |

See `third_party/DEPS.md` for update instructions.

## Files Changed

- `core/crypto/fwd.hpp` — HashSuite enum, is_crypto_hash(), is_performance_hash()
- `core/crypto/suite.hpp` — HashSuiteID constants, SuiteInfo.hash_suite field
- `core/crypto/impl.hpp` — PerformanceHashImpl struct
- `core/crypto/hash/sha256.hpp/.cpp` — SHA-256 HashProvider (Suite 1)
- `core/crypto/signer/ed25519_provider.hpp/.cpp` — Ed25519 via Monocypher
- `core/crypto/signer/mldsa_provider.hpp/.cpp` — ML-DSA via liboqs
- `core/crypto/kem/x25519_provider.hpp/.cpp` — X25519 via Monocypher
- `core/crypto/kem/mlkem_provider.hpp/.cpp` — ML-KEM via liboqs
- `core/crypto/aead/xchacha20_provider.hpp/.cpp` — XChaCha20-Poly1305 via Monocypher
- `core/crypto/kdf/hkdf.hpp/.cpp` — HKDF (RFC 5869) via HashProvider
- `core/crypto/random/getrandom.hpp/.cpp` — Linux getrandom() CSPRNG
- `core/crypto/secure/zeroize.hpp/.cpp` — SecureBuffer + volatile memset
- `core/crypto/secure/secure_compare.hpp/.cpp` — constant-time memcmp
- `providers/suite1_classical/` — Suite 1 registration
- `providers/suite2_modern/` — Suite 2 registration
- `providers/suite3_purepqc/` — Suite 3 registration
- `third_party/monocypher/` — Monocypher 4.0.2 (2 files)
- `third_party/liboqs/CMakeLists.txt` — FetchContent for liboqs 0.16.0
- `third_party/DEPS.md` — Dependency update guide
- `SPEC.md` — §3, §6.4, §16.1–§16.7, §16.8 updated
- `tests/unit/core/crypto/test_crypto.cpp` — 17 tests, all passing
