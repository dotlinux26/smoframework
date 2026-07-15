# Third-Party Dependencies Guide

SMO depends on three third-party crypto libraries, each pinned to a specific
version. This document explains how to update each one.

## 1. BLAKE3 (`third_party/blake3/`)

- **Version:** 1.3.3
- **Source:** https://github.com/BLAKE3-team/BLAKE3
- **License:** CC0 1.0 (public domain)
- **Files:** Official C source with SIMD dispatch (SSE2, SSE4.1, AVX2, AVX512, NEON)
- **Update:** Download tarball from GitHub releases → replace files in `blake3/`.
  Update CMakeLists.txt if new source files are added.

## 2. Monocypher (`third_party/monocypher/`)

- **Version:** 4.0.2
- **Source:** https://github.com/LoupVaillant/Monocypher
- **License:** CC0 1.0 (public domain)
- **Files:** Only `monocypher.h` and `monocypher.c` are needed.
- **Algorithms used:** Ed25519, X25519, XChaCha20-Poly1305.
- **Update:**
  ```bash
  curl -Lo third_party/monocypher/monocypher.h \
    "https://raw.githubusercontent.com/LoupVaillant/Monocypher/refs/tags/<TAG>/src/monocypher.h"
  curl -Lo third_party/monocypher/monocypher.c \
    "https://raw.githubusercontent.com/LoupVaillant/Monocypher/refs/tags/<TAG>/src/monocypher.c"
  ```
  Replace `<TAG>` with the new version tag (e.g. `4.0.3`). Re-run cmake.

## 3. liboqs (`third_party/liboqs/`)

- **Version:** 0.16.0
- **Source:** https://github.com/open-quantum-safe/liboqs
- **License:** MIT
- **Build method:** CMake `FetchContent` — downloaded at configure time.
- **Algorithms used:** ML-DSA-44/65/87 (FIPS 204), ML-KEM-512/768/1024 (FIPS 203).
- **Config:** Only NIST-standardized algorithms are built (see `OQS_MINIMAL_BUILD` in
  `third_party/liboqs/CMakeLists.txt`).
- **Update:** Edit `third_party/liboqs/CMakeLists.txt`, change `GIT_TAG` to the
  new version, and update the `OQS_MINIMAL_BUILD` list if algorithm identifiers
  changed. Re-run cmake to fetch and build.

## Build Order

```
smo_monocypher (C, no deps)
    ↓
smo_blake3 (C, no deps)
    ↓
liboqs (C, downloaded via FetchContent)
    ↓
smo_core (C++, links monocypher + oqs)
    ↓
smo_blake3_provider / smo_suite1_classical / smo_suite2_modern / smo_suite3_purepqc
    ↓
smo_protocol / smo_storage / smo_contract / ...
```

## Why These Libraries?

| Library | Size | Auditability | License | Provided |
|---|---|---|---|---|
| Monocypher | ~1700 LOC, 2 files | Very easy | CC0 | Ed25519, X25519, XChaCha20-Poly1305 |
| BLAKE3 C | ~2000 LOC + SIMD | Moderate | CC0 | BLAKE3-256 hashing |
| liboqs | Large (minimal build ~500KB) | N/A (relies on NIST) | MIT | ML-DSA, ML-KEM |

Monocypher was chosen over libsodium because:
- 2 files vs. libsodium's 200+ files — trivially auditable
- CC0 license vs. ISC — no attribution requirement
- No dependencies — works as a drop-in C file

liboqs was chosen for PQC because:
- Only production-ready library for NIST-standardized ML-DSA and ML-KEM
- Backed by NIST, AWS, Cisco, IBM, and others
- Active maintenance and security updates
