<p align="center">
  <img src="https://raw.githubusercontent.com/dotlinux26/smoframework/main/docs/logo.svg" alt="SMO" width="96"><br>
  <b>Secure Mesh Operation</b>
</p>

<p align="center">
  <a href="https://github.com/dotlinux26/smoframework/blob/main/LICENSE">
    <img src="https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square" alt="License">
  </a>
  <a href="https://github.com/dotlinux26/smoframework/actions">
    <img src="https://img.shields.io/badge/build-passing-brightgreen?style=flat-square" alt="Build">
  </a>
  <a href="https://github.com/dotlinux26/smoframework/tree/main/docs">
    <img src="https://img.shields.io/badge/docs-SPEC.md-blueviolet?style=flat-square" alt="Docs">
  </a>
  <img src="https://img.shields.io/badge/c%2B%2B-20-00599C?style=flat-square&logo=c%2B%2B" alt="C++20">
  <img src="https://img.shields.io/badge/cmake-3.20+-064F8C?style=flat-square&logo=cmake" alt="CMake">
  <img src="https://img.shields.io/badge/tests-31%2F31-passing-brightgreen?style=flat-square" alt="Tests">
  <img src="https://img.shields.io/badge/status-sprint%202.5-yellow?style=flat-square" alt="Status">
</p>

<p align="center">
  Capability-scoped distributed execution runtime for untrusted mesh environments —<br>
  designed for incident response, fleet operations, and intent-based orchestration.
</p>

---

### 📋 Table of Contents

- [Features](#-features)
- [Crypto Suites](#-crypto-suites)
- [Project Map](#-project-map)
- [Build](#-build)
- [Documentation](#-documentation)
- [License](#-license)

---

### ✨ Features

| Area | Status |
|------|--------|
| **Crypto Suite Freeze** (RFC 0024) — 3 frozen suites | ✅ |
| **Contract Architecture** (RFC 0023) — OpcodeRegistry, ContractID, Factory | ✅ |
| **Storage** — SQLite: session, node, DAG, audit, trust | ✅ |
| **Protocol** — Packet v1, signing, encryption, replay protection | ✅ |
| **Identity & Certificate** — Ed25519 keys, MembershipCertificate, enrollment | ✅ |
| **Transport** — Abstract layer, TCP impl, framing | ✅ |
| **Platform** — Linux Tier 1 (Tier 2: Windows in progress) | 🔶 |
| **Total tests** — 31/31 passing (PQC=ON), 27/27 (PQC=OFF) | ✅ |

---

### 🔐 Crypto Suites

| # | Suite | Hash | Sign | KEM | AEAD |
|---|-------|------|------|-----|------|
| 1 | **Classical** | SHA-256 | Ed25519 | X25519 | XChaCha20-Poly1305 |
| 2 | **Modern** | BLAKE3 | Ed25519 | X25519 | XChaCha20-Poly1305 |
| 3 | **PurePQC** | BLAKE3 | ML-DSA-65 | ML-KEM-768 | XChaCha20-Poly1305 |

All suites share the same AEAD & HKDF (HMAC-SHA256). Suite 3 requires liboqs 0.16.0.

---

### 🗺️ Project Map

```
cmd/          📟 CLI — smo-cli, smo-node, smo-admin, smo-debug
core/         ⚙️  Type system — crypto, intents, opcodes, identity, sessions, contracts, FSM
compiler/     🔨 Intent → DAG compiler (graph, parser, planner, validator, optimizer)
runtime/      🏃 Execution engine, FSM, scheduler, sandbox, worker pool
protocol/     📡 Wire format, packet, signing, encryption, replay, schema
providers/    🧩 Suite providers — Classical, Modern, PurePQC
transport/    🔌 Pluggable transport (TCP, QUIC, relay, serialization)
storage/      💾 Per-node stores (session, trust, audit, DAG, node)
trust/        📊 Trust scoring, decay, exchange, store
acl/          🔐 Capability policy engine, presets, revocation
sdk/          📦 Client library & plugin interface
tooling/      🔍 Tracing, metrics, profiling, audit viewer
tests/        🧪 Unit, integration, mesh, chaos, replay, adversarial
third_party/  📚 Monocypher 4.0.2 (CC0)
```

---

### 🔧 Build

```sh
make configure  # cmake -B build -DWITH_PQC=ON
make build      # cmake --build build -j$(nproc)
make test       # ctest --test-dir build
```

| Flag | Default | Description |
|------|---------|-------------|
| `-DWITH_PQC=ON/OFF` | ON | Toggle post-quantum (liboqs). OFF = no PQC deps, Suites 1–2 only |

> **Requires:** CMake 3.20+, C++20 compiler, OpenSSL dev headers.

---

### 📚 Documentation

| Document | Purpose |
|----------|---------|
| [SPEC.md](SPEC.md) | Precise definitions, invariants, protocols |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Design rationale & decisions |
| [RFC/](RFC/) | Proposals: 0006–0024 |
| [docs/STABILITY.md](docs/STABILITY.md) | Frozen API/ABI registry |

---

### 📄 License

Apache 2.0 — see [LICENSE](LICENSE).

Copyright &copy; 2026 **Nguyen Duc Canh** / Distributed Offensive Technology Solutions Co., Ltd.
