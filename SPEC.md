# SECURE MESH OPERATION (SMO) — Engineering Specification

Version: 3.0
Canonical Name: Secure Mesh Operation (SMO)
Supersedes: SMF (ShellMap Framework) V3

---

## PREFACE — How to Read This Document

This document is the single source of truth for the SMO system. Every module, interface, state machine, protocol decision, and engineering constraint is defined here. Nothing is inferred from conversation.

Sections are ordered by conceptual dependency, not by implementation order. Read §Implementation Order for the recommended build sequence.

---

## 0. TERMINOLOGY

| Term | Definition |
|---|---|
| SMO | Secure Mesh Operation — the complete distributed execution runtime |
| SMF | ShellMap Framework — the predecessor. Concepts absorbed into SMO |
| Node | A single machine running the SMO runtime, participating in one or more meshes |
| Mesh | A logical domain of participating SMO nodes, identified by a unique MeshID |
| MeshID | Deterministic hash derived at mesh creation: Blake3(RootPublicKey \|\| CreatedAt \|\| Random) |
| Contract | A canonical definition of HOW an intent is fulfilled. Immutable, content-addressed (Blake3), stored in Contract Registry. Four categories: Kernel, Native, Mesh, Private. NEVER carries application data |
| ContractID | Blake3(utf8(canonical_json)) — 64 hex chars. Content-addressed identifier for a contract definition |
| Contract Registry | Immutable, append-only, Blake3-addressed store of contract definitions. Git-like, not Docker-like |
| Contract Factory | Resolves an Intent to a ContractID by querying the Registry and Opcode Registry |
| Opcode Registry | Syscall-table-like registry mapping OpcodeID → handler metadata, capability mask, and arch support |
| Native Contract | Runtime-builtin contract template registered at startup. Cannot be published/deprecated by users |
| User-defined Contract | Published by a mesh participant via Contract Registry. Signed, versioned, auditable |
| Internal Contract | Runtime-only contract, never user-invokable. Created and destroyed within a single session |
| Kernel Contract | System-level trusted contract. Executor runs inline, no sandbox. Opcode range 0x00–0x0F |
| Requester | The node that proposes a contract |
| Responder | The node that receives and executes (or rejects) a contract. Has final decision authority |
| Witness | An independent third node that attests to the contract's existence and integrity. Does not execute |
| Intent | What the user wants to accomplish. Expressed as opcode + targets + parameters. Separate from Contract |
| Compiler | Node-local component that transforms Contract Definition + Parameters + Node Environment → SMIR → ExecutionGraph (DAG). 6-stage pipeline: Parser → Semantic Validator → Planner → Builder → Optimizer → Final Validator |
| SMIR | SMO Intermediate Representation — canonical IR format that decouples input formats (JSON, DSL, YAML) from the DAG pipeline |
| Contract ABI | Canonical interface description: input/output schemas, capability mask, opcode deps, ABI Hash, Semantic Hash, version bounds |
| DAG Cache | Local cache keyed by (ContractID, env_fingerprint). Avoids recompilation on repeated execution |
| Capability | A runtime-validated, session-scoped permission. NOT a static role |
| Opcode | The operation type. Hierarchical namespace: DISCOVERY, CONTROL, EXECUTION, DATA |
| Session | A scoped execution context with associated capabilities and constraints |
| DAG | Directed Acyclic Graph — the immutable execution plan produced by the compiler |
| FSM | Finite State Machine — governs node-level and mesh-level execution transitions |
| Identity | A node's Ed25519 keypair. Immutable for the lifetime of the node |
| Membership Certificate | A signed document linking a node's PublicKey to a MeshID, Role, and CapabilitySet |
| Mesh Root Key | The offline key generated at mesh creation. Used only for bootstrap, recovery, and Authority signing |
| Authority Key | An online key used for daily operations: signing certificates, granting capabilities, revocation |
| Enrollment | The process of delivering a node's PublicKey to an Authority and receiving a Membership Certificate |
| .smor | Enrollment Request file — contains PublicKey, mesh_id, nonce, timestamp, signed by node |
| .smoc | Node Certificate file — contains MeshID, Role, Capabilities, Epoch, signed by Authority |
| Epoch | Mesh-wide counter incremented on major capability changes. Certificates with old Epoch are rejected |
| Join Token | A single-use, time-limited token that authorizes a node to enroll. Distributed out-of-band |
| Recovery Authority | A holder of a Shamir secret share of the Root Key. M-of-N threshold can reconstruct the Root |
| Crypto Suite ID | An identifier that maps to a concrete set of cryptographic algorithms. Protocol references Suite ID, not algorithm names |
| Hash Suite | A first-class identifier (HashSuiteID) that specifies algorithm, digest size, output mode, and version. SMO defines 3 cryptographic suites (BLAKE3-256, SHA-256, SHA3-256) + performance suites (xxHash3, CRC32C, CityHash). Not all hash suites are cryptographic — see §16.6 |
| Connectivity Layer | The layer responsible for NAT traversal, STUN, ICE, hole punching, relay. Produces a connected socket |
| Trust Score | A composite local metric used as one input (not the sole) to execution decisions |
| Citizen Score | A trust sub-component measuring online time, heartbeat stability, route reliability |
| Execution Score | A trust sub-component measuring successful contract completions |
| Witness Score | A trust sub-component measuring witness accuracy and participation |
| Consistency Score | A trust sub-component measuring result agreement with majority |
| Policy | Node-local rules governing which contracts it will accept and how to execute them |

---

### 0.1 Platform Tiers

SMO is Linux-first. Platform-specific APIs (io_uring, getrandom, pidfd, cgroup v2, namespace, seccomp, eBPF) are encapsulated in a `platform/` layer — business logic never calls them directly.

| Tier | Platform | Status | Notes |
|---|---|---|---|
| Tier 1 | Linux x86_64 | Fully supported | Primary target. All features. |
| Tier 1 | Linux ARM64 | Fully supported | Same codebase, arch-specific SIMD. |
| Tier 2 | Windows x64 | Supported (Stage 2) | Via `platform/` abstraction layer. |
| Tier 2 | Windows ARM64 | Supported (Stage 2) | Via `platform/` abstraction layer. |
| Tier 3 | macOS, BSD, others | Community maintained | Ports welcome, not core-maintained. |

**Rules:**
1. Business logic NEVER calls Linux APIs directly. All platform calls go through `platform::*`.
2. Tier 1 must pass the full test suite before each release.
3. Tier 2 must pass the full test suite; CI runs weekly.
4. Tier 3 is community-maintained. Core team does not block on Tier 3 failures.

**Build flags:**

| Flag | Default | Description |
|---|---|---|
| `-DWITH_PQC=ON/OFF` | ON | Build Suite 3 (PQC) with liboqs. Disabling removes all ML-KEM/ML-DSA code — no liboqs dependency. Suites 1–2 still work. |

---

## I. SYSTEM POSITIONING

SMO IS:
- A capability-scoped distributed execution runtime operating on untrusted mesh environments
- A distributed constrained execution environment where contract is the unit of communication
- An incident response runtime for mass operations on Linux (and later, other OS) fleets
- A platform for intent-based orchestration: "describe intent, distribute trust, execute locally"

SMO IS NOT:
- An SSH replacement
- A Kubernetes clone
- A Dropbox-like sync tool
- An RPC framework
- A remote shell
- A distributed bash
- A blockchain or ledger
- A VPN tool

---

## II. CORE PHILOSOPHY

The following philosophical positions anchor every architectural decision in SMO. They are not optional.

1. **Contract ≠ Data.** A contract describes intent, execution constraints, participants, and metadata. It NEVER embeds application data. Data lives outside the contract and is handled by the runtime.

2. **Node sovereignty.** Every node retains the final decision over whether to execute a contract. No node can force another node to execute. Responder evaluates capability, policy, trust, signature, session, and witness attestation before executing.

3. **Trust is local, evidence is shared.** Trust scores are computed locally on each node. Evidence (contract records, heartbeat digests, attestations) is shared across the mesh via gossip and witness. Trust digests are hints, not authoritative values.

4. **No global state.** There is no single global execution state. Each node maintains its own session store, audit store, trust store, and DAG store. Mutable shared execution state is never stored globally.

5. **Execution graph immutable after compile.** Once the compiler produces an execution DAG, the DAG is never mutated. This guarantees that all nodes operate on the same execution plan.

6. **Capabilities, not roles.** The runtime does not know R/W/X as hardcoded levels. It only knows capabilities. Reader / Contributor / Authority are preset groupings of capabilities.

7. **Witness is not consensus.** A witness attests, not decides. Witness does not vote, does not execute, does not arbitrate. It provides independent evidence.

8. **Contract is the unit of communication.** Nodes do not exchange raw function calls. They exchange contracts. The receiving node evaluates and decides.

9. **Identity ≠ Membership ≠ Capability.** A node's keypair (identity) is distinct from its certificate (membership), which is distinct from its currently active capabilities (session-scoped). These layers never mix.

10. **Root Key NEVER circulates.** Only certificates circulate. Private keys never leave their host node.

11. **Protocol describes meaning. Transport describes bytes.** The protocol layer defines what a contract is and how it flows. The transport layer decides whether to send those bytes over TCP, UDP, or a Unix socket. These are independent decisions.

12. **Compilation is node-local.** Every node compiles contract definitions into DAGs independently. No DAG is ever transferred between nodes. This ensures heterogeneous nodes (different plugins, architectures, OS) produce valid execution plans.

---

## III. DESIGN INVARIANTS

These invariants MUST hold at all times across the entire system. Violating an invariant is a specification violation.

### I-01: Contract Purity
A contract MUST NOT embed application data. It contains only execution intent, execution constraints, participant identities, capabilities required, and metadata required by the runtime.

### I-02: Node Final Authority
The Responder node ALWAYS retains the final decision to execute or reject a contract. No external node (Requester, Witness, or any other) can override this decision.

### I-03: Execution Graph Immutability
An execution DAG, once produced by the compiler, MUST NOT be mutated. No runtime component may modify a compiled graph.

### I-04: Deterministic State Transition
Every FSM state transition MUST be deterministic. Given the same input and the same prior state, the transition MUST produce the same output.

### I-05: Auditable Transition
Every FSM state transition MUST be recorded in the audit log. The audit log MUST be sufficient to reconstruct the full execution history of any contract.

### I-06: Replayable Transition
Every FSM state transition MUST be replayable from the audit log. Replay must produce identical state.

### I-07: Serializable Transition
Every FSM state MUST be serializable for storage, transmission, and verification.

### I-08: No Global Mutable State
No component may store mutable shared execution state that is visible to all nodes. All execution state is per-node isolated.

### I-09: Capability Ephemerality
All capabilities MUST be ephemeral and session-scoped. Capabilities are validated at runtime, never assumed from static configuration.

### I-10: Transport Independence
The runtime MUST NOT depend on any specific transport protocol. Transport is interchangeable (TCP, UDP, QUIC, relay, Unix socket).

### I-11: Opcode Replay Safety
Every opcode MUST be replay-safe or explicitly declared as non-idempotent. Replaying an idempotent opcode MUST produce the same side-effect as a single execution.

### I-12: No Silent Mutation
No component may silently mutate state. All state mutations MUST be recorded and auditable.

### I-13: Trust Is Eventually Consistent
Trust scores are NOT absolute truth. They are local estimates that converge over time. No execution decision may depend on trust alone.

### I-14: Private Key Confinement
A node's private key MUST NEVER leave the node on which it was generated. All identity proof is via signed challenges (CSR, signed nonce), never via key transport.

### I-15: Certificate Chain Verification
Every Membership Certificate MUST be verifiable up to the Mesh Root Public Key. A node with only the Root public key can verify any certificate in the chain.

### I-16: Protocol Layer Isolation
No protocol layer (Discovery, Control, Execution, Data) may assume anything about the layers above it. Discovery does not know what a contract is. Control does not know how execution chunks data.

### I-17: Carrier Independence
The Enrollment Protocol defines the format of enrollment requests and responses. It does not specify how they are transported. QR, USB, clipboard, REST API, and file are all valid carriers.

### I-18: Wall Clock Never Trusted for Ordering
System time (`system_clock`) is used ONLY for security windows (anti-replay) and human-readable timestamps. All ordering decisions rely on sequence numbers or logical clocks. All timeouts use monotonic clock (`CLOCK_MONOTONIC`).

### I-19: Every FSM State Has Timeout and Failure Transition
No FSM state may block indefinitely. Every state MUST define a maximum dwell time and a transition to a safe fallback state when the timer expires. The default fallback is REJECTED or OFFLINE.

### I-20: Governance Is Tiered by Impact
Governance decisions are classified into 5 levels (Local/Authority/Policy/Critical/Genesis). Each level has a configurable signature threshold defined in the Mesh Manifest. Level 0 requires no signature (node sovereignty). Level 1 requires 1 Authority. Levels 2-3 require M-of-N. Level 4 (Genesis) uses the Root Key once, then offline.

### I-21: Resource Constraints Are Declared, Not Inferred
Every execution MUST declare its resource requirements (CPU, RAM, timeout, priority) at contract submission time. The Responder uses these declarations for scheduling and enforcement. Undeclared resource use is bounded by the node's default quota.

### I-22: Governance History Is Append-Only
Governance decisions, once committed, are never removed or altered. Corrections to prior governance decisions are themselves new governance decisions that reference the decisions they supersede.

### I-23: Discovery Engine Is Separate from Transport
The Discovery Engine handles peer discovery, health monitoring, and membership propagation. Transport only provides `send()` and `recv()` abstractions. These components MUST NOT be coupled.

### I-24: Local Compilation
Every node MUST compile contract definitions locally. DAGs MUST NEVER be transferred between nodes. Compilation is deterministic given the same inputs, but inputs vary per node (plugins, architecture, OS, policy).

### I-25: Registry Immutability
The Contract Registry MUST be append-only. No contract definition may be deleted or overwritten once published. A new version is a new ContractID.

### I-26: Intent ≠ Contract
An Intent is what a user wants. A Contract is how it is fulfilled. These are separate concepts with separate lifecycles. The Intent layer never touches the Contract Registry directly — the Contract Factory mediates.

### I-27: Contract Data Purity
A Contract definition MUST NOT embed application data, session state, or per-invocation parameters. It contains only the canonical execution template, capability requirements, and compiler hints.

---

## IV. SYSTEM LAYERS

The architecture is decomposed into sixteen layers. No layer may bypass the layer below it.

```
┌──────────────────────────────────────────────┐
│  1. CLI / SDK / POLICY ENGINE                 │  Entry points + OPA-like rules (post-MVP)
├──────────────────────────────────────────────┤
│  2. INTENT LAYER                               │  User → Intent (opcode + targets + params)
│                                                │  Contract Factory resolves Intent → ContractID
├──────────────────────────────────────────────┤
│  3. CONTRACT REGISTRY                         │  Immutable, append-only, Blake3-addressed
│     + OPCODE REGISTRY                         │  Syscall-table: OpcodeID → handler metadata
├──────────────────────────────────────────────┤
│  4. EXECUTION COMPILER                        │  Contract + Parameters + Env → DAG (node-local)
│     + DAG CACHE                               │  Keyed by (ContractID, env_fingerprint)
├──────────────────────────────────────────────┤
│  5. DAG SCHEDULER + RUNTIME POLICIES          │  DAG + priority, retry, preemption, quota
├──────────────────────────────────────────────┤
│  6. NODE EXECUTION FSM                        │  Per-node state machine
├──────────────────────────────────────────────┤
│  7. GOVERNANCE PROTOCOL                       │  Multi-sig decisions, policy change, split/merge
├──────────────────────────────────────────────┤
│  8. TRUST ENGINE                              │  Attestation, scoring, weighting
├──────────────────────────────────────────────┤
│  9. PROTOCOL LAYER                            │
│   ├── Discovery Protocol (UDP)                │  HELLO, PING, DISCOVER, HEARTBEAT
│   ├── Control Protocol (TCP)                  │  CONTRACT, SESSION, WITNESS, CSR, GOVERNANCE
│   ├── Execution Protocol (TCP)                │  EXEC_START, PROGRESS, RESULT, CANCEL
│   └── Data Protocol (TCP)                     │  CHANNEL_OPEN, CHUNK, ACK, FIN
├──────────────────────────────────────────────┤
│ 10. DISCOVERY ENGINE + ROUTING                │  SWIM gossip, bootstrap, Peer Record, path selection
├──────────────────────────────────────────────┤
│ 11. SESSION LAYER                              │  Keys, certificate binding, signed nonce, version negotiation
├──────────────────────────────────────────────┤
│ 12. CONNECTIVITY LAYER                        │  STUN, ICE, NAT traversal, hole punch, TURN relay
├──────────────────────────────────────────────┤
│ 13. TRANSPORT ABSTRACTION                     │  Interchangeable (TCP, UDP, future: QUIC, Unix socket)
├──────────────────────────────────────────────┤
│ 14. RESOURCE MODEL                            │  CPU/RAM/IO limits, cgroup, namespace, seccomp
├──────────────────────────────────────────────┤
│ 15. PLUGIN ABI + WASM (future)                │  C ABI plugin interface
├──────────────────────────────────────────────┤
│ 16. IDENTITY + MEMBERSHIP LAYER               │  Keypairs, lifecycle, certificates, multi-tenant, manifest
└──────────────────────────────────────────────┘
```

---

## V. PROJECT ARCHITECTURE (REPOSITORY STRUCTURE)

```
smo/
├── cmd/
│   ├── smo-cli/             User-facing execution tool (§XIX)
│   ├── smo-node/            Node daemon — FSM, session, capability enforcement
│   ├── smo-admin/           Mesh administration (§XIX)
│   └── smo-debug/           Internal tracing and debugging
│
├── core/                    Pure interfaces/type definitions only. NO business logic.
│   ├── contract/            Contract definition types, ContractID (§X)
│   ├── intent/              Intent type definitions (§X)
│   ├── opcode/              Opcode enumeration (§XVIII) + OpcodeRegistry interface (§XIX.8)
│   ├── capability/          Capability type definitions (§VIII)
│   ├── session/             Session type definitions
│   ├── crypto/              Algorithm-agnostic crypto interface + providers (§XVI)
│   │   ├── hash_provider.hpp    Abstract HashProvider interface
│   │   ├── fwd.hpp              CryptoSuiteID, HashSuiteID, HashSuite enum, EncapsResult
│   │   ├── suite.hpp            Suite ID constants (Classical=1, HybridPQC=2, PurePQC=3)
│   │   ├── impl.hpp             CryptoProvider struct: RNG + Hash + PerfHash + AEAD + KEM + Signer
│   │   ├── registry.hpp/.cpp    CryptoRegistry singleton
│   │   ├── hash/                SHA-256 HashProvider
│   │   ├── signer/              Ed25519 (Monocypher) + ML-DSA (liboqs)
│   │   ├── kem/                 X25519 (Monocypher) + ML-KEM (liboqs)
│   │   ├── aead/                XChaCha20-Poly1305 (Monocypher)
│   │   ├── kdf/                 HKDF
│   │   ├── random/              getrandom() CSPRNG
│   │   └── secure/              Zeroize + constant-time compare
│   ├── state/               State type definitions (§XIV)
│   ├── identity/            NodeIdentity, Ed25519 keypair generation, nonce signing
│   ├── mesh/                MeshID, MeshGenesis, Epoch, MembershipCertificate
│   ├── enroll/              EnrollRequest, EnrollResponse, ExportFormat
│   └── errors/              Error type definitions
│
├── contract/                Contract subsystem
│   ├── registry/            Contract Registry implementation (immutable, append-only)
│   ├── factory/             Contract Factory — Intent → ContractID resolution
│   └── compiler/            Contract → DAG compiler (node-local) + DAG cache
│
├── compiler/                (Legacy — contracts moved to contract/compiler/)
│   ├── parser/              Contract source parser
│   ├── planner/             Node planning and resource mapping
│   ├── optimizer/           DAG optimization (future)
│   ├── graph/               DAG data structures
│   └── validator/           Semantic validation
│
├── runtime/                 Core execution engine
│   ├── scheduler/           DAG-aware scheduler (NOT a FIFO queue)
│   ├── executor/            Actual opcode execution dispatch
│   ├── sandbox/             Execution isolation (future WASM, resource quotas)
│   ├── workerpool/          Concurrent worker management
│   ├── fsm/                 Finite state machine implementations
│   │   ├── node_fsm/        Per-node execution FSM (§XIV)
│   │   ├── mesh_fsm/        Multi-node coordination FSM
│   │   ├── consensus_fsm/   Witness/consensus FSM
│   │   └── transitions/     State transition definitions
│   ├── audit/               Execution audit logging
│   └── recovery/            State recovery and reconcile
│
├── protocol/
│   ├── discovery/           Discovery protocol — HELLO, PING, DISCOVER, HEARTBEAT
│   ├── control/             Control protocol — CONTRACT, SESSION, CSR, WITNESS, REVOKE
│   ├── execution/           Execution protocol — EXEC_START, PROGRESS, RESULT, CANCEL
│   ├── data/                Data protocol — CHANNEL_OPEN, CHUNK, ACK, FIN
│   ├── packet/              Packet structure and zero-copy parsing
│   ├── schema/              Schema definitions
│   ├── signing/             Cryptographic signing and verification (§XVI)
│   ├── encryption/          Payload encryption (§XVI)
│   └── replay/              Replay protection (nonce, timestamp)
│
├── transport/
│   ├── tcp/                 TCP transport implementation
│   ├── udp/                 UDP transport implementation (discovery, heartbeat)
│   ├── stun/                STUN client (RFC 8489)
│   ├── ice/                 ICE candidate gathering (RFC 8445)
│   ├── nat/                 UDP hole punch
│   ├── relay/               TURN relay (RFC 8656)
│   ├── enrollment/          QR generation, format export/import
│   ├── certificate/         Unified ImportCertificate() with format auto-detection
│   ├── framing/             Message framing
│   └── serialization/       Wire serialization/deserialization
│
├── consensus/               Multi-node coordination
│   ├── witness/             Witness selection and protocol
│   ├── attestation/         Attestation collection and verification
│   └── weighting/           Trust-weighted decision support
│
├── trust/                   Trust engine (§XII)
│   ├── scoring/             Trust score computation
│   ├── decay/               Score decay over time
│   ├── store/               Trust data persistence
│   └── exchange/            Trust digest gossip
│
├── acl/                     Access control (§VIII)
│   ├── policy/              Local policy engine (capability resolution)
│   ├── presets/             Predefined capability groupings
│   └── revocation/          Capability revocation
│
├── storage/                 Local persistent storage (§XV)
│   ├── session_store/       Active session state
│   ├── trust_store/         Trust score records
│   ├── audit_store/         Audit log
│   ├── dag_store/           Execution DAG records
│   ├── node_store/          Node identity, keys, configuration, route table
│   └── mesh_store/          Mesh memberships, certificates, epoch
│
├── sdk/                     Client and plugin SDK
│   ├── client/              API client library
│   └── plugin/              Plugin authoring interfaces
│
├── tooling/                 Observability and developer tools (§XXI)
│   ├── tracing/             Distributed tracing
│   ├── metrics/             Performance metrics
│   ├── profiling/           CPU/memory profiling
│   └── audit-viewer/        Audit log browser
│
├── providers/               Concrete crypto provider implementations
│   ├── blake3_provider/     Blake3 HashProvider (default SMO native)
│   ├── suite1_classical/    Suite 1 Classical: SHA-256 + Ed25519 + X25519 + XChaCha20
│   ├── suite2_modern/       Suite 2 Modern:  BLAKE3 + Ed25519 + X25519 + XChaCha20
│   ├── suite3_purepqc/      Suite 3 PurePQC: BLAKE3 + ML-DSA + ML-KEM + XChaCha20
│   ├── openssl_sha256/      (future) SHA-256 for FIPS environments
│   ├── openssl_sha3/        (future) SHA-3 for NIST/post-quantum readiness
│   └── perfhash/            (future) Performance hash providers: xxHash3, CRC32C, CityHash
│
├── plugins/                 External plugin directory (not built in)
│
├── docs/                    Documentation
├── tests/
│   ├── unit/                Unit tests
│   ├── integration/         Integration tests
│   ├── mesh/                Multi-node mesh tests
│   ├── chaos/               Chaos simulations (delay, split-brain, compromise)
│   ├── replay/              Deterministic replay tests
│   └── adversarial/         Security adversarial tests
│
├── examples/                Example contracts and configurations
└── deployments/             Deployment configurations and templates
```

---

## VI. NETWORKING ARCHITECTURE

### 6.1 Four Protocol Layers

The SMO networking stack defines exactly four protocol layers, each with a distinct responsibility:

```
┌─────────────────────────────────────────────┐
│ DISCOVERY PROTOCOL  (UDP)                    │
│ HELLO · DISCOVER · NODE_INFO                │
│ HEARTBEAT · PING · OFFLINE                  │
├─────────────────────────────────────────────┤
│ CONTROL PROTOCOL     (TCP)                   │
│ CONTRACT · SESSION · CSR · WITNESS          │
│ REVOKE_CERT · EPOCH_INCREMENT               │
│ TRUST_DIGEST · CAP_GRANT · CAP_REVOKE       │
├─────────────────────────────────────────────┤
│ EXECUTION PROTOCOL   (TCP)                   │
│ EXEC_START · EXEC_PROGRESS · EXEC_EVENT     │
│ EXEC_RESULT · EXEC_CANCEL · EXEC_TIMEOUT    │
├─────────────────────────────────────────────┤
│ DATA PROTOCOL        (TCP)                   │
│ CHANNEL_OPEN · CHUNK · ACK · NACK · FIN     │
└─────────────────────────────────────────────┘
```

**Layer isolation rule:** A layer MUST NOT assume anything about the layers above it. Discovery does not know what a contract is. Control does not know how execution chunks data.

### 6.2 Transport Assignment

| Layer | Transport | Rationale |
|---|---|---|
| Discovery | UDP | Connectionless, broadcast, NAT traversal, low overhead. No state needed. |
| Control | TCP | Reliable, ordered, stream-oriented. Contracts must not be lost or reordered. |
| Execution | TCP | Stateful, long-lived, progress reports must be in order. |
| Data | TCP | Bulk transfer, flow control, independent stream. |

### 6.3 Connectivity vs Transport

SMO distinguishes between Connectivity and Transport:

**Transport layer**: Raw byte movement (TCP, UDP, Unix socket). Stateless. Pluggable.

**Connectivity layer**: NAT traversal, address discovery, hole punching, relay. Produces a connected socket.

```
CONNECTIVITY LAYER
  STUN (RFC 8489)        — discover public address
  ICE  (RFC 8445)        — gather and exchange candidates
  NAT hole punch          — open UDP mappings for direct P2P
  TURN (RFC 8656)        — relay fallback when direct connection fails
       │
       ▼  (produces a connected socket)
TRANSPORT LAYER
  TCP  — reliable streams
  UDP  — connectionless datagrams
```

**Key rule:** The Runtime and Protocol layers never touch Connectivity code. They receive a connected socket from the layer below.

### 6.4 Crypto Suite ID

SMO uses a **Crypto Suite ID** to achieve algorithm agility. The protocol NEVER references specific algorithms. It references Suite IDs. The mapping from Suite ID to concrete algorithms is a local configuration.

| Suite ID | Name | Identity/Signing | Key Exchange | Symmetric | Hash Suite |
|---|---|---|---|---|---|---|
| 1 | Classical | Ed25519 | X25519 | XChaCha20-Poly1305 | SHA-256 (ID=2) |
| 2 | Modern | Ed25519 | X25519 | XChaCha20-Poly1305 | BLAKE3-256 (ID=1) |
| 3 | PurePQC | ML-DSA-65 | ML-KEM-768 | XChaCha20-Poly1305 | BLAKE3-256 (ID=1) |

**Rules:**
- Every node MUST implement Suite 1 (minimum baseline).
- A node MAY implement additional suites.
- During session establishment, nodes negotiate the highest mutually supported suite.
- The suite ID is part of the connection handshake, NOT part of every packet.

### 6.5 Hierarchical Opcode Namespace

All protocol messages use a two-level namespace:

```
Namespace      Byte   Messages
──────────     ────   ────────
DISCOVERY      0x01   HELLO(0x01), DISCOVER(0x02), NODE_INFO(0x03),
                      PING(0x04), OFFLINE(0x05)

CONTROL        0x02   CONTRACT_PROPOSAL(0x01), CONTRACT_ACCEPT(0x02),
                      CONTRACT_REJECT(0x03), CONTRACT_RESULT(0x04),
                      SESSION_OPEN(0x10), SESSION_CLOSE(0x11),
                      SESSION_RENEW(0x12),
                      CSR(0x20), CERTIFICATE(0x21),
                      WITNESS_REQUEST(0x30), WITNESS_RESPONSE(0x31),
                      CAP_GRANT(0x40), CAP_REVOKE(0x41),
                      REVOKE_CERT(0x50), EPOCH_INCREMENT(0x51),
                      TRUST_DIGEST(0x60)

EXECUTION      0x03   EXEC_START(0x01), EXEC_PROGRESS(0x02),
                      EXEC_EVENT(0x03), EXEC_RESULT(0x04),
                      EXEC_CANCEL(0x05), EXEC_TIMEOUT(0x06),
                      EXEC_ERROR(0x07)

DATA           0x04   CHANNEL_OPEN(0x01), CHUNK(0x02),
                      ACK(0x03), NACK(0x04), FIN(0x05), CANCEL(0x06)
```

### 6.6 Packet Format

Every wire message uses the following packet structure:

```
+--------------------------------------------------+
| HEADER                                           |
+--------------------------------------------------+
| PROTOCOL VERSION (1 byte)                        |
| SUITE ID      (1 byte)                           |
| NAMESPACE     (1 byte)  — DISCOVERY/CONTROL/...  |
| MESSAGE ID    (2 bytes) — opcode within namespace |
| SESSION ID    (16 bytes)                         |
| TIMESTAMP     (8 bytes)                          |
| NONCE         (8 bytes)                          |
+--------------------------------------------------+
| PAYLOAD        (variable length)                  |
+--------------------------------------------------+
| SIGNATURE      (64 bytes — Suite 1: Ed25519)     |
+--------------------------------------------------+
```

### 6.7 Wire Protocol Rules

1. Every packet MUST carry a nonce for replay protection.
2. Every packet MUST be signed by the sender using the negotiated Suite's signing algorithm.
3. Every packet MUST carry a timestamp. Receivers MAY reject packets outside a configurable time window.
4. Payload MAY be encrypted at the session level. Header fields are never encrypted.
5. Every session MUST negotiate a Crypto Suite during establishment.

### 6.8 Discovery Engine

The Discovery Engine is a dedicated component **separate from Transport**. Transport only knows `send()` and `recv()`. The Discovery Engine handles peer discovery, health monitoring, membership propagation, and route learning.

#### 6.8.1 Architecture

```
┌──────────────────────────────┐
│       DISCOVERY ENGINE        │
│  ┌────────────────────────┐  │
│  │  Membership Table      │  │  Local view of mesh members
│  │  NodeID | State | ...  │  │
│  └───────────┬────────────┘  │
│  ┌───────────▼────────────┐  │
│  │  Gossip Protocol       │  │  SWIM-inspired epidemic broadcast
│  │  (suspicion, indirect) │  │
│  └───────────┬────────────┘  │
│  ┌───────────▼────────────┐  │
│  │  Bootstrap             │  │  Seed nodes, DNS, mDNS, manual
│  └───────────┬────────────┘  │
│  ┌───────────▼────────────┐  │
│  │  Health Monitor        │  │  PING-based liveness, suspicion
│  └───────────┬────────────┘  │
└───────────────┼──────────────┘
                │ uses
                ▼
┌──────────────────────────────┐
│        TRANSPORT (UDP)       │
│    send() / recv()           │
└──────────────────────────────┘
```

#### 6.8.2 Bootstrap Mechanisms

| Method | Mechanism | Use Case |
|---|---|---|
| Seed list | Static list of bootstrap nodes from Mesh Manifest | Production, controlled deployments |
| DNS SRV | `_smo._tcp.mesh-name.example.com` | Enterprise, auto-discovery |
| mDNS | Multicast DNS on local network | LAN, demo, development |
| Manual | `smo node connect --address 10.0.0.1:7777` | Debug, air-gap |

Default: seed list from Mesh Manifest. Nodes in the seed list provide initial peer table upon connection.

#### 6.8.3 Gossip Protocol (SWIM-Inspired)

Based on the SWIM (Scalable Weakly-consistent Infection-style Membership) protocol:

- **Gossip interval**: 1 second (configurable)
- **Target**: 1 random peer per round (configurable fanout)
- **Suspicion timeout**: 5 seconds before declaring a node SUSPECT
- **Indirect ping**: If direct PING fails, ask k random peers to indirect-ping the target
- **Membership update**: Gossip includes membership changes (join, leave, suspect, confirm)

**Gossip payload:**

```
gossip_message:
  sender_id: NodeID
  sequence: uint64                    # monotonic per sender
  entries:
    - node_id: NodeID
      state: ALIVE | SUSPECT | CONFIRMED | DEPARTED
      incarnation: uint64             # bump on state change
      address: optional<SocketAddr>
      trust_digest: optional<bytes>   # compressed trust score
```

#### 6.8.4 Passive Discovery

Nodes learn about peers from:
1. Gossip messages forwarded by other nodes
2. Heartbeat responses containing additional peer references
3. Offline announcement messages (gives advance notice of peer departure)

#### 6.8.5 Leave Detection

| Signal | Action |
|---|---|
| Graceful OFFLINE message | Immediate removal from membership table |
| 3 consecutive PING timeouts | Mark SUSPECT, begin indirect ping |
| Indirect ping confirms failure | Mark CONFIRMED, gossip departure |
| SWIM suspicion timeout expires | Mark CONFIRMED, remove from active set |

### 6.9 Routing Layer

SMO is not a VPN, but nodes must know how to reach each other. The Routing Layer maintains peer connectivity metadata and selects the best path.

#### 6.9.1 Peer Record

Every known peer has a structured record:

```
PeerRecord:
  node_id: NodeID
  mesh_id: MeshID
  addresses:
    - transport: TCP | UDP | QUIC
      addr: "1.2.3.4:7777"
      type: DIRECT | RELAY | LOCAL
      cost: uint8                    # lower = preferred
      last_seen: Timestamp
    - transport: TCP
      addr: "relay-alpha:7777"
      type: RELAY
      cost: 10
      via: relay-node-id
  latency_ms: optional<uint16>
  nat_type: NONE | FULL_CONE | RESTRICTED | SYMMETRIC
  trust_score: float (0.0–1.0)
  last_heartbeat: Timestamp
```

#### 6.9.2 Path Selection Algorithm

```
For each contract requiring delivery:
  1. Lookup PeerRecord for target NodeID
  2. Filter addresses by:
     a. Currently reachable (last_heartbeat within timeout)
     b. Transport matches protocol requirement
     c. Cost <= configured maximum
  3. Sort candidates by (cost + latency_weight * latency_ms)
  4. Select lowest-cost candidate
  5. If connection fails, try next candidate
  6. If all candidates fail, return "unreachable"
```

#### 6.9.3 Dynamic Re-Routing

If the active path degrades (latency spikes, packet loss > threshold):
1. Scheduler MAY request a path re-evaluation
2. Routing Layer selects the next-best candidate
3. Session is transparently migrated if the transport supports connection migration (QUIC) or re-established (TCP)

#### 6.9.4 MVP Routing

MVP uses a simplified routing model:
- Direct TCP connections only (no relay, no NAT traversal)
- Single address per node (no multi-address)
- No path scoring
- Connection failure = unreachable

### 6.10 Version Negotiation

Different nodes may run different SMO versions. The first bytes of every connection negotiate the protocol version.

#### 6.10.1 Handshake

```
Node A connects to Node B:

1. Node A sends: [PROTOCOL_VERSION_REQUEST]
   - supported_versions: [3.2, 3.1, 3.0]
   - node_id: NodeID_A
   - nonce: 32 random bytes

2. Node B responds: [PROTOCOL_VERSION_RESPONSE]
   - selected_version: 3.1         // highest common version
   - supported_versions: [3.1, 3.0]
   - node_id: NodeID_B
   - nonce: 32 random bytes
   - signature: signed(Node A nonce || B nonce || selected_version)

3. Node A verifies signature, responds:
   - [VERSION_CONFIRMED]
   - signature: signed(B nonce || A nonce || selected_version)
```

If no common version exists → connection REJECTED.

#### 6.10.2 Version-Dependent Behavior

| Version | Frame Format | Opcode Set | Crypto Suites |
|---|---|---|---|
| 3.0 | MVP binary (§6.6) | MVP subset | Suite 1 only |
| 3.1 | Extended binary | Full set | Suite 1, 2 |
| 3.2+ | TBD | TBD | Suite 1, 2, 3 |

#### 6.10.3 Compatibility Policy

- Nodes MUST support their own version and N-2 previous versions.
- A node running 3.2 can connect to nodes running 3.0, 3.1, or 3.2.
- Obsolete versions are announced via governance (EPOCH_INCREMENT-like mechanism for protocol versions).

### 6.11 Time Model

This is one of the most critical architectural decisions. Wall clock (`system_clock`) is NOT safe for distributed ordering. SMO divides time into three domains.

#### 6.11.1 Three Time Domains

| Domain | Clock Source | Purpose | Rules |
|---|---|---|---|
| Security Time | Wall clock / NTP | Certificate validity, contract timestamps, anti-replay window | ±300s tolerance on receiver. Used only for bounding, never for ordering. |
| Execution Time | Monotonic clock (`CLOCK_MONOTONIC`) | Timeouts, deadlines, execution duration | Immune to NTP jumps. Never decreases. Basis for all FSM timers. |
| Ordering | Sequence number / logical clock | Event ordering, DAG execution order, audit log replay | Never depends on wall clock. Sequence numbers are per-sender, monotonically increasing. |

#### 6.11.2 Rules

1. **No FSM transition or ordering decision may depend on wall clock.** All timeouts use monotonic clock. All ordering uses sequence numbers.
2. **Wall clock is used ONLY for:**
   - Certificate `notBefore` / `notAfter` validation
   - Contract `created_at` / `timestamp` for human auditing
   - Anti-replay window (±300s from receiver's clock)
3. **Timestamp tolerance:** Receivers MUST accept timestamps within ±300s of their local clock. Packets outside this window are rejected as potential replay or clock drift.
4. **Sequence numbers are per-sender**, monotonically increasing. Gaps indicate lost packets (UDP) or out-of-order delivery (handled by TCP). Duplicate sequence numbers are silently dropped.
5. **Monotonic clock is used for:**
   - FSM state timeouts (maximum dwell time per state)
   - Execution deadlines
   - Heartbeat intervals
   - Gossip round timing

#### 6.11.3 Anti-Replay Without Wall Clock

Sequence numbers provide ordering; timestamps provide a bounded security window:

```
receive(packet):
  # Step 1: Sequence number check (primary anti-replay)
  if packet.seq <= last_seen_seq[sender]:
    DROP  # duplicate or replayed

  # Step 2: Timestamp check (bounded window)
  drift = abs(packet.timestamp - now())
  if drift > MAX_TIMESTAMP_DRIFT (300s):
    DROP  # possible replay or clock attack

  # Step 3: Accept
  last_seen_seq[sender] = packet.seq
  PROCESS(packet)
```

---

## VII. MESH IDENTITY & TRUST INFRASTRUCTURE (MITI)

### 7.1 Three Distinct Concepts

```
┌──────────────────────────────────────────────────┐
│ IDENTITY                                          │
│ "Tôi là node X."                                  │
│ NodeID = HashProvider::hash_hex(NodePublicKey)    │
│ (default: BLAKE3-256 → 64 hex chars)              │
│ Immutable for the lifetime of the node.           │
├──────────────────────────────────────────────────┤
│ MEMBERSHIP                                        │
│ "Tôi thuộc Mesh Y với vai trò Z."                │
│ MembershipCertificate, signed by Authority.       │
│ Per-mesh. One node can hold multiple memberships. │
├──────────────────────────────────────────────────┤
│ CAPABILITY                                        │
│ "Trong Mesh Y, tôi được phép làm gì."            │
│ CapabilitySet derived from Role in certificate.   │
│ Validated at runtime per session.                 │
└──────────────────────────────────────────────────┘
```

### 7.2 Mesh Genesis

A mesh is defined by its **Mesh Genesis** — a deterministic ID derived at creation.

```
MeshID = HashProvider::hash_hex(
    MeshRootPublicKey ||
    CreatedAtUnixMs ||
    32 random bytes
)   — 64 hex chars (BLAKE3-256 by default)
```

**Mesh creation flow:**

```
smo mesh create --name "SOC-Production"

1. Generate Mesh Root Keypair (Suite 1: Ed25519)
2. Compute MeshID = HashProvider::hash_hex(RootPK || time || random)
3. Generate first Authority Keypair
4. Root signs Authority Certificate:
     AuthorityID, MeshID, Role: AUTHORITY,
     Capabilities: [CAP_GRANT, CAP_REVOKE, ...],
     IssuedBy: RootID, Signature: RootPrivateKey
5. Export Recovery Package (AES-256-GCM encrypted, password-protected)
6. Delete Root Private Key from runtime filesystem
7. Authority certificate stored on node. Root stored OFFLINE.
```

### 7.3 Key Hierarchy

| Key | Purpose | Storage | Usage |
|---|---|---|---|
| Mesh Root Key | Bootstrap, recovery, rotate Authorities | OFFLINE (USB, YubiKey, cold storage, paper) | Once per mesh lifetime ideally never |
| Authority Key | Daily operations: sign certs, grant capabilities, revoke | Online (node runtime) | Every contract, every join |
| Node Key | Node identity, sign contracts, sign enrollment requests | Online (node runtime) | Every operation |

**The Root Key NEVER circulates.** It is:
1. Generated during mesh creation
2. Used immediately to sign the first Authority certificate
3. Exported as an encrypted Recovery Package
4. Deleted from the runtime filesystem

### 7.4 Certificate Chain

```
ROOT (offline)
  │  signs Authority.PublicKey
  ▼
AUTHORITY CERTIFICATE
  │  signs Contributor.PublicKey
  ▼
CONTRIBUTOR CERTIFICATE
  │  signs Reader.PublicKey (if policy allows)
  ▼
READER CERTIFICATE
```

Every certificate contains:

```json
{
  "mesh_id": "SOC-Production",
  "node_public_key": "base64...",
  "role": "AUTHORITY",
  "capabilities": ["CAP_GRANT", "CAP_REVOKE", "CAP_QUARANTINE"],
  "epoch": 1,
  "issued_by": "ROOT",
  "issued_at": "2026-07-15T00:00:00Z",
  "expires_at": "2032-07-15T00:00:00Z",
  "signature": "base64..."
}
```

**Verification:** Any node with the Mesh Root Public Key can verify the full chain:
```
Reader.cert → signed by → Authority.cert → signed by → Root.pub
```

### 7.5 Membership Certificate Format (.smoc)

The `.smoc` file is the serialized form of a Membership Certificate. It MAY be encoded as JSON, CBOR, or ASCII Armor.

**ASCII Armor:**
```
-----BEGIN SMO CERTIFICATE-----
SMOC1:
eyJtZXNoX2lkIjoiU09DLVZOIiw...
-----END SMO CERTIFICATE-----
```

### 7.6 Authority Privilege Boundaries

| Action | Authority A | Authority B | Root |
|---|---|---|---|
| Sign Contributor certs | YES (its own) | YES (its own) | YES |
| Revoke its own certs | YES | YES | YES |
| Revoke another Authority's certs | NO | NO | YES |
| Create new Root | NO | NO | N/A (via recovery) |
| Increment Epoch | NO | NO | YES |
| Read other nodes' keys | NO | NO | NO |

An Authority can only revoke certificates it personally issued (enforced by `issued_by` field).

### 7.7 Capability Epoch

The mesh maintains a monotonically increasing **Epoch** counter. Each Membership Certificate carries the Epoch at which it was issued. When the Epoch increments, all certificates with an older Epoch become invalid.

```
if cert.epoch < mesh.current_epoch → REJECT
(node must request a new certificate)
```

**Epoch increment IS a contract (Control protocol):**
```
Namespace: CONTROL
Message:   EPOCH_INCREMENT (0x02 0x51)
Signers:   Root (or M-of-N Recovery Authorities)
Propagation: gossip
```

This mechanism replaces CRL (Certificate Revocation Lists). No blacklist needed. Old certificates self-invalidate.

### 7.8 Recovery Authorities (Threshold)

At mesh creation, the Root Private Key is split into N shares using Shamir Secret Sharing with threshold M.

```
smo mesh create --recovery-group 5 --threshold 3

Root Private Key
  → divided into 5 shares
  → each share exported as separate file
  → each share stored on a different device/person
  → Root Private Key deleted from runtime
```

**Recovery flow (Root lost):**

```
3 of 5 share holders cooperate:
  smo mesh recover --shares 3-of-5
  → shares combined → reconstruct Root Private Key
  → smo mesh authority rotate --new-root
  → new Root generated
  → new Authority certificates issued
  → old certificates revoked via Epoch increment
```

### 7.9 Mesh Identity & Multi-Tenant

SMO's multi-mesh capability is a first-class architectural feature, NOT a configuration option. A single node is a multi-tenant runtime:

```
Node Identity (Ed25519 keypair, immutable for lifetime)
├── Mesh A: SOC-Production (cert, epoch 3, role=CONTRIBUTOR)
│   ├── route table     ── peers in Mesh A only
│   ├── trust store     ── trust scores for Mesh A members
│   ├── capability set  ── CAP_FS_READ, CAP_FS_WRITE, CAP_EXEC_BASIC
│   ├── contract history ── contracts executed in Mesh A
│   └── policy cache    ── locally cached Mesh A policy
├── Mesh B: IR-Prod (cert, epoch 1, role=AUTHORITY)
│   ├── route table     ── peers in Mesh B only
│   ├── trust store     ── trust scores for Mesh B members
│   ├── capability set  ── CAP_GRANT, CAP_REVOKE, CAP_NODE_QUARANTINE
│   ├── contract history ── contracts executed in Mesh B
│   └── policy cache    ── locally cached Mesh B policy
└── Mesh C: Research (cert, epoch 5, role=READER)
    ├── route table     ── peers in Mesh C only
    ├── trust store     ── trust scores for Mesh C members
    ├── capability set  ── CAP_HEARTBEAT, CAP_VERIFY, CAP_FS_READ
    ├── contract history ── contracts executed in Mesh C
    └── policy cache    ── locally cached Mesh C policy
```

**Multi-tenant rules:**
1. Each mesh context is fully isolated — no cross-mesh capability, trust, or policy leakage.
2. The node Identity keypair is SHARED across meshes. The same private key signs contracts in all meshes.
3. Membership is per-mesh. A certificate in Mesh A does not grant any access to Mesh B.
4. Context switching is explicit (`smo ctx use <mesh>`). Contracts are always executed in the active context unless overridden (`--mesh` flag).
5. A contract in Mesh A cannot reference resources in Mesh B. Cross-mesh operations require explicit governance (future).

**Benefits:**
- One daemon, multiple operational domains
- SOC and IR teams share the same infrastructure but operate in isolated trust domains
- Research mesh with experimental policies does not affect production
- DevOps mesh for fleet management alongside security mesh

### 7.10 Node Identity Lifecycle

Node identity is not a single event (generate → join). It is a full lifecycle with multiple states and transitions:

```
                  ┌─────────────────────┐
                  │     NEW              │  No keypair, no config
                  └──────────┬──────────┘
                             │ smo-node init
                             ▼
                  ┌─────────────────────┐
                  │  KEY_GENERATED       │  Ed25519 keypair exists
                  └──────────┬──────────┘
                             │ enrollment
                             ▼
                  ┌─────────────────────┐
                  │     JOINED           │  Membership Certificate received
                  └──────────┬──────────┘
                             │ verify + session
                             ▼
                  ┌─────────────────────┐
                  │     ACTIVE           │  Full mesh participation
                  └──────────┬──────────┘
                    ┌────────┼────────┐
                    ▼        ▼        ▼
              ┌────────┐┌────────┐┌────────┐
              │SUSPEND ││DEGRADED││OFFLINE │
              └────┬───┘└────┬───┘└────┬───┘
                   │         │         │  reactivate
                   └─────────┼─────────┘
                             │
                             ▼
                  ┌─────────────────────┐
                  │     ACTIVE           │  (return to active)
                  └──────────┬──────────┘
                             │ key compromise / lifecycle event
                             ▼
                  ┌─────────────────────┐
                  │  KEY_ROTATING        │  New keypair generated, old still valid
                  └──────────┬──────────┘
                             │ new cert issued, old enters grace period
                             ▼
                  ┌─────────────────────┐
                  │  KEY_ROTATED         │  New cert active, old in grace
                  └──────────┬──────────┘
                             │ grace period expires
                             ▼
                  ┌─────────────────────┐
                  │  ACTIVE (new key)    │  Old key/cert fully replaced
                  └──────────┬──────────┘
                             │ permanent departure
                             ▼
                  ┌─────────────────────┐
                  │     RETIRED          │  Node removed from mesh
                  └─────────────────────┘
```

#### 7.10.1 Lifecycle Transitions

| From | Event | To | Description |
|---|---|---|---|
| NEW | `smo-node init` | KEY_GENERATED | Ed25519 keypair created |
| KEY_GENERATED | Enrollment success | JOINED | Membership Certificate received |
| KEY_GENERATED | Enrollment failure | KEY_GENERATED | Retry with backoff |
| JOINED | Session established | ACTIVE | Certificate verified, capabilities negotiated |
| ACTIVE | Trust < threshold | DEGRADED | Limited operation, no new contracts |
| ACTIVE | Manual admin | SUSPENDED | Temporary deactivation |
| ACTIVE | Network loss | OFFLINE | Heartbeat timeout |
| ACTIVE | Key compromise detected | KEY_ROTATING | Emergency key rotation initiated |
| DEGRADED | Trust restored | ACTIVE | Trust recovers above threshold |
| SUSPENDED | Admin unsuspend | ACTIVE | Full reactivation |
| OFFLINE | Reconnect | ACTIVE | Session re-established |
| KEY_ROTATING | New cert issued | KEY_ROTATED | Both old + new certs valid |
| KEY_ROTATED | Grace period expires | ACTIVE | Old cert invalidated, only new cert active |
| ACTIVE | Admin retire | RETIRED | Node permanently removed |
| KEY_ROTATING | Rotation fails | ACTIVE (old key) | Fallback — keep old key, alert admin |

#### 7.10.2 Key Rotation (Zero Downtime)

Key rotation ensures continuity of operations:

```
1. Admin or node initiates key rotation
   smo node rotate-key

2. Node generates new Ed25519 keypair
   → node.key.new, node.pub.new

3. Node signs CSR with OLD private key
   → proves ownership of current identity

4. Authority validates CSR, issues new certificate for node.pub.new
   → new cert: epoch = current, supersedes = [old_cert_id]

5. Node imports new cert, sets grace period (default 7 days)
   → Both old and new certs are accepted during grace period

6. Node announces key rotation via gossip
   → TRUST_DIGEST includes new public key fingerprint

7. Grace period expires
   → Old certificate is invalidated
   → Only new certificate is accepted
```

**If old key is compromised:**
- Grace period = 0 (immediate cutover)
- Admin forces epoch increment to invalidate all old certificates
- All nodes must re-enroll

#### 7.10.3 Certificate Renewal vs Rotation

| Operation | Key Change | New CSR | Grace Period | Use Case |
|---|---|---|---|---|
| Renewal | No | No (same key) | N/A | Cert expiry refresh |
| Rotation | Yes | Yes (signed by old key) | Configurable | Periodic key refresh |
| Emergency rotation | Yes | Yes (out-of-band auth) | 0 | Key compromise |

#### 7.10.4 Suspension

Suspension is reversible deactivation:

```
smo admin suspend --node node-x --reason "maintenance"
  → node's certificate is marked SUSPENDED in mesh state
  → existing sessions are drained (no new contracts)
  → node can still receive heartbeat (for monitoring)
  → admin can unsuspend: smo admin unsuspend --node node-x
```

Suspension does NOT increment epoch. The certificate remains valid but is temporarily disabled.

### 7.11 Mesh Manifest

Every mesh has a "birth certificate" — a manifest file that encodes all mesh-level parameters. The manifest is a deployment artifact, NOT a protocol message.

**Format (mesh.yaml):**

```yaml
# Mesh Manifest — distributed out-of-band (file, URL, QR)
# Co-signed by M-of-N Authorities at creation time
mesh:
  uuid: "550e8400-e29b-41d4-a716-446655440000"
  name: "SOC-Production"
  genesis_time: "2026-07-15T00:00:00Z"
  protocol_version: 3.0
  description: "Production SOC mesh for incident response"

root:
  public_key: "MCowBQYDK2VwAyEA..."   # Mesh Root Public Key

governance:
  authority:
    issue_cert: 1
    revoke_cert: 1
    grant_capability: 1
    revoke_capability: 1
  policy:
    threshold: 2                        # Level 2: M-of-N
    authority_count: 3                  # total Authorities
  critical:
    threshold: 3                        # Level 3: M-of-N
    authority_count: 5                  # total Authorities
    emergency_lockdown: 3

bootstrap:
  nodes:
    - address: "node-a.company.com:7777"
    - address: "node-b.company.com:7777"
    - address: "node-c.company.com:7777"
  discovery: "seed"                     # "seed" | "dns" | "mDNS" | "manual"

heartbeat:
  interval_sec: 15
  timeout_sec: 45                       # 3 missed heartbeats = OFFLINE

trust:
  decay_window_sec: 300
  default_threshold: 0.5
  weights:
    citizen: 0.2
    execution: 0.5
    witness: 0.2
    consistency: 0.1

crypto:
  allowed_suites: [1, 2]                # Crypto Suite IDs permitted
  min_suite: 1                          # minimum suite for new joins

transport:
  allowed: [tcp, udp]                   # permitted transport protocols
  default_port: 7777

plugins:
  allowed: ["*"]                        # allowed plugin IDs ("*" = any)

capability_presets: {}                  # custom presets override defaults

policies:                               # mesh-level policy (post-MVP)
  default_allow: [CAP_HEARTBEAT, CAP_VERIFY]
  default_deny: []

# Co-signatures from initial Authorities
signatures:                             # manifest is co-signed at creation
  - authority_id: "Authority-A"
    signature: "base64..."
  - authority_id: "Authority-B"
    signature: "base64..."
  - authority_id: "Authority-C"
    signature: "base64..."
```

**Manifest verification:** A new node imports the manifest and verifies:
1. The Root Public Key matches the expected fingerprint
2. The manifest has M-of-N Authority signatures (per governance.policy.threshold)
3. All signatures chain up to the Root Public Key

**Node join flow with manifest:**

```
1. Operator provides mesh.yaml to new node (scp, URL, QR)
2. Node imports manifest: smo node import-manifest mesh.yaml
3. Node verifies: Root key fingerprint + M-of-N Authority signatures
4. Node knows: governance thresholds, crypto, transport, bootstrap, trust defaults
5. Node generates keypair
6. Node enrolls with Authority (per Enrollment Protocol — §IX)
7. Node receives Membership Certificate
8. Node is now a full member — all mesh parameters are locally cached
```

**Multi-tenant behavior:** A single SMO runtime can hold multiple manifests + certificates:

```
~/.smo/meshes/
├── SOC-Production/
│   ├── manifest.yaml        (verified, co-signed)
│   ├── node.smoc            (membership certificate)
│   └── node.key             (encrypted private key)
├── IR-Prod/
│   ├── manifest.yaml
│   ├── node.smoc
│   └── node.key
└── Research/
    ├── manifest.yaml
    ├── node.smoc
    └── node.key
```

Each mesh is fully isolated: separate governance, separate trust, separate capabilities.

---

## VIII. CAPABILITY SYSTEM

### 8.1 Capability Format

Capabilities are expressed as uppercase identifiers:

```
CAP_HEARTBEAT
CAP_VERIFY
CAP_FS_READ
CAP_FS_WRITE
CAP_PROC_EXEC
CAP_EXEC_BASIC
CAP_NET_BIND
CAP_SESSION_CREATE
CAP_NODE_QUARANTINE
CAP_GRANT
CAP_REVOKE
CAP_DISTRIBUTE
CAP_POLICY_CHANGE
CAP_NODE_BOOTSTRAP
CAP_SIGN_NODE
CAP_EPOCH_INCREMENT
CAP_CUSTOM_CONTRACT
```

### 8.2 Capability Properties

- **Ephemeral**: Capabilities are not permanent. They are granted within a session.
- **Session-scoped**: A capability is valid only within the context of a specific session.
- **Runtime validated**: Capabilities are checked at execution time, never assumed from configuration.
- **Epoch-gated**: The certificate's epoch must match the mesh's current epoch for capabilities to be valid.

### 8.3 Capability Presets (Role Profiles)

The runtime does NOT hardcode roles. These are predefined groupings:

**ROLE_READER** =
```
CAP_HEARTBEAT
CAP_VERIFY
CAP_FS_READ
CAP_SESSION_CREATE
```

**ROLE_CONTRIBUTOR** =
```
ROLE_READER
+
CAP_FS_WRITE
CAP_EXEC_BASIC
CAP_CUSTOM_CONTRACT
```

**ROLE_AUTHORITY** =
```
ROLE_CONTRIBUTOR
+
CAP_PROC_EXEC
CAP_GRANT
CAP_REVOKE
CAP_DISTRIBUTE
CAP_POLICY_CHANGE
CAP_NODE_BOOTSTRAP
CAP_SIGN_NODE
CAP_EPOCH_INCREMENT
CAP_NODE_QUARANTINE
```

Custom roles are defined in configuration, never in the runtime:

```
PRESET: ROLE_SOC_ANALYST
  CAP_FS_READ
  CAP_EXEC_BASIC
  CAP_NODE_QUARANTINE
  (but NOT CAP_GRANT, NOT CAP_REVOKE)
```

### 8.4 Capability Origin

Capabilities originate from the Authority that signs the Membership Certificate. All nodes can verify the signature chain back to the Root.

### 8.5 Capability Revocation

Capabilities are revoked by:
1. **Capability Epoch increment** — all old certificates invalidated at once
2. **REVOKE_CERT contract** — revokes a specific certificate
3. **Session expiry** — capabilities expire with the session

Revocation via REVOKE_CERT is itself a contract (Control protocol), propagated via gossip.

---

## IX. ENROLLMENT PROTOCOL

### 9.1 Enrollment Architecture

Enrollment is the process of delivering a node's Public Key to an Authority and receiving a Membership Certificate. SMO defines the Enrollment Protocol but NOT the transport carrier.

```
┌─────────────────────────────────────────────┐
│ IDENTITY LAYER                               │
│ smo-node init → generates Ed25519 keypair    │
│                node.key (private, NEVER leaves)│
│                node.pub (public, 32 bytes)   │
├─────────────────────────────────────────────┤
│ ENROLLMENT LAYER                             │
│ How does node.pub reach the Authority?       │
│                                              │
│ Carriers (all valid):                        │
│   File (scp, USB)                            │
│   Clipboard (copy-paste)                     │
│   QR (air-gap, demo)                         │
│   REST API (POST /api/v1/enroll)             │
│   Bluetooth / NFC                            │
├─────────────────────────────────────────────┤
│ CERTIFICATION LAYER                          │
│ Authority validates → signs → returns        │
│ MembershipCertificate (.smoc)                │
└─────────────────────────────────────────────┘
```

### 9.2 Enrollment Request (.smor)

```json
{
  "version": 1,
  "mesh_id": "SOC-Production",
  "node_name": "server-01",
  "os": "Linux 6.8.0-amd64",
  "smo_version": "3.0.0",
  "public_key": "MCowBQYDK2VwAyEA...",
  "fingerprint": "A3F9...",
  "requested_role": "CONTRIBUTOR",
  "requested_capabilities": ["CAP_FS_READ", "CAP_FS_WRITE", "CAP_EXEC_BASIC"],
  "nonce": "7b8a9c1d2e3f4a5b",
  "timestamp": "2026-07-15T12:00:00Z",
  "signature": "base64..."    // signed by node's private key
}
```

### 9.3 Node Certificate (.smoc)

```json
{
  "version": 1,
  "mesh_id": "SOC-Production",
  "node_public_key": "MCowBQYDK2VwAyEA...",
  "node_fingerprint": "A3F9...",
  "role": "CONTRIBUTOR",
  "capabilities": ["CAP_FS_READ", "CAP_FS_WRITE", "CAP_EXEC_BASIC"],
  "epoch": 1,
  "issued_by": "Authority-A",
  "issued_at": "2026-07-15T12:01:00Z",
  "expires_at": "2030-07-15T12:01:00Z",
  "signature": "base64..."    // signed by Authority's private key
}
```

### 9.4 CLI Workflow (SSH-like)

```
# On the new node:
smo-node init
  → generates node.key, node.pub
  → outputs enrollment request

smo export --format text > join_request.smor
  (or --format qr, --format json, --format binary)

# Transfer to Authority (any carrier):
scp join_request.smor admin@authority:/tmp/

# On the Authority machine:
smo-admin sign join_request.smor
  → validates request
  → signs certificate
  → outputs node.smoc

# Transfer certificate back to node:
scp node.smoc admin@node:/tmp/

# On the node:
smo-node import node.smoc
  → installs certificate
  → node is now a mesh member
```

**This mirrors SSH:** `ssh-keygen` → `ssh-copy-id` → login.

### 9.5 Enrollment Modes

| Mode | Medium | Use Case |
|---|---|---|
| Interactive (CLI↔CLI) ⭐ | File transfer (scp, rsync, curl) | DevOps, server-to-server |
| Clipboard | Copy-paste (text, email, chat) | Remote teams, Discord, Signal |
| QR | Phone camera, KVM camera | Air-gap, on-prem, demos |
| USB | Removable media | Isolated networks, compliance |
| API | REST endpoint | Enterprise, CI/CD, cloud |

Every mode is optional. A deployment may implement only the modes it needs. The .smor/.smoc format is mandatory; the carrier is not.

### 9.6 Join Token

QR codes carry a **Join Token**, not a certificate:

```
SMO://JOIN
  ?mesh=soc-vn
  &token=abcefg123
  &bootstrap=node.company.com:5555
```

**Token properties:**
- Generated by Authority
- Single-use (consumed after first successful join)
- Time-limited (default: 10 minutes)
- No private key in token — token is purely an authorization secret

**Node join via token:**
```
1. Node scans QR
2. Connects to bootstrap node
3. Generates ephemeral keypair for join
4. Sends CSR: { mesh_id, join_token, public_key, signed_nonce }
5. Authority validates token → signs Membership Certificate
6. Node receives and imports certificate
```

### 9.7 Export/Import Abstraction

```cpp
// Export an enrollment request in any format
std::error_code ExportEnrollmentRequest(
    const EnrollRequest& request,
    ExportFormat format,       // TEXT | JSON | QR | BINARY
    std::vector<uint8_t>& out
);

// Import from any format (auto-detect encoding)
std::error_code ImportCertificate(
    std::span<const uint8_t> data,
    NodeCertificate& cert
);
```

```bash
smo export --format text     # ASCII Armor (.smor)
smo export --format json     # JSON (.smor.json)
smo export --format qr       # QR on terminal
smo export --format binary   # Raw CBOR (.smor.cbor)
```

Export changes only the presentation. The underlying EnrollRequest is identical.

---

## X. CONTRACT MODEL

This section supersedes the original contract model (RFC 0001) with the layered architecture defined in RFC 0023. The three-party execution flow (Requester → Responder → Witness) remains valid at the execution layer (§XI), but the contract definition itself is now separated from intent and managed through a formal registry.

### 10.1 Layered Model

```
User (CLI/GUI/REST/SDK)
        │  expresses goal
        ▼
    Intent                 (what: opcode + targets + parameters)
        │  resolved by
        ▼
    Contract Factory       (maps Intent → ContractID via Registry)
        │
        ▼
    Contract Definition    (how: canonical template loaded from Registry)
        │  compiled (node-local)
        ▼
    Compiler               (Contract + Parameters + Env → DAG)
        │
        ▼
    DAG Cache              (keyed by ContractID + env_fingerprint)
        │
        ▼
    Executor               (DAG-aware scheduler runs the graph)
```

**Key rule:** The user never touches a Contract directly. All entry points produce an Intent. The Contract Factory resolves Intent → ContractID. The Compiler loads the Contract definition from the local Registry, compiles it to a DAG, and hands the DAG to the Executor.

### 10.2 Three Contract Categories

| Category | Nature | Storage | Lifecycle | Examples |
|---|---|---|---|---|
| **Kernel** | System-level, trusted. Executor runs these inline — no sandbox | Built into runtime binary | Fixed per SMO version; updated only via runtime upgrade | `ping`, `whoami`, `session_open`, `session_close`, `discover`, `node.info`, `identity.rotate` |
| **Native** | Runtime-builtin template | Registered at startup from `contract/registry/native.cpp` | Fixed for SMO version; updated only via runtime upgrade | `ls`, `put`, `get`, `exec`, `quarantine` |
| **Mesh** | Published by a mesh participant | Contract Registry (local DB, pulled from peers) | Draft → Publish → Sync → Compile → Execute → Deprecate | Incident playbooks, compliance checks, custom workflows |
| **Private** | User-local, never shared | Local store only | Created, executed, deleted within local scope | Personal automation, test contracts |

### 10.3 Contract Definition Schema

Every contract (all four categories) is a **canonical JSON object**:

```json
{
  "contract_version": "1.0",
  "category": "native",
  "opcode": "ls",
  "name": "List Directory",
  "description": "List files at the specified path",
  "publisher": "00000000-0000-0000-0000-000000000000",
  "semver": "1.0.0",
  "parameters": {
    "path": {
      "type": "string",
      "required": true,
      "description": "Absolute path to list"
    },
    "recursive": {
      "type": "boolean",
      "required": false,
      "default": false
    }
  },
  "capabilities_required": {
    "filesystem_read": 1
  },
  "abi": {
    "abi_version": 1,
    "input_schema": { "type": "object", "properties": { "path": { "type": "string" }, "recursive": { "type": "boolean" } } },
    "output_schema": { "type": "array", "items": { "type": "object", "properties": { "name": { "type": "string" }, "size": { "type": "integer" } } } },
    "capability_mask": "00000001",
    "opcode_dependencies": [],
    "abi_hash": "abc123...",
    "semantic_hash": "def456...",
    "min_runtime_version": "3.0.0",
    "max_runtime_version": "3.255.255"
  },
  "compiler_hints": {
    "max_parallelism": 1,
    "timeout_sec": 30,
    "idempotent": true
  },
  "signature": null
}
```

**Canonical JSON rules:**
1. Keys sorted lexicographically.
2. No whitespace beyond JSON requirement.
3. No trailing newline.
4. UTF-8 without BOM.

#### Field definitions

| Field | Type | Required | Description |
|---|---|---|---|
| `contract_version` | string (semver) | yes | Schema version of the contract format |
| `category` | string | yes | `native`, `user_defined`, or `internal` |
| `opcode` | string | yes | Primary opcode this contract implements |
| `name` | string | yes | Human-readable name (max 128 chars) |
| `description` | string | no | Human-readable description (max 2048 chars) |
| `publisher` | string (UUID) | yes | NodeID of publisher; zero-UUID for native |
| `semver` | string (semver) | yes | Version of this contract definition |
| `parameters` | object | yes | Parameter schema (JSON Schema subset) |
| `capabilities_required` | object | yes | Capability → level map for execution |
| `compiler_hints` | object | yes | max_parallelism, timeout_sec, idempotent |
| `signature` | string (Base64) | conditional | Publisher's signature; null for native |

### 10.4 ContractID

```
ContractID = HashProvider::hash_hex(utf8(canonical_json))
           = HashSuite1(BLAKE3-256) → 64 hex chars by default
```

**Properties:**
- **Content-addressed** — same canonical definition always produces the same ContractID.
- **Immutable** — changing any field produces a different ContractID. No "update in place."
- **64 hex chars** (256 bits) — matches SMO's default Hash Suite 1 (BLAKE3-256).
- **Primary key** in Contract Registry, DAG cache, and Intent resolution.
- **Algorithm-agnostic:** The formula always calls `HashProvider`. A FIPS node with SHA-256 configured produces ContractIDs over the same canonical JSON, but with different hash output (different SuiteID recorded in metadata).

### 10.5 Contract Lifecycle

```
       ┌──────────┐
       │  Draft   │  (local, not published)
       └────┬─────┘
            │ publish (sign + store)
            ▼
       ┌──────────┐
       │ Published│  (in Registry, immutable)
       └────┬─────┘
            │ sync (pull from peer)
            ▼
       ┌──────────┐
       │  Synced  │  (verified, stored locally)
       └────┬─────┘
            │ compile (node-local)
            ▼
       ┌──────────┐
       │  Cached  │  (DAG in dag_store)
       └────┬─────┘
            │ execute
            ▼
       ┌──────────┐
       │ Executed │  (run by Executor)
       └──────────┘

Post-execution:
       ┌──────────┐
       │ Deprecated│← publisher signature or governance
       └──────────┘

       ┌──────────┐
       │  Revoked │← emergency governance (Level 3+)
       └──────────┘
```

| Transition | From | To | Trigger |
|---|---|---|---|
| publish | Draft | Published | Publisher signs + calls `Publish()` |
| sync | Published | Synced | Remote node calls `Sync()` |
| compile | Synced | Cached | Compiler produces DAG; stored in `dag_store` |
| deprecate | Published | Deprecated | Publisher signature or governance proposal |
| revoke | Deprecated | Revoked | Emergency governance (Level 3+) |

### 10.6 Contract Registry

The Contract Registry is **Git-like, not Docker-like**:

- **Immutable:** Once written, a ContractID cannot be deleted or overwritten.
- **Append-only:** New contracts are appended; old ones remain for audit.
- **Blake3-addressed:** Key = ContractID.
- **Local-first:** Each node maintains its own Registry DB.
- **Verifiable:** Every user-defined contract carries a publisher signature (§10.8).

#### Registry schema (SqliteStore)

```sql
CREATE TABLE IF NOT EXISTS contract_registry (
    contract_id     TEXT PRIMARY KEY,
    canonical_json  TEXT NOT NULL,
    category        TEXT NOT NULL,
    opcode          TEXT NOT NULL,
    name            TEXT NOT NULL,
    publisher       TEXT NOT NULL,
    semver          TEXT NOT NULL,
    parameters      TEXT NOT NULL,
    capabilities    TEXT NOT NULL,
    compiler_hints  TEXT NOT NULL,
    signature       TEXT,
    published_at    INTEGER NOT NULL,
    status          TEXT NOT NULL DEFAULT 'active',
    deprecation_note TEXT
);
```

#### Registry operations

| Operation | Description |
|---|---|
| `Publish(definition, signature)` | Verify canonical JSON, compute ContractID, verify signature, insert |
| `Resolve(opcode, version)` | Query registry for matching opcode; return latest active contract |
| `Get(contract_id)` | Load canonical JSON by ContractID |
| `Deprecate(contract_id, reason)` | Set status = 'deprecated'; requires publisher signature or governance |
| `Sync(peer, since)` | Pull new contracts from peer since timestamp; verify each before insert |

#### Sync strategy

- **Pull-based:** Node A requests new contracts from Node B since a given timestamp.
- **Verification:** Each entry is verified (signature, canonical JSON) before local insert.
- **No DAG sync:** Only canonical definitions are synced. DAGs are never transferred.
- **Trust filter:** A node only syncs from peers with trust score ≥ configured threshold.

### 10.7 Intent → Contract Resolution

```
User: smo exec ls --path /tmp

1. CLI produces Intent:
   {
     "opcode": "ls",
     "targets": ["node-abc"],
     "parameters": {"path": "/tmp"},
     "scope": "single",
     "trust_min": 0.5
   }

2. Contract Factory:
   a. Look up ContractID for opcode "ls":
      - Check user-specified contract_hint (optional)
      - Query registry for latest active "ls" contract
      - Fall back to native contract if no user-defined match
   b. Return ContractDefinition

3. Compiler:
   a. Check local dag_cache for (ContractID, env_fingerprint)
   b. Cache miss → compile Contract + Parameters → DAG
   c. Store DAG in dag_cache

4. Executor runs DAG (see §XIII)
```

### 10.8 Signature Scheme (User-Defined Contracts)

1. Author computes `ContractID = Blake3(utf8(canonical_json))`.
2. Author signs `ContractID` with their Ed25519 private key.
3. `signature` field = Base64-encoded signature.
4. Verification: compute `ContractID`, verify signature against publisher's public key.

### 10.9 Contract Record (Post-Execution)

The post-execution record (stored by all three participants) uses the same schema as §10.3 but adds runtime fields:

| Field | Description |
|---|---|
| ContractID | ContractID of the resolved contract |
| IntentID | ID of the originating Intent |
| RequesterID | Originating node |
| ResponderID | Executing node |
| WitnessID | Attesting node |
| StartTime | Execution start (Unix ns) |
| FinishTime | Execution end (Unix ns) |
| Status | SUCCESS / FAILURE / REJECTED / TIMEOUT |
| ResultHash | HashProvider::hash_hex(execution result) — 64 hex chars |
| AuditHash | HashProvider::hash_hex(full audit trail) — 64 hex chars |
| RequesterSignature | Signed by requester |
| ResponderSignature | Signed by responder |
| WitnessSignature | Signed by witness |

### 10.10 Large Data Handling

If an opcode operates on large data, the data is transferred through a separate **Data Protocol** channel (§VI.1). The contract references data only by hash.

### 10.11 Contract ABI

Every contract carries an **ABI** (Application Binary Interface) that describes
its inputs, outputs, capabilities, dependencies, and version bounds. The ABI
is a canonical JSON object with a content-addressed **ABI Hash** enabling
runtime verification without re-parsing the contract body.

#### ABI Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `abi_version` | uint8 | yes | ABI format version. Current = 1 |
| `input_schema` | Schema | yes | Input parameter schema (JSON Schema subset) |
| `output_schema` | Schema | yes | Output result schema (JSON Schema subset) |
| `capability_mask` | string | yes | Hex-encoded 64-bit capability bitmask |
| `opcode_dependencies` | string[] | yes | Opcodes this contract invokes |
| `abi_hash` | string | yes | BLAKE3(canonical ABI JSON excluding abi_hash and semantic_hash) |
| `semantic_hash` | string | yes | BLAKE3(abi_hash + canonical contract body) |
| `min_runtime_version` | semver | yes | Minimum SMO runtime version required |
| `max_runtime_version` | semver | yes | Maximum SMO runtime version allowed |

#### Hash Linkage

```
ContractID  = BLAKE3(canonical_json(full_contract))
    ↕  (verifies contract body integrity)
Semantic Hash = BLAKE3(abi_hash || body_json)
    ↕  (verifies ABI + body consistency)
ABI Hash    = BLAKE3(canonical_json(abi_fields))
    ↕  (verifies interface compatibility)
Runtime check: ABI Hash match → no re-parse needed
```

If the ABI Hash matches a cached DAG, the runtime skips re-validation.

#### ABI Registry

```cpp
class AbiRegistry {
public:
    void register_abi(const ContractABI& abi);
    std::optional<ContractABI> get_abi(const ContractID& id) const;
    bool verify_compatibility(
        const ContractID& id, const ExecutionContext& ctx) const;
    Hash256 compute_abi_hash(const ContractABI& abi) const;
};
```

Populated at registration time (kernel/native), publish time (mesh/private),
and on registry sync (from remote peers).

#### Version Compatibility

| Runtime ABI | Contract ABI | Result |
|-------------|--------------|--------|
| 1 | 1 | Compatible |
| 1 | 2 | Incompatible — reject |
| 2 | 1 | Compatible (backward compatible) |

`min_runtime_version` and `max_runtime_version` are checked at dispatch time:

```
if (runtime_version < abi.min_runtime_version) reject;
if (runtime_version > abi.max_runtime_version) reject;
```

---

## XI. WITNESS PROTOCOL

### 11.1 Witness Role

A witness is an independent third node that:
- Does NOT execute the contract
- Does NOT own the target resource
- Does NOT vote or arbitrate
- Attests that it knows both Requester and Responder
- Confirms it has no knowledge of anomalous behavior
- Stores a copy of the contract record for future non-repudiation

### 11.2 Witness Selection

1. The Responder selects the witness.
2. Selection priority: (a) Most trusted node known to the Responder (highest local Trust Score). (b) Random selection from known nodes.
3. The Requester MAY propose a witness candidate, but the Responder has final choice.

### 11.3 Witness Redundancy

```
Primary Witness
     ↓ timeout
Secondary Witness
     ↓ timeout
Fallback: Responder proceeds with local decision only (no witness)
```

### 11.4 Witness Confirmation Payload

```
witness_id
contract_id
requester_id
responder_id
trust_digest
attestation
timestamp
witness_signature
```

---

## XII. TRUST AND REPUTATION SYSTEM

### 12.1 Trust Components

| Component | Measures | Input Signals |
|---|---|---|
| Citizen Score | Online time, heartbeat stability, route reliability | Heartbeat response rate, uptime, route consistency |
| Execution Score | Successful contract execution, no rollbacks, no witness objections | Contract completion ratio, witness attestations |
| Witness Score | Witness participation, accuracy of attestations | Number of witness assignments, confirmation accuracy |
| Consistency Score | Agreement with majority in multi-node operations | Result comparison across nodes |

### 12.2 Composite Trust Score

```
Trust = Citizen × 0.2 + Execution × 0.5 + Witness × 0.2 + Consistency × 0.1
```

Weights are defaults. Individual nodes MAY configure different weights in their local policy.

### 12.3 Score Decay

All trust scores MUST use sliding window decay. Scores reflect recent behavior, not cumulative history.

### 12.4 Trust in Execution Decisions

Trust is ONE input to the Responder's decision, not the sole factor:

```
Execute = CapabilityValid AND
          PolicyMatches AND
          CertificateValid AND
          EpochValid AND
          SignatureValid AND
          SessionValid AND
          (Trust >= LocalThreshold OR OverrideByAuthority)
```

### 12.5 Trust Digests Are Hints

Trust digests transmitted over the wire (via TRUST_DIGEST message in Control protocol) are **hints only**. The receiving node:
1. Receives the trust digest from the wire
2. Compares it against its locally computed score for that node
3. Blends the two (weighted by the sender's own trust score)
4. Uses the blended value as ONE input

The local trust computation is always authoritative.

### 12.6 Penalties

- Contract rejected due to insufficient trust: small score penalty to Requester
- Contract rejected due to insufficient capability: larger score penalty to Requester
- Spam detection: nodes that repeatedly submit rejectable contracts accumulate score penalties

---

## XIII. EXECUTION DAG

### 13.1 DAG Format (Internal)

```json
{
  "graph_id": "dag-777",
  "nodes": [
    {
      "task_id": "t1",
      "opcode": { "namespace": "EXECUTION", "id": "EXEC_START" },
      "depends_on": [],
      "parameters": { ... }
    },
    {
      "task_id": "t2",
      "opcode": { "namespace": "EXECUTION", "id": "EXEC_EVENT" },
      "depends_on": ["t1"],
      "parameters": { ... }
    }
  ]
}
```

### 13.2 Compiler Pipeline

The compiler is a 6-stage pipeline that transforms a contract definition into
an immutable ExecutionGraph (DAG). A **SMIR** (SMO Intermediate Representation)
layer decouples input formats from the downstream pipeline, enabling future
frontends (DSL, YAML, visual workflow, AI generator) to reuse the same
optimizer, planner, and executor.

```
CONTRACT JSON / DSL / YAML / AI-GEN
    ↓
PARSER                 →  JSON → AST (abstract syntax tree)
    ↓
  ┌─────┐
  │ AST │             Opcodes, operands, edges, conditions
  └─────┘
    ↓
  ┌──────┐
  │ SMIR │             SMO Intermediate Representation — canonical IR
  └──────┘             (opcodes + basic blocks + operands, format-agnostic)
    ↓
SEMANTIC VALIDATOR     →  Pass 1: ABI hash match, capability req, opcode dep check
    ↓
PLANNER                →  Target node selection, shard mapping
    ↓
BUILDER                →  SMIR → ExecutionGraph (DAG construction)
    ↓
OPTIMIZER              →  Prune redundant nodes, merge read-only siblings,
                          constant folding
    ↓
FINAL VALIDATOR        →  Pass 2: acyclic check, max depth/width,
                          node reachability
    ↓
EXECUTION DAG          →  immutable output
```

**Key rules:**
- Compiler does NOT depend on Runtime.
- Executor does NOT depend on contract category.
- DAG is immutable once produced (§13.3).
- Cache keyed by `ContractID + env_fingerprint` (§13.5).

### 13.3 DAG Immutability Rule

Once the DAG is produced, it MUST NOT be modified. All nodes involved in execution operate on the same DAG. If a DAG must change, a new DAG is compiled from the original intent.

### 13.4 Compiler Interface

The compiler accepts a Contract Definition + Intent parameters + Node Environment and produces a DAG.

```
CompileInput {
    ContractDefinition  contract;        // loaded from Registry by ContractID
    std::string         parameters_json; // from Intent
    NodeEnvironment     environment;     // os, arch, plugin versions, policy
}

CompileOutput {
    DAG     dag;
    string  env_fingerprint;  // Blake3 of environment snapshot
    uint64  compiled_at;      // Unix nanoseconds
}
```

**Node environment fingerprint:**

```cpp
struct NodeEnvironment {
    string os;
    string arch;
    uint64 env_epoch;                       // incremented on plugin/policy change
    map<string, string> plugin_versions;    // "plugin-id" → "1.2.0"
    vector<string> enabled_policies;
};
```

### 13.5 DAG Cache

The DAG cache avoids recompilation when the same contract runs with the same environment.

```
Cache key = Blake3(ContractID + "|" + env_fingerprint)
```

**Rules:**
1. **Cache hit:** Return cached DAG without recompiling.
2. **Cache miss:** Compile, store in `dag_store`, return.
3. **Cache invalidation:** When `env_epoch` changes (plugin install/remove, policy update), old cache entries are naturally bypassed because `env_fingerprint` changes.
4. **Eviction:** LRU eviction when cache exceeds limit (default 1000 DAGs).
5. **No DAG sync:** DAGs are never transferred between nodes. Each node compiles independently.

#### DAG cache schema

```sql
CREATE TABLE IF NOT EXISTS dag_cache (
    cache_key       TEXT PRIMARY KEY,
    contract_id     TEXT NOT NULL,
    env_fingerprint TEXT NOT NULL,
    dag_json        TEXT NOT NULL,
    compiled_at     INTEGER NOT NULL,
    last_accessed   INTEGER NOT NULL
);
```

### 13.6 Scheduler Runtime Policies

The scheduler is DAG-aware but also implements runtime scheduling policies:

#### Priority Queues

Each task in the DAG carries a `priority` value (0–255, default 100). The scheduler maintains multiple queues:
- **Critical (200–255)**: Incident response, quarantine, emergency stop
- **High (150–199)**: Time-sensitive operations
- **Normal (50–149)**: Default execution
- **Low (0–49)**: Batch jobs, log rotation, maintenance

Tasks from higher-priority queues are dispatched before lower-priority ones, regardless of DAG order (respecting dependency constraints).

#### Retry Policy

Each task MAY declare a retry policy:

```json
{
  "task_id": "t1",
  "retry": {
    "max_attempts": 3,
    "backoff": "exponential",     // "exponential" | "linear" | "constant"
    "initial_delay_ms": 1000,
    "max_delay_ms": 30000,
    "jitter": true                // add random jitter to avoid thundering herd
  }
}
```

Default: no retries. Tasks that fail are marked FAILED.

#### Cancellation

A contract with `EXEC_CANCEL` stops the associated execution:
1. Scheduler marks the task as CANCELLING.
2. Runtime sends SIGTERM to the process (or equivalent).
3. After `grace_sec` (default 10s), sends SIGKILL.
4. Task transitions to CANCELLED.
5. Downstream DAG tasks that depend on the cancelled task are marked CANCELLED (cascade).

#### Deadline Propagation

Each DAG node carries a `deadline_ms` (relative to DAG start). If a task misses its deadline:
1. The task is marked FAILED (deadline exceeded).
2. The failure propagates to all downstream dependent tasks.
3. The scheduler MAY attempt to execute remaining independent tasks.

#### Preemption (Post-MVP)

Only implemented when Resource Model is active:
- A higher-priority task MAY preempt a lower-priority running task.
- The preempted task is suspended (SIGSTOP on Linux) and resumed when resources are available.
- If the preempted task cannot be suspended (non-preemptible opcode), the higher-priority task waits.

#### Quota Enforcement

Per-mesh and per-node execution quotas limit concurrency:
```
mesh "SOC-Production": max_concurrent = 10, cpu_quota = 4 cores
node "server-01":      max_concurrent = 4,  cpu_quota = 2 cores
```
When a quota is exceeded, new contracts are queued or REJECTED based on priority.

### 13.7 Runtime Interface

The runtime exposes a **single entry point** for all contract execution:

```cpp
struct ExecutionResult {
    bool success;
    ContractID contract_id;
    Hash256 dag_hash;           // hash of the executed DAG
    std::string output_json;    // contract output (if any)
    uint64_t started_at;        // Unix ns
    uint64_t completed_at;      // Unix ns
    std::vector<Error> errors;  // per-task errors (if any)
};

struct ExecutionContext {
    SessionID session_id;
    NodeID requester;
    CapabilityMask granted_caps;
    uint64_t deadline_ns;
    uint32_t max_concurrency;
};

class Runtime {
public:
    // The ONLY entry point for contract execution.
    // Runtime does NOT know contract category (Kernel/Native/Mesh/Private).
    ExecutionResult execute(
        const ContractID& id,
        const ExecutionContext& ctx
    );
};
```

**Execution flow:**
1. Resolve `ContractID` via `ContractRegistry` → get `ContractDefinition`
2. Check DAG cache (keyed by `ContractID + env_fingerprint`)
3. On cache miss: call `Compiler::compile()` → cache DAG in `dag_store`
4. Dispatch each DAG task node through `Executor`
5. Return `ExecutionResult`

The executor is **ignorant** of contract category — it receives a compiled
`ExecutionGraph` and a `Session`, executes each task node, and returns results.
Category affects sandbox policy only (Kernel = inline, Native = capability gate,
Mesh/Private = full sandbox + audit).

---

## XIV. NODE EXECUTION FSM

### 14.1 FSM Implementation Rules

Every state transition MUST be:
1. **Auditable** — recorded in the audit log
2. **Deterministic** — same input + same prior state = same output
3. **Replayable** — the audit log is sufficient to reconstruct the exact execution
4. **Serializable** — the state can be stored, transmitted, and verified

### 14.2 Node-Level States

```
UNINITIALIZED           No keypair yet
    ↓
KEY_GENERATION          Generate Ed25519 keypair (smo-node init)
    ↓
ENROLLING               Send CSR, await Membership Certificate
    ↓
DOMAIN_JOIN             Certificate received, begin discovery
    ↓
CAPABILITY_NEGOTIATION  Verify certificate chain, establish session
    ↓
ACTIVE                  Full participation
    ↓
CONTRACT_PENDING    →   CONTRACT_VALIDATING
                            ↓
                       CONTRACT_ACCEPTED  or  CONTRACT_REJECTED
                            ↓
                       (if accepted) EXECUTING
                            ↓
                       COMPLETED  or  FAILED
                            ↓
                       AUDITING
                            ↓
                       RECORDING
                            ↓
                       ACTIVE
    ↓
DEGRADED  or  QUARANTINED  or  OFFLINE
```

### 14.3 Contract Execution Sub-FSM

```
PENDING
    ↓
VALIDATE_POLICY         →  REJECTED
    ↓
VALIDATE_CERTIFICATE    →  REJECTED (cert expired, wrong epoch, bad chain)
    ↓
VALIDATE_CAPABILITY     →  REJECTED
    ↓
VALIDATE_SIGNATURE      →  REJECTED
    ↓
VERIFY_SESSION          →  REJECTED
    ↓
CONSULT_WITNESS         →  REJECTED or fallback
    ↓
LOCAL_DECISION
    ↓
EXECUTE  or  REJECT
    ↓
RECORD_RESULT
    ↓
NOTIFY_PARTICIPANTS
    ↓
FINALIZED
```

---

## XV. STORAGE MODEL

### 15.1 Storage Types

| Store | Content | Scope |
|---|---|---|---|
| session_store | Active session state (capabilities, keys, expiration) | Per-node |
| trust_store | Trust scores, score history, decay parameters | Per-node |
| audit_store | Execution audit log (all state transitions) | Per-node |
| dag_store | Compiled execution DAGs | Per-node |
| node_store | Node identity keypair, configuration, route table | Per-node |
| mesh_store | Mesh memberships, certificates, epoch, root public key | Per-node |
| peer_store | Peer Records: addresses, latency, NAT type, trust | Per-node |
| governance_store | Governance history: proposals, signatures, decisions | Per-node |

### 15.2 Storage Invariant

Never store mutable shared execution state globally. All stored state is per-node isolated.

---

## XVI. SECURITY IMPLEMENTATION

### 16.1 Crypto Suite 1 (Phase 1 — Classical)

| Algorithm | Purpose |
|---|---|
| Ed25519 | Signing and identity |
| X25519 | Key exchange |
| XChaCha20-Poly1305 | Symmetric encryption |
| Hash Suite 1 (BLAKE3-256) | Cryptographic hashing |

### 16.2 Crypto Suite 2 (Phase 2 — Hybrid PQC)

| Algorithm | Purpose |
|---|---|
| Ed25519 + ML-DSA (Dilithium) | Signing and identity |
| X25519 + ML-KEM (Kyber) | Key exchange |
| XChaCha20-Poly1305 | Symmetric encryption |
| Hash Suite 1 (BLAKE3-256) | Cryptographic hashing |

### 16.3 Crypto Suite 3 (Phase 3 — Pure PQC)

| Algorithm | Purpose |
|---|---|
| ML-DSA (Dilithium) | Signing and identity |
| ML-KEM (Kyber) | Key exchange |
| XChaCha20-Poly1305 | Symmetric encryption |
| Hash Suite 1 (BLAKE3-256) | Cryptographic hashing |

### 16.4 Key Management

- Each node generates its own keypair during `smo-node init`.
- NodeID = HashProvider::hash_hex(NodePublicKey) (default: BLAKE3-256 → 64 hex chars).
- Root Key generated at mesh creation, exported as encrypted Recovery Package, deleted from runtime.
- Authority Keys are signed by the Root and stored on the Authority node.
- Session keys are derived via the negotiated Crypto Suite's key exchange mechanism.
- Long-term keys are stored in node_store (encrypted at rest).

### 16.6 Hash Provider and Hash Suite Architecture

#### 16.6.1 Overview

All hashing in SMO goes through `HashProvider`, a swappable abstract interface in `core/crypto/hash_provider.hpp`. No business logic calls a hash algorithm directly.

SMO defines **Hash Suite** as a first-class concept (see `core/crypto/fwd.hpp`):

```
Hash Suite
├── Algorithm     (e.g. BLAKE3, SHA-256, SHA3-256, xxHash3)
├── Digest Size   (e.g. 256 bits for crypto, 64 bits for perf)
├── Output Mode   (e.g. raw bytes, hex string, stream)
└── Version       (reserved for future algorithm revisions)
```

A single `HashSuiteID` (uint16_t) encodes all four parameters. This means:
- Changing digest size (BLAKE3-256 vs BLAKE3-512) produces a different SuiteID.
- Adding a new algorithm version (e.g. SHA-256 with different internal params) produces a different SuiteID.
- Providers register per SuiteID, not per algorithm name.

#### 16.6.2 Hash Suite Taxonomy

SMO divides hashes into two categories:

| Category | Purpose | Examples | Used For |
|---|---|---|---|
| **Cryptographic** | Security-critical, collision-resistant | BLAKE3-256, SHA-256, SHA3-256 | NodeID, ContractID, ManifestID, signatures, audit chain, integrity |
| **Performance** | Speed-critical, non-security | xxHash3, CRC32C, CityHash | DAG cache keys, hash tables, bloom filters, checksums |

#### 16.6.3 Three Official Cryptographic Hash Suites

SMO defines exactly 3 cryptographic hash suites. No more.

| HashSuiteID | Name | Algorithm | Digest | Status | Use Case |
|---|---|---|---|---|---|
| 1 | `HASH_BLAKE3` | BLAKE3 | 256 bits | **Default** | SMO native — fastest, parallel, CC0, XOF |
| 2 | `HASH_SHA256` | SHA-256 | 256 bits | Required | FIPS, OpenSSL, TPM, HSM, AWS KMS, PKCS#11, TLS, X.509, SSH, Git, Docker |
| 3 | `HASH_SHA3_256` | SHA3-256 (Keccak) | 256 bits | Optional | NIST standard, hardware ASIC, government, defense |

**Suite 1 — BLAKE3-256 (Default)**
- Speed: ~4–8 GB/s (tree hash, parallelizable). SHA-256 is ~1 GB/s (linear).
- XOF: Extensible output (any length), useful for key derivation and cache keys.
- License: CC0 1.0 (public domain), no copyleft restrictions.
- Incremental: Can hash streaming data without holding everything in memory.
- Every component defaults to BLAKE3-256. Only explicit configuration changes it.

**Suite 2 — SHA-256 (FIPS Compatibility)**
- Required because the entire external world (OpenSSL, Linux Kernel, TPM, HSM, AWS KMS, Azure, PKCS#11, TLS, X.509, SSH, Git, Docker) understands SHA-256.
- If government/military/FIPS mandates "no BLAKE3", recompile with Hash Suite 2. Zero business logic changes.
- Slower than BLAKE3 (~1 GB/s), but universally accepted.

**Suite 3 — SHA3-256 (NIST Future-Proof)**
- NIST standard (Keccak), hardware ASIC support expected.
- Slower than both BLAKE3 and SHA-256, but mandated in some defense/government contexts.
- Included for long-term readiness.

#### 16.6.4 Explicitly Excluded Algorithms

| Algorithm | Reason |
|---|---|
| **BLAKE2** | Strictly dominated by BLAKE3 in speed, parallelism, XOF, and simplicity. Legacy only. |
| **MD5** | Broken. Collision attacks since 2004. Never used. |
| **SHA-1** | Broken. SHAttered (2017), SHambles (2020). Never used. |
| **Whirlpool** | Niche. No hardware support. No ecosystem advantage over SHA-3. |
| **xxHash** (for crypto) | Non-cryptographic. Not collision-resistant. Cannot be used for NodeID, signatures, or integrity. |

#### 16.6.5 Performance Hash Category

For non-security uses (DAG cache keys, hash tables, bloom filters, checksums), SMO uses **performance hashes**:

| HashSuiteID | Name | Speed | Use Case |
|---|---|---|---|
| 101 | xxHash3 | ~30 GB/s | Cache keys, hash tables, fast checksums |
| 102 | CRC32C | hardware | SSE 4.2 / ARM CRC, network checksums |
| 103 | CityHash | ~15 GB/s | Hash tables (Google-originated) |

Performance hashes do NOT flow through the `HashProvider` abstraction. They are used directly in data structures via `PerformanceHashImpl` (see `core/crypto/impl.hpp`):

```cpp
struct PerformanceHashImpl {
    uint64_t (*hash64)(BytesView data) = nullptr;
    uint32_t (*hash32)(BytesView data) = nullptr;
};
```

The `CryptoProvider` struct bundles both `HashImpl` (cryptographic) and `PerformanceHashImpl` (non-cryptographic). A provider may set `perf_hash` to null; only the cryptographic `hash`/`hmac` are required.

#### 16.6.6 C++ Interface

```cpp
// core/crypto/fwd.hpp
enum class HashSuite : uint16_t {
    Blake3     = 1,   // default SMO native
    Sha256     = 2,   // FIPS compatibility
    Sha3_256   = 3,   // NIST future-proof
    // Performance hashes
    XxHash3    = 101,
    Crc32C     = 102,
    CityHash   = 103,
};

inline constexpr bool is_crypto_hash(HashSuite s) noexcept {
    return s == HashSuite::Blake3 || s == HashSuite::Sha256 || s == HashSuite::Sha3_256;
}
inline constexpr bool is_performance_hash(HashSuite s) noexcept {
    return s == HashSuite::XxHash3 || s == HashSuite::Crc32C || s == HashSuite::CityHash;
}
```

#### 16.6.7 Architecture Summary

```
┌─────────────────────────────────────────────────────────┐
│  Consumer: ContractID, NodeID, ManifestID, DAG cache    │
│  ─────────────────────────────────────────────────────  │
│  Uses: HashProvider::hash(data) → 32 bytes              │
│         HashProvider::hash_hex(data) → 64-char hex      │
└──────────────────────┬──────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────┐
│  HashProvider (abstract interface)                       │
│  core/crypto/hash_provider.hpp                           │
│  HashSuiteID determines which concrete impl to call      │
└──────────────────────┬──────────────────────────────────┘
                       │
          ┌────────────┼────────────┐
          │            │            │
          ▼            ▼            ▼
┌────────────────┐ ┌──────────┐ ┌──────────────┐
│ HashSuite 1    │ │Suite 2   │ │ Suite 3      │
│ Blake3Provider │ │SHA256    │ │ SHA3_256     │
│ providers/     │ │(future)  │ │ (future)     │
│ blake3_provider│ │          │ │ sha3_provider│
└───────┬────────┘ └──────────┘ └──────────────┘
        │
        ▼
┌─────────────────────────────────────────────────────┐
│  third_party/blake3/  (official BLAKE3 C, SIMD:      │
│  SSE2, SSE4.1, AVX2, AVX512, NEON)                  │
└─────────────────────────────────────────────────────┘
```

**Rules:**
1. Every business-logic component that needs hashing MUST depend on `HashProvider`, never on a specific algorithm.
2. `HashProvider` is registered at startup via `HashProviderRegistry` singleton (default = HashSuite 1 / BLAKE3-256).
3. Swapping to SHA-256 (FIPS) or SHA3-256 (NIST) requires only: write a new provider → register it → recompile. No business logic changes.
4. The concrete provider lives in `providers/`, NOT in `core/`. Third-party hash sources live in `third_party/`, NOT in `core/crypto/`.
5. Performance hashes use `PerformanceHashImpl` (function-pointer table), NOT the virtual `HashProvider` interface. They are non-cryptographic and used only for data structures.
6. **No BLAKE2.** Blake3 strictly dominates BLAKE2 in speed, parallelism, XOF, and simplicity. BLAKE2 is legacy.
7. **No xxHash** for cryptographic purposes. Despite its speed, xxHash is not collision-resistant and must never be used for NodeID, ContractID, signatures, or integrity.

### 16.7 Crypto Utility Layer

SMO provides a set of small, auditable crypto utilities in `core/crypto/`:

| Component | File(s) | Purpose |
|---|---|---|
| **SecureBuffer** | `secure/zeroize.hpp/.cpp` | RAII buffer that zeroes memory on destruction. Uses volatile memset to prevent compiler elision. |
| **constant_time_compare** | `secure/secure_compare.hpp/.cpp` | Timing-safe memory comparison. Used for MAC verification, key confirmation. |
| **CSPRNG** | `random/getrandom.hpp/.cpp` | Wraps Linux `getrandom()` syscall. Used for key generation, nonce generation, seed material. |
| **HKDF** | `kdf/hkdf.hpp/.cpp` | HMAC-based Key Derivation (RFC 5869). Uses **fixed HMAC-SHA256** regardless of active hash suite. This guarantees session key derivation is identical across all suites. |

**Rules:**
1. Every key in RAM MUST be stored in a `SecureBuffer` and zeroed after use.
2. Every MAC comparison MUST use `constant_time_compare`, never `memcmp`.
3. All random bytes MUST come from `random::fill()`, never from `rand()`, `random_device`, or `std::mt19937`.
4. Session keys MUST be derived via HKDF from the KEM shared secret, never used raw.

### 16.8 Session Binding (Certificate → Session)

After the certificate is established, session-level authentication uses a **signed nonce** challenge:

```
1. Node A connects to Node B
2. Node B sends a random nonce (32 bytes)
3. Node A signs the nonce with its Ed25519 private key
4. Node B verifies:
   a. Signature matches Node A's PublicKey
   b. Node A's PublicKey matches the Membership Certificate
   c. Membership Certificate chain verifies up to Root
   d. Certificate Epoch >= mesh CurrentEpoch
   e. Certificate has not expired
5. → Session established
```

Two independent security layers are required: both the certificate (membership) and the signed nonce (key possession) must be valid.

---

## XVII. FILESYSTEM EVOLUTION MODEL

### Phase 1 — Shared Root (MVP)

- Single shared workspace: `/myshared`
- All file operations are sandboxed within this directory.

### Phase 2 — Multi-Root Capability

- Multiple roots: `/shared`, `/var/log`, `/opt/runtime`
- Each root is capability-scoped.

### Phase 3 — System Execution Domain

- Full controlled execution: process execution, service orchestration, runtime isolation, incident containment.

---

## XVIII. NODE LIFECYCLE

The full node identity lifecycle is defined in §VII.10 (Node Identity Lifecycle). This section provides a summary reference.

**States:**

```
NEW → KEY_GENERATED → JOINED → ACTIVE ↔ SUSPENDED
                                  ↓
                            KEY_ROTATING → KEY_ROTATED → ACTIVE
                                  ↓
                            RETIRED (permanent)
```

**Transitions:**

| From | Event | To | Reference |
|---|---|---|---|
| NEW | `smo-node init` | KEY_GENERATED | §IX Enrollment |
| KEY_GENERATED | Enrollment success | JOINED | §IX Enrollment |
| JOINED | Session established | ACTIVE | §XVI.5 Session Binding |
| ACTIVE | Trust < threshold | DEGRADED | §XII Trust |
| ACTIVE | Admin suspension | SUSPENDED | §VII.10.4 |
| ACTIVE | Network loss | OFFLINE | §VI.8 Discovery |
| ACTIVE | Key compromise | KEY_ROTATING | §VII.10.2 |
| KEY_ROTATING | New cert issued | ACTIVE | §VII.10.2 |
| ACTIVE | Admin retire | RETIRED | §VII.10 |
| SUSPENDED | Admin unsuspend | ACTIVE | §VII.10.4 |
| OFFLINE | Reconnect | ACTIVE | §VI.8.5 Leave Detection |

See §VII.10 for complete lifecycle FSM with all transitions and failure cases.

---

## XIX. OPCODES

### 19.1 Hierarchical Namespace

All opcodes use a two-level namespace. The first byte is the namespace, the second byte is the message within that namespace.

### 19.2 Discovery Protocol Opcodes

| ID | Message | Description | Transport |
|---|---|---|---|
| 0x01 0x01 | HELLO | Session initiation over UDP | UDP |
| 0x01 0x02 | DISCOVER | Find nodes on the mesh | UDP |
| 0x01 0x03 | NODE_INFO | Node metadata exchange | UDP |
| 0x01 0x04 | PING | Liveness check | UDP |
| 0x01 0x05 | OFFLINE | Graceful departure announcement | UDP |

### 19.3 Control Protocol Opcodes

| ID | Message | Description | Transport |
|---|---|---|---|
| 0x02 0x01 | CONTRACT_PROPOSAL | Propose a contract | TCP |
| 0x02 0x02 | CONTRACT_ACCEPT | Accept proposed contract | TCP |
| 0x02 0x03 | CONTRACT_REJECT | Reject proposed contract | TCP |
| 0x02 0x04 | CONTRACT_RESULT | Report execution result | TCP |
| 0x02 0x10 | SESSION_OPEN | Open a new session | TCP |
| 0x02 0x11 | SESSION_CLOSE | Close an existing session | TCP |
| 0x02 0x12 | SESSION_RENEW | Renew a session | TCP |
| 0x02 0x20 | CSR | Certificate Signing Request | TCP |
| 0x02 0x21 | CERTIFICATE | Certificate delivery | TCP |
| 0x02 0x30 | WITNESS_REQUEST | Request witness attestation | TCP |
| 0x02 0x31 | WITNESS_RESPONSE | Witness attestation response | TCP |
| 0x02 0x40 | CAP_GRANT | Grant capabilities to a node | TCP |
| 0x02 0x41 | CAP_REVOKE | Revoke capabilities from a node | TCP |
| 0x02 0x50 | REVOKE_CERT | Revoke a specific certificate | TCP |
| 0x02 0x51 | EPOCH_INCREMENT | Increment mesh epoch | TCP |
| 0x02 0x60 | TRUST_DIGEST | Gossip trust digest | TCP |

### 19.4 Execution Protocol Opcodes

| ID | Message | Description | Transport |
|---|---|---|---|
| 0x03 0x01 | EXEC_START | Begin execution | TCP |
| 0x03 0x02 | EXEC_PROGRESS | Execution progress update | TCP |
| 0x03 0x03 | EXEC_EVENT | Execution event (stdout, status) | TCP |
| 0x03 0x04 | EXEC_RESULT | Final execution result | TCP |
| 0x03 0x05 | EXEC_CANCEL | Cancel execution | TCP |
| 0x03 0x06 | EXEC_TIMEOUT | Execution timed out | TCP |
| 0x03 0x07 | EXEC_ERROR | Execution error | TCP |

### 19.5 Data Protocol Opcodes

| ID | Message | Description | Transport |
|---|---|---|---|
| 0x04 0x01 | CHANNEL_OPEN | Open a data transfer channel | TCP |
| 0x04 0x02 | CHUNK | Data chunk | TCP |
| 0x04 0x03 | ACK | Acknowledge chunk receipt | TCP |
| 0x04 0x04 | NACK | Negative acknowledgment | TCP |
| 0x04 0x05 | FIN | End of data stream | TCP |
| 0x04 0x06 | CANCEL | Cancel data transfer | TCP |

### 19.6 Opcode Categories (Application-Level)

These map to the original SM opcodes but are expressed as contract parameters, not wire-level opcodes:

| Opcode | Description | Requires | Idempotent |
|---|---|---|---|
| LS | List directory contents | CAP_FS_READ | Yes |
| PUT | Write file | CAP_FS_WRITE | Yes (same content) |
| GET | Read file | CAP_FS_READ | Yes |
| EXEC | Execute a command | CAP_PROC_EXEC | No (explicitly non-idempotent) |
| QUARANTINE | Isolate a node | CAP_NODE_QUARANTINE | No |
| MKDIR | Create directory | CAP_FS_WRITE | Yes |
| RM | Remove file or directory | CAP_FS_WRITE | No |
| CP | Copy file | CAP_FS_WRITE | Yes |
| CUSTOM | User-defined operation (plugin) | CAP_CUSTOM_CONTRACT | (declared by plugin) |

### 19.7 Opcode Replay Rule

Every opcode MUST be either:
- **replay-safe**: executing it N times produces the same effect as executing it once, OR
- **explicitly declared non-idempotent**: the system documents that replay has side effects.

### 19.8 Opcode Registry

The Opcode Registry is a **syscall-table-like registry** mapping `OpcodeID → handler metadata`. It lives in `core/opcode/` and is populated at startup from two sources:

1. **Builtin opcodes:** hardcoded in `opcode.h` (LS=0x01, PUT=0x02, etc.)
2. **Plugin opcodes:** registered by loaded plugins via `register_opcode()` at init time

#### OpcodeEntry schema

```cpp
struct OpcodeEntry {
    Opcode       id;              // 0x01–0xEF builtin, 0xF0–0xFE plugin, 0xFF custom
    std::string  name;            // "ls", "put", "get", ...
    std::string  semver;          // opcode version
    uint32_t     capability_mask; // required capabilities
    bool         idempotent;
    std::string  contract_id;     // default ContractID for this opcode
    std::string  plugin_id;       // empty for builtin
    std::vector<std::string> supported_arches;
};
```

#### Opcode range allocation

| Range | Owner | Registration |
|---|---|---|
| 0x01–0xEF | Builtin SMO opcodes | Hardcoded in `opcode.h` |
| 0xF0–0xFA | Reserved for future builtin | — |
| 0xFB–0xFE | Plugin opcodes | `register_opcode()` at plugin load |
| 0xFF | Custom/user-defined | Reserved for dynamic dispatch |

#### Opcode → Contract resolution

Opcodes and Contracts are **1:N**. A single opcode (e.g. `ls`) can have multiple contract implementations. The Contract Factory selects which ContractID to use based on:
- The Intent's opcode
- The Intent's `contract_hint` (optional, user-specified)
- The node's local Registry state (latest compatible active version)

---

## XX. CLI DESIGN

### 20.1 Entry Points

| Binary | Purpose |
|---|---|
| `smo-cli` | User-facing execution tool |
| `smo-node` | Node daemon |
| `smo-admin` | Mesh administration |
| `smo-debug` | Internal tracing and debugging |

### 20.2 CLI Commands

```
# Mesh lifecycle
smo mesh create --name "SOC-Production" --recovery-group 5 --threshold 3
smo mesh discover
smo node join --mesh SOC --token abc123
smo node leave --mesh SOC
smo node import-manifest mesh.yaml

# Context switching
smo ctx use SOC-Production
smo ctx list

# Enrollment
smo-node init
smo export --format text > join_request.smor
smo-admin sign join_request.smor
smo-node import node.smoc

# Key lifecycle
smo node rotate-key                          # initiate key rotation
smo node renew-cert                          # renew cert without key change
smo admin suspend --node node-x              # temporary deactivation
smo admin unsuspend --node node-x            # reactivate
smo admin retire --node node-x               # permanent removal
smo admin emergency-rotate --node node-x     # forced rotation (compromise)

# Execution
smo exec --mesh SOC --target node-a --opcode LS --path /var/log
smo exec --file contract.json

# Administration
smo admin authority issue --pubkey candidate.pub
smo admin authority revoke --node node-x
smo admin grant-cap --node node-b --cap CAP_FS_WRITE
smo admin revoke-cap --node node-x --cap CAP_PROC_EXEC
smo admin epoch increment
smo admin recover --shares 3-of-5

# Governance
smo gov propose --action policy-change --file new-policy.yaml
smo gov sign --governance-id gov-001
smo gov status --governance-id gov-001

# Trust
smo trust inspect node-a
smo trust peers

# Mesh operations
smo quarantine node-c
```

---

## XXI. OBSERVABILITY

Observability is mandatory, not optional.

```
tooling/
├── tracing/             Distributed tracing (trace IDs propagated through contracts)
├── metrics/             Performance and health metrics
├── profiling/           CPU and memory profiling
└── audit-viewer/        Audit log browser
```

Without observability, distributed systems become unmanageable.

---

## XXII. TESTING MODEL

```
tests/
├── unit/                Per-module unit tests
├── integration/         Cross-module integration tests
├── mesh/                Multi-node mesh topology tests
├── chaos/               CRITICAL — simulate network delay, node compromise, split-brain, divergence
├── replay/              Deterministic replay from audit logs
└── adversarial/         Security-focused adversarial scenarios
```

### 22.1 Chaos Testing Requirements

The chaos test suite MUST simulate:
- Delayed packets
- Compromised nodes
- Split-brain conditions
- State divergence between nodes
- Authority failure and recovery
- Root key loss and M-of-N recovery
- Requester crash during contract proposal
- Responder crash mid-execution (orphan contract)
- Witness timeout and fallback
- Network partition with conflicting governance decisions
- Half-execution with process orphan
- Clock drift beyond ±300s window
- Duplicate packet delivery (sequence number replay)
- Epoch mismatch after partition heal
- Certificate expiry during active session
- Key rotation during active contract execution
- Resource exhaustion (OOM, disk full) during EXEC
- Plugin crash (segfault in C ABI plugin)
- Governance proposal expiry before threshold met

---

## XXIII. MVP SCOPE

### 23.1 Deliverables

1. **5 application opcodes**: LS, PUT, GET, EXEC, QUARANTINE
2. **1 killer workflow**: Incident Containment
   ```
   Detect suspicious node
       ↓
   Reduce trust score
       ↓
   Revoke capabilities
       ↓
   Quarantine node
   ```
3. Transport + Session + Signatures operational (TCP, UDP discovery)
4. Single-node FSM operational (including enrollment flow)
5. Contract proposal/acceptance/rejection flow operational
6. Witness protocol (basic, single-witness, no fallback required for MVP)
7. Identity system: node init, keypair generation, enrollment (.smor/.smoc)
8. Single mesh with single Authority
9. **Time Model**: monotonic clock for execution timeouts, sequence numbers for ordering, wall clock for security windows only
10. **Version Negotiation**: TLS-style version handshake on first connection
11. **Mesh Manifest**: mesh.yaml import at join time
12. **Node Identity Lifecycle**: JOINED → ACTIVE → OFFLINE (minimal subset)

### 23.2 Out of MVP Scope

- Full DAG scheduler with runtime policies (priority, preemption, quota)
- Trust engine with all four components
- Consensus weighting
- DAG optimizer
- .seme DSL (JSON/YAML only)
- WASM sandbox
- Multi-root filesystem (Phase 1 only: /myshared)
- Windows/macOS adapters
- Multiple Authority support
- Recovery Authorities (threshold recovery)
- Capability Epoch (manual revocation only)
- STUN/ICE/NAT connectivity (direct TCP only)
- Mesh context switching (single mesh per node)
- REVOKE_CERT and EPOCH_INCREMENT (manual revocation only)
- **Mesh Governance** (multi-signature decisions)
- **Mesh Discovery Engine** (SWIM gossip deferred; basic UDP HELLO/PING only)
- **Routing Layer** (direct TCP only; no relay selection or path scoring)
- **Failure Model** (basic timeout handling only; no systematic failure scenarios)
- **Resource Model** (no cgroup/namespace enforcement; best-effort only)
- **Plugin ABI** (MVP plugins are compile-time linked; no C ABI hot-loading)
- **Policy Engine** (policy is hardcoded capability checks; no rule language)

---

## XXIV. IMPLEMENTATION ORDER

DO NOT start with trust AI, fancy consensus, or complex DAG optimization.

**Cross-cutting (applied from Stage 1 onward):** Failure Model, Time Model, Version Negotiation — these are designed upfront and refined in each stage.

### Stage 1 — Foundation

```
identity + transport + session + signature + time + version
```
- Ed25519 keypair generation
- TCP transport + version handshake
- Session establishment (signed nonce)
- Discovery protocol (basic UDP HELLO/PING)
- Time Model: monotonic clock for timeouts, sequence numbers for ordering
- Version Negotiation: TLS-style version list in first handshake packet

### Stage 2 — Node Core

#### Sprint 2.1 — Protocol

```
packet serialization + signing + encryption + replay protection + schema
```
- PacketHeader (37-byte fixed header) + variable payload + 64-byte signature
- Ed25519 signing and verification
- XChaCha20-Poly1305 encryption
- ReplayProtector (nonce + timestamp window)
- Schema helpers

#### Sprint 2.2 — Storage

```
SqliteStore KV backend + 5 store implementations
```
- session_store, audit_store, node_store, dag_store, trust_store
- SqliteStore: Init, Get, Set, Delete, Exists
- Migration support (schema version tracking)

#### Sprint 2.3 — Transport High-Level

```
transport abstraction + framing + TCP transport + version handshake
```
- Framing layer (message length prefix, version byte)
- TCP transport: listen/connect/send/close with poll timeout
- Version handshake during connect
- Callback-based event model

#### Sprint 2.4 — Contract Architecture RFC

```
RFC 0023 + SPEC.md §X update
```
- Write Contract Architecture RFC (Intent model, 3 contract categories, ContractID, Registry, Compiler boundary, DAG cache)
- Update SPEC.md §X, §XIII, §XIX, §IV, §V with new architecture

#### Sprint 2.5 — Contract Registry + Opcode Registry

```
ContractRegistry + OpcodeRegistry + ContractFactory + Intent extensions
```
- `core/opcode/opcode_registry.hpp` — OpcodeRegistry class with builtin registration + plugin registration
- `core/contract/contract_id.hpp` — ContractID type with HashProvider computation
- `core/contract/contract_definition.hpp` — ContractDefinition struct with canonical JSON serialization
- `core/intent/intent.h` — extend Intent with optional `contract_hint` field
- `contract/registry/contract_registry.hpp` — ContractRegistry with Publish, Resolve, Get, Deprecate, Sync
- `contract/registry/native.cpp` — Native contract registration (ls, put, get, exec, quarantine)
- `contract/factory/contract_factory.hpp` — ContractFactory resolving Intent → ContractDefinition
- **Hash Suite architecture:** `core/crypto/fwd.hpp` — HashSuite enum, `core/crypto/suite.hpp` — HashSuiteID constants, `core/crypto/impl.hpp` — PerformanceHashImpl, SPEC.md §16.6 update with Hash Suite taxonomy
- Tests: ContractID computation, registry CRUD, opcode registration, factory resolution

#### Sprint 2.6 — Contract ABI (RFC 0026)

```
core/contract/contract_abi.hpp + ContractABI struct + ABI Hash
```
- RFC 0026: Contract ABI Specification
- `core/contract/contract_abi.hpp/.cpp` — ContractABI struct, Schema, AbiRegistry
- `abi_hash` = BLAKE3(canonical ABI JSON)
- `semantic_hash` = BLAKE3(abi_hash + contract body)
- Runtime checks ABI Hash match → skip re-parse
- SDK code generation from ABI (future)

#### Sprint 2.7 — Compiler Pipeline

```
Parser → AST → SMIR → Semantic Validator → Planner → Builder → Optimizer → Final Validator → DAG
```
- RFC 0025: Contract Runtime Architecture
- `compiler/ast/ast.hpp/.cpp` — Contract AST node types
- `compiler/smir/smir.hpp/.cpp` — SMIR opcodes, operands, basic blocks, AST → SMIR lowering
- `compiler/parser/parser.hpp/.cpp` — JSON → AST (real impl, replaces stub)
- `compiler/validator/semantic.hpp/.cpp` — Pass 1: ABI hash match, capability check, opcode dep check
- `compiler/validator/final.hpp/.cpp` — Pass 2: acyclic check, max depth/width, node reachability
- `compiler/planner/planner.hpp/.cpp` — Target node selection, shard mapping (replaces stub)
- `compiler/graph/builder.hpp/.cpp` — SMIR → ExecutionGraph
- `compiler/optimizer/optimizer.hpp/.cpp` — Prune, merge reads, constant fold (replaces stub)
- `compiler/compiler.cpp` — Concrete Compiler class tying all stages
- Tests: Parser round-trip, SMIR lowering, Semantic/Final Validator, full pipeline

#### Sprint 2.8 — Executor + Kernel Contracts

```
Runtime::execute(ContractID, ExecutionContext) + polymorphic kernel contracts
```
- `runtime/executor/executor.hpp/.cpp` — resolve ContractID → DAG cache → execute DAG
- `runtime/sandbox/sandbox.hpp/.cpp` — Seccomp/namespace isolation per task node
- `runtime/workerpool/workerpool.hpp/.cpp` — Thread pool with task-level parallel dispatch
- `runtime/runtime.hpp/.cpp` — `ExecutionResult execute(const ContractID&, const ExecutionContext&)`
- Kernel contracts registered polymorphically (zero opcode hardcode):
  `ping`, `whoami`, `session_open`, `session_close`, `discover`, `node.info`, `identity.rotate`
- `core/contract/contract_interface.hpp` — Abstract Contract interface
- Contract category gating: Kernel = inline, Native = capability gate, Mesh/Private = full sandbox
- Tests: Executor dispatch via Runtime::execute(), kernel contract execution

#### Sprint 2.9 — Discovery Completion

```
handle_ping/handle_pong → real response + gossip piggyback + seed fallback
```
- `core/discovery/discovery.cpp` — handle_ping/handle_pong from no-op to real response
- `core/discovery/gossip.hpp/.cpp` — SWIM gossip piggyback membership updates on ping/pong
- Seed priority fallback (DNS → hardcoded → seed list)
- Tests: Ping/pong round-trip, gossip propagation

### Stage 3 — Multi-Node

```
multi-node propagation + witness + discovery engine
```
- Multi-node contract flow
- Basic witness protocol
- Control protocol over TCP
- Routing Layer: Peer Record, multi-address, basic path selection

### Stage 4 — DAG

```
DAG scheduler + runtime policies
```
- Intent → DAG compiler
- DAG-aware scheduler
- Execution protocol
- Scheduler policies: priority queues, retry, backoff, cancellation, deadline

### Stage 5 — Trust

```
trust engine + membership protocol
```
- Trust scoring (all four components)
- SWIM-inspired membership (gossip, suspicion, indirect ping)
- Heartbeat protocol over UDP

### Stage 6 — Full Networking

```
connectivity + data protocol + routing
```
- STUN client (RFC 8489)
- ICE candidate gathering (RFC 8445)
- UDP hole punch
- TURN relay (RFC 8656)
- Data protocol (chunked transfer)
- Routing: dynamic re-routing, relay selection, path scoring

### Stage 7 — Governance

```
multiple authorities + epoch + recovery + governance protocol
```
- Multiple Authority support
- Capability Epoch
- Recovery Authorities (Shamir threshold)
- REVOKE_CERT and EPOCH_INCREMENT
- Governance Protocol: multi-signature decisions, policy change, M-of-N
- Plugin ABI: C ABI, extern "C" entry points

### Stage 8 — Policy Engine (Post-MVP)

```
policy language + runtime evaluation
```
- Policy rule language (OPA-like)
- Runtime policy evaluation per contract
- Custom presets and organization policies

---

## XXV. GOLDEN ENGINEERING RULES

### Rule 1: Invariant First
Invariants MUST be defined and enforced before features are added. Never add a feature that violates an invariant.

### Rule 2: Opcode Replay Safety
Every opcode must be replay-safe or explicitly declared non-idempotent. This is not a guideline; it is a requirement.

### Rule 3: No Silent Mutation
No component may silently mutate state. ALL mutations must be recorded and auditable. EVER.

### Rule 4: Trust Is Eventually Consistent
Trust is NOT absolute truth. It is a local, eventually-consistent estimate. No execution decision may depend on trust alone.

### Rule 5: Execution Graph Immutability
The execution graph is immutable after compilation. No runtime component may modify a compiled graph.

### Rule 6: Private Key Never Travels
No private key ever leaves the node on which it was generated. All identity proof is via signed challenges.

### Rule 7: Certificate Chain Verifiable by All
Every Membership Certificate must be verifiable up to the Root Public Key by any node in the mesh.

### Rule 8: Wall Clock Is Never Trusted for Ordering
System time (`system_clock`) is used ONLY for security windows (anti-replay) and human-readable timestamps. Ordering decisions rely on sequence numbers or logical clocks. Monotonic clock is used for timeouts and deadlines.

### Rule 9: Every FSM State Has a Timeout and a Failure Transition
No FSM state may block indefinitely. Every state MUST define a maximum dwell time and a transition to a safe fallback state when the timer expires. The default fallback is REJECTED or OFFLINE.

### Rule 10: Governance Signature Threshold Is Defined by the Mesh Manifest
Governance decisions are classified into 5 levels (Local/Authority/Policy/Critical/Genesis). Each level's signature threshold is defined in the Mesh Manifest, not hardcoded. Level 0 requires no signature. Level 1 requires 1 Authority. Levels 2-3 require M-of-N. Level 4 (Genesis) uses the Root Key once.

### Rule 11: Resource Constraints Are Declared, Not Inferred
Every execution MUST declare its resource requirements (CPU, RAM, timeout, priority) at contract submission time. The Responder uses these declarations for scheduling and enforcement. Undeclared resource use is bounded by the node's default quota.

---

## XXVI. FINAL ARCHITECTURE VIEW

```
                       ┌──────────────────────────────────────────────┐
                       │              APPLICATION                     │
                       │  (incident response, fleet ops, ...)        │
                       └──────────────────┬───────────────────────────┘
                                          │
                       ┌──────────────────▼───────────────────────────┐
                       │   CLI / SDK · smo-cli · smo-admin · smo-node │
                       └──────────────────┬───────────────────────────┘
                                          │  produces
                                          ▼
                       ┌──────────────────────────────────────────────┐
                       │          INTENT LAYER                        │
                       │  Intent (opcode + targets + params)          │
                       │  Contract Factory → ContractID               │
                       └──────────────────┬───────────────────────────┘
                                          │  resolves
                                          ▼
                       ┌──────────────────────────────────────────────┐
                       │   CONTRACT REGISTRY + OPCODE REGISTRY        │
                       │   Immutable · Append-only · Blake3-addressed │
                       │   Syscall-table: OpcodeID → handler metadata │
                       └──────────────────┬───────────────────────────┘
                                          │  compiles (node-local)
                                          ▼
                       ┌──────────────────▼───────────────────────────┐
                       │   COMPILER + DAG CACHE                       │
                       │   Contract + Params + Env → DAG              │
                       │   Cache keyed by (ContractID, env_fp)       │
                       └──────────────────┬───────────────────────────┘
                                          │
                       ┌──────────────────▼───────────────────────────┐
                       │   SCHEDULER · FSM                            │
                       │   DAG-aware · Priority · Retry · Cancel     │
                       │   Node FSM · Mesh FSM · Resource Model      │
                       └──────────────────┬───────────────────────────┘
                                         │
                      ┌──────────────────▼───────────────────────────┐
                      │   GOVERNANCE PROTOCOL (multi-sig)            │
                      │   Authority · Epoch · Policy Change         │
                      │   Mesh Split/Merge · M-of-N                 │
                      └──────────────────┬───────────────────────────┘
                                         │
                      ┌──────────────────▼───────────────────────────┐
                      │   CONTROL PROTOCOL (TCP)                     │
                      │   Contract · Session · Witness               │
                      │   CSR · Certificate · Revoke · Cap Grant    │
                      └──────────────────┬───────────────────────────┘
                                         │
                      ┌──────────────────▼───────────────────────────┐
                      │   EXECUTION PROTOCOL (TCP)                   │
                      │   Start · Progress · Event · Result         │
                      └──────────────────┬───────────────────────────┘
                                         │
              ┌──────────────────────────┼──────────────────────────┐
              │                          │                          │
   ┌──────────▼────────────┐ ┌───────────▼───────────┐ ┌───────────▼───────────┐
   │  DISCOVERY ENGINE     │ │  DATA PROTOCOL (TCP)  │ │  WITNESS              │
   │  SWIM gossip          │ │  CHUNK · ACK · FIN    │ │  Attest · Verify      │
   │  Bootstrap · Health   │ │                       │ │                       │
   │  Routing Layer        │ │                       │ │                       │
   └──────────┬────────────┘ └───────────┬───────────┘ └───────────┬───────────┘
              │                          │                          │
   ┌──────────▼──────────────────────────▼──────────────────────────▼───────────┐
   │                          SESSION LAYER                                      │
   │  Keys · Certificate binding · Capability negotiation · Signed nonce       │
   │  Version Negotiation · Time Model (3 domains)                              │
   └─────────────────────────────────┬──────────────────────────────────────────┘
                                     │
   ┌─────────────────────────────────▼──────────────────────────────────────────┐
   │                      CONNECTIVITY LAYER                                     │
   │  STUN (RFC 8489) · ICE (RFC 8445) · NAT hole punch                        │
   │  TURN relay (RFC 8656)                                                      │
   └─────────────────────────────────┬──────────────────────────────────────────┘
                                     │
   ┌─────────────────────────────────▼──────────────────────────────────────────┐
   │                       TRANSPORT LAYER                                      │
   │  TCP · UDP · (future: QUIC, Unix socket)                                   │
   └─────────────────────────────────┬──────────────────────────────────────────┘
                                     │
   ┌─────────────────────────────────▼──────────────────────────────────────────┐
   │                  IDENTITY + MEMBERSHIP LAYER                                │
   │  Ed25519 keypairs · Node Identity Lifecycle                                │
   │  Membership Certificates · Chain verification · Multi-tenant               │
   │  Mesh Root · Authority · Epoch · M-of-N Recovery · Mesh Manifest          │
   └────────────────────────────────────────────────────────────────────────────┘
```

**Component References:**

| Layer | SPEC Section |
|---|---|
| Application | §I |
| CLI / SDK | §XX |
| Intent Layer | §X.1, §X.7 |
| Contract Registry + Opcode Registry | §X.6, §XIX.8 |
| Compiler + DAG Cache | §XIII.4, §XIII.5 |
| Scheduler / FSM | §XIII.6, §XIV, §XXX |
| Policy Engine | §XXXII (post-MVP) |
| Governance Protocol | §XXXIII |
| Control Protocol | §VI.1, §XIX.3 |
| Execution Protocol | §VI.1, §XIX.4 |
| Discovery Engine | §VI.8 |
| Data Protocol | §VI.1, §XIX.5 |
| Witness | §XI |
| Session Layer | §XVI.5 |
| Connectivity Layer | §VI.3 |
| Transport Layer | §VI.2 |
| Identity + Membership | §VII, §VII.10, §VII.11 |

---

## XXIX. FAILURE MODEL

Every distributed system is defined by how it fails. This section enumerates every failure scenario that SMO's FSM must handle and defines the required transition for each.

### 29.1 Failure Classification

| Class | Examples | Handling Principle |
|---|---|---|
| Node failure | Requester crash, Responder crash, Witness crash | Timeout → fallback → log |
| Network failure | Partition, packet loss, delay, duplicate | Idempotency + retry + bounded wait |
| Security failure | Key compromise, cert expiry, epoch mismatch | Immediate REJECT + alert |
| Resource failure | OOM, disk full, CPU starvation | Graceful degradation → REJECT |
| Governance failure | Authority death, threshold not met, split brain | M-of-N recovery → manual intervention |

### 29.2 Requester Failure

| Scenario | FSM Transition |
|---|---|
| Requester sends CONTRACT_PROPOSAL then disconnects | Responder waits for `proposal_timeout` (configurable, default 30s). If no CONTRACT_ACCEPT received, transitions to ORPHANED. Orphaned contracts are garbage-collected after `gc_ttl` (default 5min). |
| Requester crashes after receiving CONTRACT_ACCEPT | Responder proceeds with execution normally. Result is stored; delivery to Requester is retried with exponential backoff (max 3 retries). After all retries exhausted, result is available for pull via contract_id. |
| Requester sends duplicate CONTRACT_PROPOSAL (same contract_id) | Responder checks contract_id idempotency store. If already processed, returns cached result. If already pending, returns PENDING status. Never executes twice. |

### 29.3 Responder Failure

| Scenario | FSM Transition |
|---|---|
| Responder crashes mid-execution (after EXEC_START, before EXEC_RESULT) | On restart, Responder reads persisted FSM state. If EXECUTING state found, checks execution outcome via OS (process table, exit code). If process still running, resumes monitoring. If process dead without result, transitions to FAILED with reason "crash during execution." |
| Responder crashes before persisting CONTRACT_ACCEPT | Requester's CONTRACT_PROPOSAL remains unanswered. Requester times out and may retry on another node. |
| Responder runs out of resources during execution | Execution is preempted (if scheduler supports preemption) or killed with EXEC_ERROR "resource exhausted." Contract result: FAILED. |

### 29.4 Witness Failure

| Scenario | FSM Transition |
|---|---|
| Primary witness unreachable | Responder falls back to secondary witness (if configured). If no secondary, proceeds with local decision only. Contract is still executed; witness attestation field is marked as "unavailable." |
| Witness responds after local decision already made | Witness attestation is stored but not used for the current decision. It is available for audit and trust scoring. |
| Witness provides conflicting attestation | Responder trusts its own local observation over witness attestation. Conflict is recorded in audit log. Trust score of witness is penalized. |

### 29.5 Network Failure

| Scenario | FSM Transition |
|---|---|
| Transport disconnects mid-session | Session enters RECONNECTING state. Node attempts reconnection with exponential backoff (1s, 2s, 4s, 8s, max 30s). If reconnected within `session_timeout` (default 60s), session resumes. If not, session is CLOSED. In-flight contracts are handled per Requester/Responder failure rules. |
| Network partition separates mesh into two groups | Each group continues operating independently. No split-brain prevention at this level — governance protocol (multi-signature) prevents conflicting Authority decisions. When partition heals, DAG reconciliation merges contract histories. |
| Duplicate packet arrives | Sequence number tracking per session. Duplicates are silently dropped. |
| Packet delayed beyond timeout | Sender retransmits (Control/Execution/Data protocols over TCP handle this natively). Discovery (UDP) uses sequence numbers and ignore out-of-window packets. |

### 29.6 Security Failure

| Scenario | FSM Transition |
|---|---|
| Certificate expired | Session is REJECTED at connection time. Node must re-enroll. |
| Certificate epoch < mesh current_epoch | Session is REJECTED. Node must request new certificate. |
| Signature verification fails | Packet is dropped. Event is logged as security alert. Repeated failures from same source trigger trust score penalty and potential quarantine. |
| Nonce replay detected | Packet is dropped. Source node's trust score is penalized. |
| Key compromise detected (out-of-band) | Authority issues REVOKE_CERT. Epoch MAY be incremented. Node must re-enroll with new keypair. |

### 29.7 Half-Execution (Orphan Contract)

| Scenario | FSM Transition |
|---|---|
| EXEC_START sent but Responder crashes before execution begins | On restart, Responder loads persisted FSM state. If state is PENDING or VALIDATING, contract is REJECTED with reason "node restart before execution." Requester receives EXEC_ERROR. |
| Execution begins (process spawned) but Responder crashes during execution | On restart, Responder checks if the spawned process still exists. If yes, resumes monitoring. If no, transitions to FAILED. The process may become orphaned — the runtime MUST use OS process groups or cgroup tracking to clean up orphaned children on restart. |
| Requester crashes after receiving EXEC_RESULT but before acknowledging | Responder stores result. Retries delivery with exponential backoff (max 3). After exhaustion, result is available for pull by contract_id. |

### 29.8 Governance Failure

| Scenario | FSM Transition |
|---|---|
| Authority node dies | Other Authorities continue operating. New certificates cannot be issued until the dead Authority is replaced via M-of-N governance. Existing certificates signed by the dead Authority remain valid until their natural expiry or epoch increment. |
| Threshold not met for governance decision | Decision is DEFERRED. Requesters are notified with status "insufficient signatures." A retry with additional signers MAY be attempted. |
| Two Authorities issue conflicting certificates for the same node | The certificate with the higher epoch wins. If epochs are equal, the one signed by the higher-authority issuer wins (Root > Authority). If still tied, the one with the earlier `issued_at` timestamp wins. |

### 29.9 Failure Model Invariants

1. **Safety over liveness**: When in doubt, REJECT. A failed-safe system is better than a failed-unsafe system.
2. **Every state has a timeout**: No FSM state may block indefinitely. See Rule 9.
3. **Idempotency is assumed**: All contract operations are idempotent unless explicitly declared non-idempotent.
4. **Orphans are collected**: Any contract in PENDING or VALIDATING state for longer than `gc_ttl` (default 5min) is garbage-collected.
5. **Trust is adjusted**: Every failure event updates the relevant trust score components.

---

## XXX. RESOURCE MODEL

Resource constraints ensure that one execution cannot starve the node or other executions. Constraints are **declared** in the contract and **enforced** by the runtime.

### 30.1 Resource Declaration

Every contract MAY include a resource declaration:

```json
{
  "contract_id": "...",
  "opcode": "EXEC",
  "parameters": { "command": "..." },
  "resources": {
    "cpu_max": "2",            // CPU cores (fractional: 0.5)
    "cpu_quota_us": 100000,    // CFS quota (Linux cgroup v2)
    "ram_max": "512MB",        // memory limit
    "ram_swap_max": "1GB",     // memory + swap limit
    "io_max": "100MB/s",       // I/O bandwidth limit
    "timeout_sec": 300,        // maximum wall-clock execution time
    "grace_sec": 10,           // time allowed for cleanup after timeout
    "priority": 100,           // 0 (lowest) to 255 (highest)
    "nice": 10,                // Linux nice value
    "pid_max": 64,             // max number of child processes
    "network": true            // allow network access
  }
}
```

All fields are optional. Omitted fields inherit the node's default quota.

### 30.2 Default Quota (Per Node)

| Resource | Default (MVP) | Production |
|---|---|---|
| cpu_max | 1 core | Configurable |
| ram_max | 256MB | Configurable |
| timeout_sec | 60 | Configurable |
| priority | 100 | 0–255 |
| max_concurrent_executions | 4 | Configurable |

### 30.3 Enforcement (Linux Reference)

| Resource | Linux Primitive | MVP Support |
|---|---|---|
| CPU | cgroup v2 `cpu.max` | Post-MVP |
| RAM | cgroup v2 `memory.max` | Post-MVP |
| I/O | cgroup v2 `io.max` | Post-MVP |
| Timeout | `alarm()` / `timer_create()` | MVP |
| Priority | `setpriority()` / cgroup v2 `cpu.weight` | Post-MVP |
| Process limit | `setrlimit(RLIMIT_NPROC)` | Post-MVP |
| Network isolation | network namespace | Post-MVP |
| Syscall filter | seccomp | Post-MVP |

**MVP behavior:** Resource declarations are parsed and validated but NOT enforced at the OS level. The runtime logs a warning if limits are exceeded. Hard enforcement begins in Stage 4.

### 30.4 Resource Accounting

Each node tracks resource usage per execution and cumulatively:

```
Node Resource State:
  cpu_used: 2.5 / 8 cores
  ram_used: 1.2 / 4 GB
  active_executions: 3 / 10 max
  quota_by_mesh: { "SOC": { cpu_max: 4, ram_max: "2GB" } }
```

Before accepting a contract, the Responder checks:
1. Does the contract's declared resource fit within available resources?
2. Does the contract's declared resource fit within the mesh's quota?
3. Does the contract's priority justify preempting a lower-priority execution?

If any check fails, the contract is REJECTED with reason "resource exhaustion" or "quota exceeded."

### 30.5 Resource Model Invariants

1. **Declared resources are ceiling, not allocation**: The runtime guarantees the declared maximum, not a reserved minimum.
2. **No overcommit in MVP**: Post-MVP, the runtime MAY overcommit resources based on statistical multiplexing.
3. **Priority is advisory in MVP**: Preemption is not implemented in MVP. Lower-priority executions run to completion before higher-priority ones.

---

## XXXI. PLUGIN ABI

Plugins extend SMO with custom opcodes. The ABI is C, not C++, to ensure cross-language compatibility and stable linking.

### 31.1 Plugin Interface (C ABI)

```c
/* ABI version — incremented on breaking changes */
#define SMO_PLUGIN_ABI_VERSION 1

/* Opaque handle to runtime context */
typedef struct smo_ctx smo_ctx;

/* Error codes */
typedef enum {
  SMO_OK = 0,
  SMO_ERR_UNKNOWN = -1,
  SMO_ERR_INVALID_PARAM = -2,
  SMO_ERR_NOT_FOUND = -3,
  SMO_ERR_PERMISSION = -4,
  SMO_ERR_TIMEOUT = -5,
  SMO_ERR_RESOURCE = -6,
  SMO_ERR_INTERNAL = -7,
} smo_error_t;

/* Plugin descriptor — one per shared object */
typedef struct {
  uint32_t    abi_version;       /* MUST be SMO_PLUGIN_ABI_VERSION */
  const char* name;              /* human-readable plugin name */
  const char* version;           /* plugin version string */
  uint32_t    api_version;       /* plugin's own API version */

  /* Lifecycle */
  smo_error_t (*init)(const smo_plugin_config* config);
  smo_error_t (*shutdown)(void);

  /* Opcode handler — called by runtime for each contract */
  smo_error_t (*execute)(const smo_contract* contract, smo_ctx* ctx);

  /* Health check — called by runtime periodically */
  smo_error_t (*health)(char* status_out, size_t status_max);
} smo_plugin_v1;
```

### 31.2 Plugin Lifecycle

```
LOAD      → dlopen() / LoadLibrary()
  ↓
INIT      → plugin->init(config)
  ↓
ACTIVE    → plugin->execute(contract, ctx)  // called per contract
  ↓            plugin->health(status)       // called every heartbeat interval
SHUTDOWN  → plugin->shutdown()
  ↓
UNLOAD    → dlclose()
```

### 31.3 Language Bindings

| Language | Binding Mechanism | Status |
|---|---|---|
| C/C++ | Direct `.so` / `.dll` via `dlopen` | Native |
| Rust | `extern "C"` export with `#[no_mangle]` | Wrapper needed |
| Go | `cgo` with `//export` directives | Wrapper needed |
| Zig | `export fn` with C calling convention | Native |
| Python | `ctypes.CDLL` loading `.so` | Wrapper needed |

### 31.4 Sandboxing (Future)

- **MVP**: Plugins are loaded in-process. No sandboxing. Only trusted plugins.
- **Stage 8**: WASM sandbox via the same C ABI. The WASM runtime exports `smo_plugin_v1`; SMO loads it as a standard plugin.
- **Stage 8+**: seccomp + Landlock for filesystem and syscall filtering.

### 31.5 Plugin ABI Invariants

1. **The C ABI is the ONLY stable interface.** C++ plugins must use `extern "C"`.
2. **Plugins never block the runtime.** Long-running operations must use async callbacks.
3. **Plugins must be reentrant.** A single plugin instance may handle multiple contracts concurrently.
4. **ABI version must match.** Runtime rejects plugins with mismatched `abi_version`.

---

## XXXII. POLICY ENGINE (Post-MVP Design)

The Policy Engine is SMO's programmable decision layer. It allows operators to define fine-grained rules for contract acceptance beyond capability checks.

### 32.1 Design Goals

- **Local evaluation**: Policy is evaluated on the Responder node, not shipped to a central server.
- **Composable**: Policies can be combined (AND, OR, NOT) and reference each other.
- **Context-aware**: Policies have access to trust scores, resource state, time, mesh identity, and previous contract outcomes.
- **Fail-closed**: If the policy engine cannot evaluate a rule, the default is REJECT.

### 32.2 Policy Rule Syntax (Design Sketch)

```
// Examples — not final syntax

allow "EXEC on production nodes"
  when opcode == "EXEC"
    and mesh == "SOC-Production"
    and requester.trust > 0.8
    and node.cpu < 80
    and time.between("08:00", "17:00")
    and hostname matches "web-*"

deny "QUARANTINE outside business hours"
  when opcode == "QUARANTINE"
    and not time.between("06:00", "22:00")

allow "FS_READ for SOC analysts"
  when opcode in ["LS", "GET"]
    and requester.role == "CONTRIBUTOR"
    and node.filesystem == "/shared"
```

### 32.3 Policy Evaluation Order

```
1. Capability check (hardcoded — always evaluated first)
2. Certificate check (always evaluated)
3. Policy Engine rules (if configured)
4. Trust threshold check
5. Resource availability check
6. Local decision → ACCEPT or REJECT
```

Policy rules are evaluated at step 3. If no policy engine is configured, evaluation skips to step 4.

### 32.4 Deployment

- Policy is defined in the **Mesh Manifest** (`mesh.yaml`) or in a separate local file.
- Policy is NOT a protocol message. It is local configuration.
- Each node MAY define its own local policy that extends or overrides mesh-level policy.
- Mesh-level policy is signed by M-of-N Authorities and distributed via gossip (GOVERNANCE_PROPOSAL).

### 32.5 Out of MVP Scope

The Policy Engine is explicitly post-MVP. MVP uses hardcoded policy rules:
- Capabilities must match opcode requirements.
- Certificate must be valid.
- Session must be valid.
- Trust must be above local threshold.

---

## XXXIII. MESH GOVERNANCE

SMO is a **distributed incident response runtime**, not a DAO. Governance exists only to manage changes to the mesh itself — it does NOT govern contract execution. Contract execution is governed by capability + policy + trust (§VIII, §XII, §XXXII).

### 33.1 Governance Principle

> **Governance manages mesh-level changes. Contract execution is governed by capability + policy + trust. These are separate layers.**

A node's sovereignty over its own execution decisions is absolute (§I-02). Governance never overrides it.

### 33.2 Five Levels of Governance

Not every decision needs multi-signature. SMO defines five levels:

| Level | Scope | Examples | Required Signers | Configurable |
|---|---|---|---|---|
| **0 — Local** | Node-internal | Node policy, CPU quota, RAM, plugin enable, worker count | **None** (node sovereignty) | Per-node |
| **1 — Authority** | Single-Authority actions | Issue cert, Revoke cert, Grant capability, Revoke capability | **1 Authority** | Mesh manifest |
| **2 — Mesh Policy** | Mesh-wide configuration | Default trust threshold, heartbeat interval, protocol config, capability presets | **M-of-N Authorities** (default: 2-of-3) | Mesh manifest |
| **3 — Critical** | Mesh-level danger | Rotate Mesh Root, Emergency lockdown, Epoch increment, Destroy mesh | **M-of-N Authorities** (default: 3-of-5) | Mesh manifest |
| **4 — Genesis** | Mesh creation | `smo mesh create` | **Root Key only** (single event) | N/A — once only |

**Key rule:** The Root Key is used exactly once — at Genesis (§7.2). After signing the first Authority certificate and exporting the Recovery Package, it is deleted from runtime. All subsequent governance uses Authority-level keys.

### 33.3 Configurable Thresholds

Governance thresholds are defined in the Mesh Manifest (§7.11), NEVER hardcoded:

```yaml
# mesh.yaml — governance section
governance:
  authority:
    issue_cert: 1           # Level 1: single Authority
    revoke_cert: 1          # Level 1: single Authority
    grant_capability: 1     # Level 1: single Authority
  policy:
    threshold: 2            # Level 2: M-of-N
    authority_count: 3      # total Authorities in mesh
  critical:
    threshold: 3            # Level 3: M-of-N
    authority_count: 5      # total Authorities in mesh
    emergency_lockdown: 3   # Level 3: same threshold
```

**Small mesh (1 Authority):**
```yaml
governance:
  authority:
    issue_cert: 1
  policy:
    threshold: 1            # 1-of-1
    authority_count: 1
  critical:
    threshold: 1            # 1-of-1
    authority_count: 1
```

**Large mesh (7 Authorities):**
```yaml
governance:
  authority:
    issue_cert: 1
  policy:
    threshold: 4            # 4-of-7
    authority_count: 7
  critical:
    threshold: 5            # 5-of-7
    authority_count: 7
```

When the mesh grows, the manifest is updated via a Level 2 governance proposal — no code change needed.

### 33.4 Authority Conflict Resolution

If two Authorities issue conflicting decisions for the same scope:

| Scenario | Resolution |
|---|---|
| Authority A grants CAP_EXEC to node X; Authority B revokes CAP_EXEC from node X | **POLICY_CONFLICT** state. The conflicting capabilities are marked CONFLICTED. The runtime rejects ALL contracts using conflicted capabilities until resolution. |
| Two Authorities issue different certificates for the same NodeID | The certificate with the higher epoch wins. If epochs equal, the one with the earlier `issued_at` wins. |
| Authority A signs a governance proposal; Authority B signs a conflicting proposal | Both proposals are valid independently. The first to reach threshold is committed. The other is REJECTED with reason "superseded." |

**POLICY_CONFLICT behavior:**

```
FSM: CONTRACT_VALIDATING
  → VALIDATE_CAPABILITY
    → if capability.state == CONFLICTED
      → REJECT with reason "capability in conflict: CAP_PROC_EXEC (ALLOW vs DENY)"
      → audit log records both conflicting decisions
      → NO auto-resolution
      → operator must submit new governance proposal
```

**Fail-closed:** When in doubt, REJECT. No guessing, no majority vote, no random selection. The conflicting Authorities must resolve via a new governance proposal.

### 33.5 Compromised Authority Recovery

If an Authority node is compromised:

```
1. Mesh has Authorities A, B, C (threshold 2-of-3)
2. Authority A is compromised
3. Authorities B and C sign REVOKE_AUTHORITY proposal
4. Threshold (2) met → A's certificate is revoked
5. Epoch is incremented to invalidate all certs signed by A
6. Affected nodes re-enroll with remaining Authorities
```

The Root Key is never needed. The compromised Authority is removed by its peers.

**If M-of-N Authorities are compromised (e.g., 3 of 5 are compromised):**
- Mesh is considered compromised
- Recovery requires Root Key (M-of-N among original Recovery share holders)
- See §7.8 (Recovery Authorities)

### 33.6 Governance Protocol Message

```
Namespace: CONTROL
Message:   GOVERNANCE_PROPOSAL (0x02 0x70)

Payload:
{
  "governance_id": "gov-001",
  "level": 2,                                 // 0-4 (Local not wire-visible)
  "action": "POLICY_CHANGE" | "AUTHORITY_CREATE" |
            "AUTHORITY_REVOKE" | "EPOCH_INCREMENT" |
            "EMERGENCY_LOCKDOWN" | "MESH_DESTROY",
  "mesh_id": "SOC-Production",
  "payload": { ... },                          // action-specific data
  "threshold": 2,                              // required signatures (from manifest)
  "authority_count": 3,                        // total Authorities (from manifest)
  "signers": ["Authority-A", "Authority-B"],
  "signatures": ["sig_a...", "sig_b..."],
  "created_at": "...",
  "expires_at": "..."
}
```

### 33.7 Governance Flow

```
1. Authority (or node with CAP_GOVERNANCE_PROPOSE) creates GOVERNANCE_PROPOSAL
2. Proposal is gossiped to all Authorities in the mesh
3. Each Authority independently evaluates the proposal:
   a. Is the signer authorized for this Level?
   b. Is the proposal within the signer's scope?
   c. Is the proposal format valid?
4. Each Authority MAY sign (or reject)
5. When signature count >= threshold → proposal is ACCEPTED
6. ACCEPTED proposal is committed to all nodes via gossip
7. All nodes apply the governance decision locally
8. If proposal expires before threshold is met → REJECTED
```

### 33.8 Mesh Split (Partition)

SMO does NOT auto-merge split meshes. Governance history divergence requires human intervention.

```
1. Network partition splits the mesh
2. Both sides continue independently
3. Each side MAY increment its local Epoch
   → Side A: Epoch 13A
   → Side B: Epoch 13B
4. Partition heals — nodes detect governance fork
5. Runtime logs: "GOVERNANCE_FORK: Epoch 13A vs 13B"
6. No automatic merge
7. Operator decides which governance history to keep
8. Operator submits governance proposal to adopt the chosen history
9. The losing side's decisions are reversed where possible (audit-logged)
```

**Why no auto-merge?**
- Policy merge is undecidable in general: Side A granted CAP_EXEC, Side B revoked it. No algorithm can know which is correct.
- SMO controls real machines. A wrong merge could grant unauthorized execution.
- Human judgment is required for all governance conflicts.

### 33.9 Root Key Usage

| Event | Root Key Used? |
|---|---|
| Mesh creation (Genesis) | YES — used to sign first Authority certificate, then deleted |
| New Authority creation | NO — handled by existing Authorities (Level 2) |
| Authority revocation | NO — handled by peer Authorities (Level 2) |
| Compromised Authority | NO — M-of-N peer revocation |
| Emergency lockdown | NO — Level 3 by Authorities |
| Mesh destroy | NO — Level 3 by Authorities |
| Recovery after all Authorities lost | YES — M-of-N Recovery shares (§7.8) |

**The Root Key is used exactly once.** After Genesis, it is stored in an encrypted Recovery Package (AES-256-GCM, password-protected) and never touches a network.

### 33.10 Governance Invariants

1. **Governance does NOT govern execution.** Contract execution is governed by capability + policy + trust — always local, always node-sovereign.
2. **Thresholds are configurable per mesh.** Defined in Mesh Manifest, never hardcoded.
3. **Conflict = fail-closed.** When Authorities disagree, the runtime rejects affected operations until resolution.
4. **Compromised Authority = peer revocation.** M-of-N Authorities can revoke a compromised peer without Root Key.
5. **No auto-merge on split.** Governance fork requires human judgment.
6. **Governance history is append-only.** Corrections are new proposals referencing the ones they supersede.

### 33.11 MVP Governance

MVP uses simplified governance:
- Single Authority (1-of-1 for all levels).
- Root used at Genesis only.
- No policy change protocol.
- No mesh split/merge.
- Emergency recovery via Recovery Package (offline).

Full multi-Authority governance is post-MVP (Stage 7).

---

## XXVII. APPENDIX A — BUSINESS AND LICENSING

This section is non-normative.

### A.1 License

Apache License 2.0.

### A.2 Copyright

```
Copyright (c) 2026 Nguyen Duc Canh
Copyright (c) 2026 Distributed Offensive Technology Solutions Co., Ltd.
SPDX-License-Identifier: Apache-2.0
```

### A.3 Open Core Model

- **Community Edition**: Apache 2.0. Includes CLI, runtime, compiler, transport, SDK, documentation, basic examples.
- **Commercial**: Incident Response Packs, SOC Automation Packs, Enterprise playbooks (`.seme`), consulting, training, support.

### A.4 Project Identity

- **Founder / Lead Architect**: Nguyen Duc Canh
- **Organization**: Distributed Offensive Technology Solutions Co., Ltd.
- **Repository**: https://github.com/D-O-T-Solutions/smoframework

---

## XXVIII. APPENDIX B — REFERENCE LEGACY

This section is non-normative.

The directory `reference/OLD_SHELLMAP` contains the implementation of the predecessor ShellMap Framework (SMF). This code is:

- **NOT** to be reused as architecture
- **NOT** to be copied as code structure
- Available for reference of:
  - Post-quantum cryptographic wrapper implementations
  - Networking helper patterns
  - Serialization approaches
  - Implementation history and lessons learned

All new implementation MUST follow the SMO specification defined in this document. Legacy code does not override this specification.
