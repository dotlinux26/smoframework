<p align="center">
  <img src="docs/logo.svg" width="120" alt="SMO"><br>
  <b>Secure Mesh Operation</b>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square" alt="License"></a>
  <a href="SPEC.md"><img src="https://img.shields.io/badge/docs-SPEC.md-blueviolet?style=flat-square" alt="Docs"></a>
  <img src="https://img.shields.io/badge/c++-20-00599C?style=flat-square&logo=c%2B%2B" alt="C++20">
  <img src="https://img.shields.io/badge/tests-31%2F31-brightgreen?style=flat-square" alt="Tests">
  <img src="https://img.shields.io/badge/build-passing-brightgreen?style=flat-square" alt="Build">
</p>

<p align="center">
  Capability-scoped distributed execution runtime for untrusted mesh environments —
  incident response, fleet operations, intent-based orchestration.
</p>

---

### Features

| Area | Status |
|------|--------|
| Crypto Suite Freeze (RFC 0024) — 3 suites | done |
| Contract Architecture (RFC 0023) | done |
| Storage — SQLite: session, node, DAG, audit, trust | done |
| Protocol v1 — packet, signing, encryption, replay | done |
| Identity & Certificate — Ed25519, enrollment | done |
| Transport — abstract layer, TCP, framing | done |
| Platform — Linux Tier 1 (Windows Tier 2 in progress) | WIP |
| Tests — 31/31 (PQC=ON), 27/27 (PQC=OFF) | done |

### Crypto Suites

| # | Suite | Hash | Sign | KEM | AEAD |
|---|-------|------|------|-----|------|
| 1 | Classical | SHA-256 | Ed25519 | X25519 | XChaCha20-Poly1305 |
| 2 | Modern | BLAKE3 | Ed25519 | X25519 | XChaCha20-Poly1305 |
| 3 | PurePQC | BLAKE3 | ML-DSA-65 | ML-KEM-768 | XChaCha20-Poly1305 |

### Project Map

```
cmd/          CLI — smo-cli, smo-node, smo-admin, smo-debug
core/         crypto, intents, opcodes, identity, sessions, contracts, FSM
compiler/     Intent → DAG: graph, parser, planner, validator, optimizer
runtime/      execution engine, FSM, scheduler, sandbox, worker pool
protocol/     wire format, packet, signing, encryption, replay, schema
providers/    suite providers — Classical, Modern, PurePQC
transport/    TCP, QUIC, relay, serialization
storage/      session, trust, audit, DAG, node stores
trust/        scoring, decay, exchange, store
acl/          policy engine, presets, revocation
sdk/          client library & plugin interface
tooling/      tracing, metrics, profiling, audit viewer
tests/        unit, integration, mesh, chaos, replay, adversarial
third_party/  Monocypher 4.0.2 (CC0)
```

### Build

```
make configure   # cmake -B build -DWITH_PQC=ON
make build       # cmake --build build -j$(nproc)
make test        # ctest --test-dir build
```

| Flag | Default | Description |
|------|---------|-------------|
| `-DWITH_PQC=ON/OFF` | ON | Toggle liboqs. OFF = Suites 1–2 only, no PQC deps |

Requires: CMake 3.20+, C++20 compiler, OpenSSL dev headers.

### Docs

| Document | Purpose |
|----------|---------|
| [SPEC.md](SPEC.md) | Definitions, invariants, protocols |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Design rationale |
| [RFC/](RFC/) | Proposals 0006–0024 |
| [docs/STABILITY.md](docs/STABILITY.md) | Frozen API/ABI |

### License

Apache 2.0 — see [LICENSE](LICENSE).

Copyright (c) 2026 Nguyen Duc Canh / Distributed Offensive Technology Solutions Co., Ltd.
