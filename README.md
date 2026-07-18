<p align="center">
  <img src="docs/logo.svg" width="120" alt="SMO"><br>
  <b>Secure Mesh Operation</b>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square" alt="License"></a>
  <a href="SPEC.md"><img src="https://img.shields.io/badge/docs-SPEC.md-blueviolet?style=flat-square" alt="Docs"></a>
  <a href="https://github.com/D-O-T-Solutions/smoframework"><img src="https://img.shields.io/badge/repo-D--O--T--Solutions%2Fsmoframework-6366f1?style=flat-square&logo=github" alt="GitHub"></a>
  <img src="https://img.shields.io/badge/c++-20-00599C?style=flat-square&logo=c%2B%2B" alt="C++20">
  <img src="https://img.shields.io/badge/status-sprint%2036D-blue?style=flat-square" alt="Sprint 36D">
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
| Crypto Suite Freeze (RFC 0024) — 3 suites | ✅ done |
| Contract Architecture (RFC 0023) | ✅ done |
| Storage — SQLite: session, node, DAG, audit, trust | ✅ done |
| Protocol v1 — packet, signing, encryption, replay | ✅ done |
| Identity & Certificate — Ed25519, enrollment | ✅ done |
| Transport — abstract layer, TCP/UDP, framing | ✅ done |
| Bootstrap Protocol (RFC 0034) — CBOR, slot-based join | ✅ done |
| PKI & Governance (RFC 0033) — 2-tier, mesh FSM, recovery | ✅ done |
| Signature Join Token v2 — CBOR + Ed25519 | ✅ done |
| Mesh Genesis — Stage 0/1, SlotRing, Manifest | ✅ done |
| Runtime Kernel — Pipeline, Dispatcher, EventBus | ✅ done |
| Contract ABI (RFC 0036) — ContextValue, NextAction, lifecycle | ✅ done |
| Runtime Services (RFC 0037) — 15 injected services | ✅ done |
| Execution State Machine (RFC 0038) — 12-step FSM, compensation | ✅ done |
| Contract Registry & Manager (RFC 0040) — lifecycle + metadata | ✅ done |
| Native Contracts — Join, Bootstrap, Governance, Recovery, File, Process | ✅ done |
| Discovery — Ping/pong response, gossip engine | ✅ done |
| Platform — Linux Tier 1 | ✅ stable |

### Crypto Suites

| # | Suite | Hash | Sign | KEM | AEAD |
|---|-------|------|------|-----|------|
| 1 | Classical | SHA-256 | Ed25519 | X25519 | XChaCha20-Poly1305 |
| 2 | Modern | BLAKE3 | Ed25519 | X25519 | XChaCha20-Poly1305 |
| 3 | PurePQC | BLAKE3 | ML-DSA-65 | ML-KEM-768 | XChaCha20-Poly1305 |

### Project Map

```
cmd/          CLI — smo-cli, smo-node, smo-admin, smo-debug
core/
├── runtime/  kernel, dispatcher, contracts (Join, Bootstrap, Governance, Recovery, File, Process)
├── genesis/  SlotRing, Manifest, RecoveryPackage, RootSession, GenesisWizard
├── bootstrap/ CBOR, BootstrapSnapshot, BootstrapProtocol
├── governance/ 2-tier proposals, GovernanceEngine, MeshHealth
├── recovery/ Soft/Hard recovery, RecoverySession, CRL
├── enroll/  JoinToken v2 (CBOR + signature)
├── transport/ TCP (+framing), UDP, TransportRegistry
├── network/  PacketDispatcher, HeartbeatService
├── session/  Session FSM, SessionManager
├── opcode/   OpcodeRegistry
└── ...       crypto, identity, certificate, FSM, storage, authority, mesh, trust
protocol/     wire format, packet, signing, encryption, replay, schema
runtime/      (migrating to core/runtime/)
transport/    high-level TCP, QUIC, relay (partial)
storage/      session, trust, audit, DAG, node stores
trust/        scoring, decay, exchange, store
tests/        unit, integration, mesh, chaos
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
| [RFC/](RFC/) | Proposals 0006–0040 |
| [docs/PLAN.md](docs/PLAN.md) | Sprint roadmap |
| [ARCHITECTURE_SUMMARY.md](ARCHITECTURE_SUMMARY.md) | Architecture overview & status (Vietnamese) |

### License

Apache 2.0 — see [LICENSE](LICENSE).

Copyright (c) 2026 Nguyen Duc Canh / Distributed Offensive Technology Solutions Co., Ltd.
