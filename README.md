<p align="center">
  <img src="docs/logo.svg" width="140" alt="SMO"><br>
  <b>Secure Mesh Operation</b>
</p>

<p align="center">
  <em>Capability-scoped distributed execution runtime for untrusted mesh environments</em>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square" alt="License"></a>
  <a href="SPEC.md"><img src="https://img.shields.io/badge/docs-SPEC.md-blueviolet?style=flat-square" alt="Specification"></a>
  <a href="RFC/"><img src="https://img.shields.io/badge/rfc-0001%E2%80%930044-8b5cf6?style=flat-square" alt="RFCs"></a>
  <a href="https://github.com/D-O-T-Solutions/smoframework/releases/tag/v0.0.1-rc"><img src="https://img.shields.io/badge/release-v0.0.1--rc-f97316?style=flat-square" alt="Release"></a>
  <img src="https://img.shields.io/badge/c%2B%2B-20-00599C?style=flat-square&logo=c%2B%2B" alt="C++20">
  <img src="https://img.shields.io/badge/build-19%2F19%20tests-brightgreen?style=flat-square" alt="19/19 tests">
</p>

---

## Overview

SMO is a **post-quantum secure**, **capability-scoped** distributed execution runtime designed for **untrusted mesh environments**. Nodes communicate via signed contracts, not RPC. Every operation is scoped by capability, audited by state machines, and secured by choice of classical, modern, or pure-PQC crypto suites.

**Use cases:**
- Incident response & fleet ops at scale
- Intent-based orchestration across untrusted networks
- Distributed contract execution with cryptographic attestation
- Mesh governance with PKI, CRL, epoch-based authority rotation

---

## Quick Start

```bash
make configure   # cmake -B build -DWITH_PQC=ON
make build       # cmake --build build -j$(nproc)
make test        # ctest --test-dir build --output-on-failure
```

**Result:** 19 protocol compliance tests green, E2E smoke test pass.

```bash
build/cmd/smo-node/smo-node --help
build/cmd/smo-cli/smo-cli   --help
build/cmd/smo-admin/smo-admin --help
```

| Option | Default | Description |
|--------|---------|-------------|
| `-DWITH_PQC=ON/OFF` | ON | Include ML-DSA-65 / ML-KEM-768 post-quantum suites. OFF = classical only, no liboqs |

**Dependencies:** CMake 3.20+, C++20 compiler, OpenSSL dev headers, liboqs (optional, auto-fetched).

---

## Architecture

```
                    ┌──────────────────────────┐
                    │   Runtime Kernel         │
                    │  Pipeline │ Dispatcher    │
                    └──────────┬───────────────┘
                               │
         ┌─────────────────────┼─────────────────────┐
         │                     │                     │
   ┌─────▼──────┐       ┌─────▼──────┐       ┌─────▼──────┐
   │  Protocol   │       │    Core    │       │  Storage   │
   │  v1 Wire    │       │            │       │  SQLite    │
   │  Sign/Enc   │       │ Identity   │       │  Session   │
   │  Replay     │       │ Certificate│       │  Trust     │
   └─────────────┘       │ PKI/Gov    │       │  Audit     │
                         │ Bootstrap  │       │  DAG       │
   ┌──────────────────────┤ Discovery  │       └────────────┘
   │  3 Crypto Suites    │ Gossip     │
   │  Classical (1)      │ Sessions   │       ┌─────────────────┐
   │  Modern   (2)       │ Transport  │       │  Native Contracts│
   │  PurePQC  (3)       │ FSM        │       │  Join, Bootstrap │
   └──────────────────────┤ Contracts  │       │  Governance, ... │
                         │ Runtime    │       └─────────────────┘
                         └────────────┘
```

### Key Components

| Layer | Component | Description |
|-------|-----------|-------------|
| **Protocol** | Packet v1 | Fixed header + variable payload, AEAD-encrypted, Ed25519-signed |
| | Signing/Encryption | Pluggable per crypto suite (classical/modern/PQC) |
| | Replay Protection | Nonce + sliding window |
| **Core** | Identity | Ed25519 keypair, NodeID = Blake3(pubkey) |
| | Certificate Chain | Leaf → Authority → Root, timelock + epoch |
| | Governance | 2-tier (Membership/Constitution), epoch-based proposals |
| | Bootstrap | CBOR snapshot, slot-based join, delta sync |
| | Discovery | Ping/pong, SWIM-inspired gossip engine |
| | FSM | Generic FSM + NodeLifecycleFSM + JoinFSM (23+12+17 states) |
| **Crypto** | Suite 1 (Classical) | SHA-256, Ed25519, X25519, XChaCha20-Poly1305 |
| | Suite 2 (Modern) | BLAKE3, Ed25519, X25519, XChaCha20-Poly1305 |
| | Suite 3 (PurePQC) | BLAKE3, ML-DSA-65, ML-KEM-768, XChaCha20-Poly1305 |
| **Runtime** | Kernel | Pipeline orchestrator, EventBus, middleware |
| | Contracts | 7 native: Echo, Bootstrap, Join, Governance, Recovery, File, Process |
| | Services | 15 injected: crypto, network, storage, scheduler, identity, vault, audit, metrics, clock, history, logger, file, transport, random, storage |
| **Storage** | SqliteStore | Session, node, DAG, audit, trust — all Go-binary → CBOR |
| **CLI** | smo-cli | Interactive shell with intent parser |
| | smo-node | Node daemon (discovery, gossip, sync, runtime) |
| | smo-admin | Mesh administration (create, join, list, export) |

---

## Project Structure

```
cmd/            CLI daemons & tooling
├── smo-cli/    Interactive shell
├── smo-node/   Mesh node daemon
├── smo-admin/  Mesh administration
└── smo-debug/  Debug utility

core/           Core runtime library
├── runtime/    Kernel, dispatcher, contracts
├── genesis/    SlotRing, Manifest, RecoveryPackage
├── bootstrap/  CBOR, BootstrapSnapshot, BootstrapProtocol
├── governance/ 2-tier proposals, GovernanceEngine
├── recovery/   Soft/hard recovery, CRL
├── enroll/     JoinToken v2 (CBOR + Ed25519)
├── transport/  TCP (+framing), UDP, TransportRegistry
├── network/    PacketDispatcher, HeartbeatService, sync
├── fsm/        Generic FSM, NodeLifecycleFSM
├── crypto/     Suite registry, KEM, signer providers
├── identity/   NodeID, Identity
├── certificate/ Certificate, CertificateChain, CSR
├── opcode/     OpcodeRegistry
├── storage/    SqliteStore, manifest store
├── discovery/  SWIM gossip engine, MembershipTable
├── session/    Session FSM, SessionManager
├── authority/  Mesh authority, node registry
├── mesh/       MeshManager, MeshConfig
├── contract/   ContractID, ContractDefinition, Registry
└── errors/     Domain error codes & categories

protocol/       Wire protocol
├── packet/     Buffer serialization
├── signing/    Signing abstraction
├── encryption/ AEAD encryption
├── replay/     ReplayProtector (nonce + window)
└── schema/     MessageType, ProtocolVersion

storage/        Storage providers (session, trust, DAG, node)
contract/       Contract ABI, ContextValue, lifecycle
transport/      High-level TCP transport
trust/          Trust scoring, decay, exchange
compiler/       SMIR pipeline, DAG cache
providers/      Crypto suite implementations
tests/          Unit, integration, mesh, chaos, adversarial
third_party/    Monocypher 4.0.2 (CC0), Blake3, SQLite3
```

---

## Crypto Suites

| # | Suite | Hash | Sign | KEM | AEAD |
|---|-------|------|------|-----|------|
| 1 | Classical | SHA-256 | Ed25519 | X25519 | XChaCha20-Poly1305 |
| 2 | Modern | BLAKE3 | Ed25519 | X25519 | XChaCha20-Poly1305 |
| 3 | PurePQC | BLAKE3 | ML-DSA-65 | ML-KEM-768 | XChaCha20-Poly1305 |

Suite negotiation happens at join time via capability bitmap (CAP_DELTA_SYNC, CAP_CRT, CAP_RUNTIME_NEGOTIATE, etc.).

---

## Test Suite

```
 19 tests total, 0 failures (1.9s)

 ── Protocol ─────────────────
 ✓ Protocol model           13 tests (packet, schema, replay)
 ✓ Protocol compliance      17 PCTs (CBOR roundtrip, FSM, gossip, CRL, ...)

 ── Core ────────────────────
 ✓ Trust model              4 tests
 ✓ Governance model         3 tests
 ✓ Discovery model          8 tests
 ✓ Session model            2 tests
 ✓ FSM model                11 tests
 ✓ Certificate model        5 tests
 ✓ Identity model           3 tests
 ✓ Contract model           7 tests
 ✓ Storage model            3 tests
 ✓ Crypto model             3 tests
 ✓ Error model              2 tests

 ── Transport ───────────────
 ✓ Transport model          4 tests
 ✓ High-level transport     2 tests
 ✓ Storage stores           1 test

 ── Legacy stubs ────────────
 ✓ core                     placeholder
 ✓ protocol                 placeholder
 ✓ compiler                 placeholder
```

E2E smoke test:
```bash
bash tests/integration/smoke_test.sh build   # 8/8 checks
```

---

## Documentation

| Document | Purpose |
|----------|---------|
| [SPEC.md](SPEC.md) | Canonical specification (2744 lines) |
| [RFC/](RFC/) | Proposals 0001–0044 |
| [docs/MASTER_SYSTEM_MAP.md](docs/MASTER_SYSTEM_MAP.md) | Complete system map |
| [docs/PLAN.md](docs/PLAN.md) | Sprint roadmap |
| [docs/discussions/](docs/discussions/) | Design discussions 0033–0041 |
| [ARCHITECTURE_SUMMARY.md](ARCHITECTURE_SUMMARY.md) | Vietnamese architecture summary |

---

## Release History

| Tag | Date | Status | Highlights |
|-----|------|--------|------------|
| [v0.0.1-rc](https://github.com/D-O-T-Solutions/smoframework/releases/tag/v0.0.1-rc) | 2026-07 | ✅ Current | Protocol freeze, 19/19 tests, E2E smoke pass |
| v0.0.2 | TBD | 🔄 Planned | Anti-entropy, CI/CD, production hardening |

See [DISCUSSION_0041](docs/discussions/DISCUSSION_0041_v0.0.2_Plan.md) for the v0.0.2 roadmap.

---

## License

Apache 2.0 — see [LICENSE](LICENSE).

Copyright (c) 2026 Nguyen Duc Canh / Distributed Offensive Technology Solutions.
