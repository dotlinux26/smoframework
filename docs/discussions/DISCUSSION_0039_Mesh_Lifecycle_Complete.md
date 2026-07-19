# Discussion 0039 — Mesh Lifecycle: Complete Implementation Plan

**Date:** 2026-07-19  
**Status:** 🟢 PHASE 1–4 ✅ | PHASE 5 🟡 (PQ handshake done, cert verify + manifest sig pending)  
**Core Principle:** **NO HTTP in mesh communication.** Everything via TCP Transport + CBOR opcodes.

---

## ⚡ KEY CLARIFICATIONS (Confirmed)

| Aspect | Design Decision |
|--------|-----------------|
| **Invite/Join mechanism** | ✅ **Static token (copy-paste)** — `SMO-JOIN-xxxxx` generated once, copied to joining node |
| **Transport** | ✅ **Pure TCP/CBOR** — **NO HTTP anywhere** in mesh communication |
| **Join flow** | Token (copy-paste) → TCP `JOIN_REQUEST` (0x0601) → `JOIN_RESPONSE` with cert + mesh_id + bootstrap_nodes (no full manifest) |
| **Bootstrap sync** | `BOOTSTRAP_SYNC_REQUEST` (0x0603) → `BOOTSTRAP_SYNC_RESPONSE` delta (epoch-based, changed components only) |
| **Auth** | Token is self-contained (CBOR payload + Ed25519/ML-DSA signature), no server-side session |

---

## 📚 REFERENCES

| Reference | Description |
|-----------|-------------|
| [RFC 0020 - Opcode Registry](../RFC/0020-opcode-registry.md) | Opcode namespace allocation, registration rules |
| [RFC 0031 - Mesh Manager](../RFC/0031-mesh-manager.md) | MeshManager API, mesh lifecycle, multi-mesh support |
| [RFC 0032 - Context-Aware CLI](../RFC/0032-context-cli.md) | Mesh context, current mesh, CLI context management |
| [RFC 0033 - Mesh Genesis & Governance](../RFC/0033-mesh-genesis-governance.md) | Mesh lifecycle states, bootstrap slots, governance tiers |
| [RFC 0034 - Bootstrap Protocol](../RFC/0034-bootstrap-protocol.md) | Bootstrap protocol (namespace 0x05), CBOR schemas, message flow |
| [RFC 0035 - Runtime Architecture](../RFC/0035-runtime-architecture.md) | Runtime kernel, execution pipeline, contract interface |
| [DISCUSSION_0034_UX_MESH_CONTEXT](DISCUSSION_0034_UX_MESH_CONTEXT.md) | Mesh context, CLI UX, mesh catalog |
| [DISCUSSION_0035_PKI_GOVERNANCE](DISCUSSION_0035_PKI_GOVERNANCE.md) | PKI hierarchy, governance tiers, certificate lifecycle |
| [DISCUSSION_0036_JOIN_TOKEN_BOOTSTRAP](DISCUSSION_0036_JOIN_TOKEN_BOOTSTRAP.md) | Join token format, bootstrap protocol, invite flow |
| [DISCUSSION_0037_WIRING_BRIDGE](DISCUSSION_0037_WIRING_BRIDGE.md) | Runtime wiring, kernel, dispatcher, middleware |
| [SPEC.md](../SPEC.md) | Main specification document |
| [ARCHITECTURE_SUMMARY.md](../ARCHITECTURE_SUMMARY.md) | Architecture summary |

---

## 1. Current State: What Works (✅ Tested)

| Feature | Command | Status |
|---------|---------|--------|
| Node init | `smo-node --init --name <name> --data <dir>` | ✅ Creates identity + CSR |
| Certificate signing | `smo-admin sign` + `smo-node --import` | ✅ Authority signs CSR |
| Mesh creation | `smo-admin create-mesh <name> <dir>` | ✅ Creates mesh + authority + mesh.json |
| Bootstrap config | `smo-admin mesh publish --listen ... --endpoint ...` | ✅ Configures bootstrap endpoints |
| Generate invite | `smo-admin generate-invite --role <role> --expire <dur> --endpoint <ep>` | ✅ Creates JoinToken v2 (CBOR + sig) |
| Join via token | `smo-node --join --token SMO-JOIN-... --port <p> --data <dir>` | ✅ TCP/CBOR JOIN_REQUEST (0x0601) |
| Bootstrap | `smo-node --daemon --seed <ip:port>` | ✅ HelloMsg → BootstrapResponse → snapshot + BootstrapSync (0x0603) |

---

## 2. Completed & Remaining

| Priority | Feature | Status |
|----------|---------|--------|
| **1** | `smo mesh create <name>` | ✅ done (MeshManager::create_mesh + smo-cli handler) |
| **2** | `smo mesh invite <role>` | ✅ done (generate-invite via smo-admin, token format v2) |
| **3** | `smo mesh join --token` | ✅ done (TCP/CBOR JOIN_REQUEST 0x0601, state machine, CERT_VERIFY, persist) |
| **4** | `/mesh/bootstrap` endpoint | ✅ done (BootstrapContract + opcodes 0x0603/0x0604 + daemon raw handler + client) |
| **5** | `MeshManager::join_mesh` (secure) | 🟡 PQ handshake ✅ in client + server; cert verify + manifest sig pending |
| **6** | Mesh catalog in `smo` CLI | ❌ not started |
| **7** | Mesh catalog sync via gossip | ❌ not started |

---

## ⚡ NO HTTP — Pure TCP/CBOR Protocol (CONFIRMED)

### Current (has HTTP - REMOVE):
```
smo-node --join --token ... → HTTP POST /enroll → returns cert
```

### Target (pure TCP/CBOR):
```
Node                          Bootstrap Endpoint
  │                                    │
  │── JOIN_REQUEST (opcode 0x0601) ────►│
  │    { token_wire, csr_cbor,         │
  │      timestamp, nonce, csr_hash,   │
  │      request_signature }           │
  │                                    │── verify timestamp ±30s
  │                                    │── verify token_id not used
  │                                    │── verify nonce fresh
  │                                    │── verify request signature
  │                                    │── verify CSR
  │                                    │── issue certificate
  │◄── JOIN_RESPONSE ──────────────────│
  │    { certificate, mesh_id,         │
  │      manifest_digest,              │
  │      manifest_epoch,               │
  │      bootstrap_nodes }             │
  │                                    │
  │── BOOTSTRAP_SYNC_REQUEST ─────────►│
  │    { manifest_epoch, crl_epoch,    │
  │      membership_epoch,             │
  │      policy_version }              │
  │◄── BOOTSTRAP_SYNC_RESPONSE ───────│
  │    { manifest_delta?,              │
  │      membership_delta?,            │
  │      policy_delta?,                │
  │      crl_delta?, ... }             │
```

### New Opcodes (namespace 0x06 - Join):

| Opcode | Name | Direction |
|--------|------|-----------|
| `0x0601` | JOIN_REQUEST | Node → Bootstrap endpoint |
| `0x0602` | JOIN_RESPONSE | Bootstrap → Node |
| `0x0603` | BOOTSTRAP_SYNC_REQUEST | Node → Bootstrap |
| `0x0604` | BOOTSTRAP_SYNC_RESPONSE | Bootstrap → Node |

---

## 5. Critical Protocol Fixes (Must Fix Before P6 Implementation)

### 5.1 JOIN_RESPONSE: Lightweight (No Full Manifest)

**Critical design decision:** JOIN_RESPONSE must NOT return full manifest. For a mesh with large policy/contract sets, manifest can be several MB. This would make JOIN_RESPONSE a bottleneck.

**Principle:** JOIN gives you a certificate. Bootstrap gives you state.

**JOIN_RESPONSE (lightweight):**
```cbor
{
  1: certificate,
  2: mesh_id,
  3: manifest_digest,      // sha256 — verify later
  4: manifest_epoch,       // monotonic uint64
  5: bootstrap_nodes       // 5–10 seed endpoints
}
```

**Post-join flow:**
```
JOIN
  ↓
CERTIFICATE received
  ↓
BOOTSTRAP_SYNC (0x0603)
  ↓
manifest_delta / full manifest
  ↓
membership_delta
  ↓
READY
```

> **Rationale:** Join grants identity. Bootstrap grants world state. Never mix them.

---

### 5.2 Bootstrap Sync: Epoch-Based Delta Sync

**Current Request (BROKEN - full sync):**
```cbor
{
  mesh_id,
  node_id,
  known_epoch
}
```

**Fixed Request (EPOCH-BASED):**
```cbor
{
  1: mesh_id,
  2: node_id,
  3: manifest_epoch,
  4: crl_epoch,
  5: membership_epoch,
  6: policy_version
}
```

**Bootstrap Response Logic:**
```
manifest_epoch == local_manifest_epoch  → skip manifest
crl_epoch == local_crl_epoch           → skip CRL
membership_epoch == local_epoch        → skip membership
policy_version == local_version        → skip policies
Only send changed components (delta sync)
```

---

### 5.3 JOIN_RESPONSE: Only Seeds, Not Full Peers

**Current (BROKEN - returns ALL peers):**
```cbor
{
  certificate,
  manifest,
  peers[10000],    // NOT OK for large mesh
  seeds
}
```

**Fixed - Seeds Only:**
```cbor
{
  1: certificate,
  2: manifest,
  3: seed_nodes,        // 5-10 nodes (bootstrap entry points)
  4: bootstrap_nodes,   // Additional bootstrap entry points
  5: policies,
  6: crl_digest,
  7: manifest_epoch
}
```

> **Rationale:** Full peer list is for GossipEngine to discover. JOIN_RESPONSE only needs enough to start gossip.

---

### 5.4 BOOTSTRAP_SYNC_RESPONSE: Delta Format

**Current (FULL SNAPSHOT + duplicate CBOR keys):**
```cbor
{
  manifest,
  peers,
  seeds,
  policies
}
```

**Fixed - DELTA FORMAT (unique CBOR keys):**
```cbor
{
  1: manifest_delta?,       // present only if manifest_epoch > known
  2: membership_delta?,     // present if membership_epoch > known
  3: policy_delta?,         // present if policy_version > known
  4: crl_delta?,            // present if crl_epoch > known
  5: manifest_epoch,        // monotonic uint64
  6: membership_epoch,      // monotonic uint64
  7: crl_epoch,             // monotonic uint64
  8: policy_version         // monotonic uint64
}
```

> **Rationale:** Mesh with 10,000 nodes → full sync = MB. Delta = KB.

---

### 5.5 MeshManager::join_mesh - Verification Steps

**Current (INSECURE):**
```
connect → sync → update → gossip
```

**Fixed - SECURE FLOW:**
```
load local identity
    ↓
connect seed (TCP)
    ↓
TLS / PQ handshake
    ↓
verify bootstrap cert
    ↓
verify mesh authority
    ↓
BOOTSTRAP_SYNC
    ↓
validate manifest signature
    ↓
apply manifest
    ↓
update peer db
    ↓
start heartbeat
    ↓
start gossip
```

> **Critical:** Verify bootstrap authority cert chain to Root before accepting manifest.

---

### 5.6 Gossip: What to Sync vs NOT

**SYNC via Gossip (GossipEngine):**
```
membership changes (join/leave/suspend)
policy updates (add/remove/modify)
CRL updates (revocations)
contract deployments/updates
routing table updates
CRT scores
```

**DO NOT SYNC via Gossip (Local State):**
```
mesh aliases (display names)
CLI context (current mesh)
current_mesh in ~/.smo/context.json
local node identity.json
per-node config.json
```

> **Rationale:** Local state is per-node, not mesh-wide. Gossip = mesh-wide state only.

---

### 5.7 Service Decomposition (Avoid God Object)

**Current (GOD OBJECT - MeshManager):**
```cpp
class MeshManager {
    create_mesh()
    join_mesh()          // join logic
    join_mesh()          // bootstrap sync
    list_meshes()
    switch_mesh()
    // ... 50+ methods
}
```

**Fixed - DECOMPOSED:**
```
MeshManager
├── JoinService
│   ├── JOIN_REQUEST / JOIN_RESPONSE
│   ├── CSR validation & orchestration
│   └── AuthorityService::issue_certificate() ← delegation
├── BootstrapService
│   ├── BOOTSTRAP_SYNC_REQUEST/RESPONSE
│   ├── Manifest + peer + seed + policy delta sync
│   └── Manifest signature verification
└── SyncService
    ├── Membership delta sync (gossip)
    ├── Policy delta sync
    ├── CRL delta sync
    ├── Manifest delta sync
    └── GossipEngine integration
```

> **Benefit:** Each service testable independently, single responsibility, no god object.

---

### 5.8 Join State Machine (Resume Capability)

```
NEW
  ↓
TOKEN_RECEIVED
  ↓
CSR_CREATED
  ↓
JOIN_SENT
  ↓
WAIT_RESPONSE
  ↓
timeout → retry (max 3, exponential backoff) → FAILED
  ↓
CERT_RECEIVED
  ↓
BOOTSTRAP_SYNC
    ↓
    WAIT_SYNC
    ↓
    timeout → retry → FAILED
  ↓
GOSSIP_SYNC
  ↓
  WAIT_GOSSIP
  ↓
  timeout → retry → FAILED
  ↓
READY
```

**On Failure:**
```
ANY_STATE
  ↓
RETRY (max 3, exponential backoff)
  ↓
FAILED (persist state to disk)
```

**On Restart:**
```
Read persisted state
Resume from last completed step
```

> **Rationale:** Node crash/restart during join → resume without user intervention.

---

### 5.9 manifest_version Policy

Add `manifest_version` alongside `manifest_epoch`:

```
epoch:  1, 2, 3, 4      // only increments, no semantic meaning
version: v2.1, v2.2, v3  // semantic: major.minor.patch

compatibility check:
  node v2 joins mesh v3 → reject
  node v3 joins mesh v3 → ok
  node v3 joins mesh v2 → reject (downgrade)
```

---

### 5.10 BootstrapResponse Capabilities (Fixed Duplicate Keys)

Add capability negotiation with unique CBOR keys:

```cbor
{
  1: certificate,
  2: manifest,
  3: seed_nodes,
  4: bootstrap_nodes,
  5: policies,
  6: crl_digest,
  7: manifest_epoch,         // monotonic uint64
  8: manifest_version,       // semantic "major.minor.patch"
  9: capabilities: [         // capability negotiation
      "gossip_v2",
      "contract_sync",
      "crt",
      "delta_sync",
      "compression:lz4",
      "compression:zstd",
      "compression:brotli",
      "stream_sync"
    ]
}
```

> **Compression negotiation:** Capabilities specify algorithm (`compression:lz4`). Response payload is optionally compressed. Node selects best mutually supported algorithm.

---

### 5.11 Seed Node Priority

```cbor
seed_nodes: [
  {
    "endpoint": "mesh.company.com:5454",
    "priority": 1,
    "weight": 100
  },
  {
    "endpoint": "vpn.company.com:5454",
    "priority": 2,
    "weight": 50
  }
]
```

---

### 5.12 SyncService Scheduler

```
SyncService
└── SyncScheduler
    ├── membership_delta  (every 30s)
    ├── policy_delta      (every 60s)
    ├── crl_delta         (every 300s)
    ├── manifest_delta    (on change)
    └── contracts_delta   (on change)
```

---

### 5.13 Acceptance Criteria (Final State)

```
READY
  ↓
Heartbeat ACTIVE
  ↓
Discovery ACTIVE
  ↓
Gossip ACTIVE
  ↓
Sync ACTIVE
```

---

### 5.14 JoinService → AuthorityService Delegation

```
JoinService
  ↓
CSR validation
  ↓
AuthorityService::issue_certificate()
  ↓
JoinService builds response
```

> **Authority is CA owner.** JoinService only orchestrates. HSM/remote signer support requires this separation.

---

---

### 5.15 Epoch vs Version — Explicit Semantics

| Field | Type | Semantics | Use |
|-------|------|-----------|-----|
| `manifest_epoch` | monotonic uint64 | 1, 2, 3, 4 ... | Sync ordering (what's newer) |
| `manifest_version` | semver `major.minor.patch` | 2.1.0, 2.2.0, 3.0.0 | Compatibility check (what's compatible) |

**Rules:**
- NEVER use version for sync ordering — epoch handles that
- NEVER use epoch for compatibility — version handles that
- Downgrade detection: `join_version < mesh_version → reject`
- Upgrade detection: `join_version >= mesh_version.next → warn` (future compat)

---

### 5.16 Join Token — Add Nonce for Replay Protection

**Current token:**
```cbor
{
  expiry,
  mesh_id,
  role,
  signature
}
```

**Fixed token (with token_id):**
```cbor
{
  1: token_id,             // 128-bit random (crypto-grade RNG)
  2: mesh_id,              // 16 bytes
  3: role,                 // "member" | "admin" | "observer"
  4: expiry,               // UNIX timestamp uint64
  5: signature             // Ed25519 or ML-DSA
}
```

**Bootstrap cache:**
- Maintain `used_token_ids: Vec<[u8; 16]>` (only in memory, not persisted)
- On JOIN_REQUEST: reject if `token_id` in `used_token_ids`
- After expiry: flush all expired token_ids from cache
- This prevents replay even if token is intercepted

---

### 5.17 JOIN_REQUEST — Replay Protection (Timestamp + Nonce)

Each JOIN_REQUEST includes per-request freshness:

```cbor
{
  1: token_wire,            // the JoinToken CBOR blob
  2: csr_cbor,              // CSR (Ed25519 or ML-DSA public key)
  3: timestamp,             // UNIX seconds uint64 (±30s window)
  4: nonce,                 // 64-bit random per-request
  5: csr_hash,              // sha256(csr_cbor) — bind request to CSR
  6: request_signature      // sign(token_wire || timestamp || nonce || csr_hash)
}
```

**Bootstrap verify:**
1. `timestamp` within ±30s of local clock
2. `token_id` not in `used_token_ids` (from token_wire)
3. `nonce` not seen before (per-peer dedup window, flush after 60s)
4. `request_signature` valid against token issuer public key
5. Delete token from `used_token_ids` after first use (or keep until expired)

> **Without this:** Anyone intercepting a JOIN_REQUEST can replay it to re-issue a certificate.

---

### 5.18 BootstrapService — Stateless + Separate Stores

**BootstrapService is stateless:**
```
Node sends BOOTSTRAP_SYNC_REQUEST
  ↓
BootstrapService queries stores
  ↓
Serializes delta response
  ↓
Sends BOOTSTRAP_SYNC_RESPONSE
  ↓
State returned to caller (no internal cache)
```

All state lives in dedicated stores:

```
MeshManager
  │
  ├── JoinService
  │     └── AuthorityService
  │
  ├── BootstrapService    (stateless — queries stores)
  │
  ├── SyncService
  │     └── GossipEngine
  │
  ├── DiscoveryEngine     (UDP only — see 5.20)
  │
  └── Stores
        ├── PeerStore       // peer list, heartbeat timestamps
        ├── SeedStore       // bootstrap seed nodes with priority/weight
        ├── AuthorityStore  // trusted authorities, CA chain
        ├── ManifestStore   // immutable manifest snapshots
        └── PolicyStore     // active policies
```

> **Rationale:** Stateless BootstrapService can be horizontally scaled. No sync needed between bootstrap instances.

---

### 5.19 Gossip — Add routing_delta

```
SyncScheduler:
  membership_delta   (every 30s)
  policy_delta       (every 60s)
  crl_delta          (every 300s)
  manifest_delta     (on change)
  contracts_delta    (on change)
  routing_delta      (every 15s)   ← NEW
```

**routing_delta:** Routing table entries change frequently (peers connect/disconnect). 15s interval keeps routing fresh without flooding. Delta format: `{ added: [...], removed: [...], changed: [...] }`.

---

### 5.20 Discovery vs Bootstrap — Protocol Separation

**Discovery (UDP — ephemeral, best-effort):**
```
Hello    → UDP broadcast "I exist"
Welcome  → UDP response "I see you"
Ping     → UDP liveness check
Pong     → UDP liveness response
```

**Bootstrap (TCP — reliable, stateful):**
```
JOIN_REQUEST          → TCP
JOIN_RESPONSE         → TCP
BOOTSTRAP_SYNC_REQUEST  → TCP
BOOTSTRAP_SYNC_RESPONSE → TCP
```

**Rule:** Discovery NEVER participates in JOIN/BOOTSTRAP. Discovery is only for:
- Initial peer discovery (find nodes on same LAN)
- Liveness monitoring
- NAT traversal hints

---

### 5.21 State Machine — Add CERT_VERIFY

Current:
```
CERT_RECEIVED
  ↓
BOOTSTRAP_SYNC
```

Fixed:
```
CERT_RECEIVED
  ↓
CERT_VERIFY          ← NEW state
  │ verify cert chain → bootstrap authority → mesh root
  │ verify signature
  │ verify not expired
  │ verify not revoked (check CRL)
  ↓
BOOTSTRAP_SYNC
```

**Full state machine:**
```
NEW
  ↓
TOKEN_RECEIVED
  ↓
CSR_CREATED
  ↓
JOIN_SENT
  ↓
WAIT_RESPONSE
  ↓ timer / retry → FAILED
  ↓
CERT_RECEIVED
  ↓
CERT_VERIFY           ← NEW
  ↓ fail → FAILED
  ↓
BOOTSTRAP_SYNC
  ↓
  WAIT_SYNC
  ↓ timer / retry → FAILED
  ↓
GOSSIP_SYNC
  ↓
  WAIT_GOSSIP
  ↓ timer / retry → FAILED
  ↓
READY
```

> **Rationale:** Receiving a cert doesn't mean the cert is valid. Chain verification, expiry, and CRL checks must happen before proceeding to bootstrap.

---

### 5.22 Manifest — Immutable (Git-like)

Manifests are **never updated in-place**. Each change creates a new manifest version:

```
Manifest V1  ── hash: a1b2c3
Manifest V2  ── hash: d4e5f6
Manifest V3  ── hash: g7h8i9
```

**Storage:**
```
~/.smo/meshes/<name>/manifests/
  ├── v1_manifest.cbor        (immutable)
  ├── v2_manifest.cbor        (immutable)
  └── LATEST                  → symlink to v2_manifest.cbor
```

**Delta sync:** Bootstrap compares `manifest_epoch`. If joining node's epoch < current, send only changed components (delta). If delta too large (cross-version gap > N), send full manifest.

> **Rationale:** Immutable manifests make auditing, rollback, and verification trivial. Hash (digest) serves as content identifier.

---

### 5.23 Service Graph — Dependencies

```
MeshManager
    │
    ├── JoinService
    │      │
    │      ├── AuthorityService   (CA: issue_certificate)
    │      │
    │      └── JoinStateMachine   (persisted FSM)
    │
    ├── BootstrapService          (stateless)
    │      │
    │      ├── ManifestStore
    │      ├── PeerStore
    │      ├── SeedStore
    │      └── PolicyStore
    │
    ├── SyncService
    │      │
    │      └── SyncScheduler
    │             │
    │             ├── membership_delta
    │             ├── policy_delta
    │             ├── crl_delta
    │             ├── manifest_delta
    │             ├── routing_delta
    │             └── contracts_delta
    │
    ├── GossipEngine              (TCP — mesh-wide state)
    │
    ├── DiscoveryEngine           (UDP — liveness + LAN discovery)
    │
    └── Stores
          ├── PeerStore
          ├── SeedStore
          ├── AuthorityStore
          ├── ManifestStore
          └── PolicyStore
```

**Dependency direction:** Top → Bottom. `JoinService` depends on `AuthorityService`. `SyncService` depends on `GossipEngine`. `BootstrapService` depends on Stores. `MeshManager` orchestrates all.

---

## 6. Updated Implementation Phases

### Phase 1: `smo mesh create` + `smo mesh invite` ✅
- `cmd/smo-cli/main.cpp` → handlers for `mesh create`, `mesh invite`
- `MeshManager::create_mesh()` ✅ exists, `generate_invite()` ✅ exists

### Phase 2: `smo mesh join --token` (TCP/CBOR) ✅
- `cmd/smo-cli/main.cpp` → handler for `mesh join --token`
- `core/enroll/auto_enroll.cpp` — replaced HTTP POST `/enroll` with TCP/CBOR ✅
- JOIN_REQUEST: add timestamp, nonce, csr_hash, request_signature (replay protection) ✅
- JOIN_RESPONSE: lightweight — cert, mesh_id, manifest_digest, manifest_epoch, bootstrap_nodes (NO full manifest) ✅
- **State machine:** add CERT_VERIFY state between CERT_RECEIVED and BOOTSTRAP_SYNC ✅
- Persist state after each step (resume on crash) ✅

### Phase 3: `/mesh/bootstrap` endpoint ✅
- `BootstrapContract::handle_bootstrap_sync()` — BootstrapService is stateless ✅
- Opcodes: `0x0603` (request), `0x0604` (response) — registered in PacketDispatcher + RuntimeBridge ✅
- Delta sync: `manifest_delta?`, `membership_delta?`, `policy_delta?`, `crl_delta?` ✅
- Epoch-based request: `{ manifest_epoch, crl_epoch, membership_epoch, policy_version }` ✅
- Client-side BootstrapSync TCP/CBOR in `auto_enroll.cpp` ✅
- Daemon raw handler wired for JoinRequest + BootstrapSyncRequest ✅

### Phase 4: Service Decomposition ✅ (MeshManager refactored)
- Stores built: `SeedStore` (core/discovery/) ✅, `AuthorityStore` (core/authority/) ✅, `ManifestStore` (core/storage/) ✅, `PolicyStore` exists in `storage/policy_store/`
- Services built: `JoinService` (core/join/) ✅, `BootstrapService` (core/bootstrap/) ✅, `SyncService` (core/network/sync/) ✅
- `MeshManager` refactor ✅ — service injection via `set_join_service`/`set_bootstrap_service`/`set_sync_service`, `join_mesh()`/`leave_mesh()`/`delete_mesh()` delegate to services
- `DiscoveryEngine` (UDP) ❌ **NOT DONE** (separate concern, see §5.20)
- Build: ✅ all targets (`smo_core`, `smo-cli`, `smo-admin`, `smo-node`)
- Phase 5 SecureSession integration started: `auto_enroll.cpp` + `smo-node` accept loop ✅

### Phase 5: `MeshManager::join_mesh` (Secure)
- ✅ `SecureSession` class: PQ 1-RTT KEM handshake + XChaCha20-Poly1306 AEAD + HKDF key derivation
- ✅ `KemImpl::generate_keypair` added to all 3 suites (Classical/Modern/PurePQC)
- ✅ Client integration: `auto_enroll.cpp` — `tcp_cbor_exchange` replaced with `SecureSession` handshake + encrypted send/recv for both JoinRequest and BootstrapSync
- ✅ Server integration: `smo-node` accept loop — PQ handshake on accepted TCP fds with server cert + identity signing key; `SecureTransportSession` adapter for transparent dispatch; fallback to plaintext when no cert
- ✅ PacketDispatcher refactor: `dispatch_session(TcpSession&)` → `dispatch_session(TransportSession&)` — encryption-agnostic
- ⬜ Verify bootstrap cert → authority chain → manifest signature (post-handshake cert verification)
- ⬜ Delta sync with epoch checks
- ⬜ State machine persistence (including CERT_VERIFY)
- ✅ Manifest immutable (Git-like versioned snapshots) — already documented in §5.22

### Phase 6: `smo mesh list/use` in `smo` CLI

### Phase 7: Mesh catalog sync via gossip (GossipEngine + MembershipSync)

---

## Acceptance Criteria (Final)

```bash
# 1. Create mesh (smo, not smo-admin)
smo mesh create mymesh
# → ~/.smo/meshes/mymesh/ created with mesh.json + keys

# 2. Configure bootstrap
smo mesh publish --listen 0.0.0.0:5454 --endpoint mesh.mydomain.com:5454

# 3. Generate invite
smo mesh invite --role member --expire 1h --endpoint mesh.mydomain.com:5454
# → SMO-JOIN-xxxxx

# 4. Join (pure TCP/CBOR, no HTTP)
smo mesh join --token SMO-JOIN-xxxxx
# → TCP JOIN_REQUEST (0x0601, with timestamp+nonce+signature)
# → JOIN_RESPONSE (cert + mesh_id + manifest_digest + bootstrap_nodes)
# → BOOTSTRAP_SYNC_REQUEST (epochs) → BOOTSTRAP_SYNC_RESPONSE (deltas)

# 5. Start node
smo-node --daemon --data /tmp/node1 --port 9121 --seed 127.0.0.1:5454
# → BOOTSTRAP_SYNC_REQUEST (epochs) → BOOTSTRAP_SYNC_RESPONSE (deltas)
# → Bootstrap complete. Peers: 1
# State machine: NEW → TOKEN_RECEIVED → CSR_CREATED → JOIN_SENT → WAIT_RESPONSE → CERT_RECEIVED → CERT_VERIFY → BOOTSTRAP_SYNC → WAIT_SYNC → GOSSIP_SYNC → WAIT_GOSSIP → READY

# 6. Mesh catalog
smo mesh list
smo mesh use mymesh

# 7. Verify state machine on restart
# Kill node during GOSSIP_SYNC → restart → resume from GOSSIP_SYNC
```

---

## Next Step

**All 15 architectural improvements applied:**
1. ✅ 5.1 JOIN_RESPONSE lightweight (no manifest)
2. ✅ 5.4 BOOTSTRAP_SYNC_RESPONSE duplicate CBOR keys fixed
3. ✅ 5.10 BootstrapResponse duplicate CBOR keys fixed (+ compression)
4. ✅ 5.15 Epoch vs Version semantics clarified
5. ✅ 5.16 Join Token nonce (token_id) for replay protection
6. ✅ 5.17 JOIN_REQUEST timestamp+nonce+signature replay protection
7. ✅ 5.18 BootstrapService stateless + store separation
8. ✅ 5.19 Gossip routing_delta added (in SyncSchedule)
9. ✅ 5.20 Discovery (UDP) vs Bootstrap (TCP) separation
10. ✅ 5.21 State machine: CERT_VERIFY added
11. ✅ 5.22 Manifest immutable (Git-like)
12. ✅ 5.23 Service graph with dependency order
13. ✅ Updated target protocol diagram
14. ✅ Updated Acceptance Criteria
15. ✅ Updated Phase 2–5 implementation steps

**Phase 5**: `MeshManager::join_mesh` (secure PQ + cert verify + manifest validation) 🟡
- Phase 6: `smo mesh list/use` in `smo` CLI
- Phase 7: Mesh catalog sync via gossip

**Next Step**: Phase 5 — cert chain verification (join_response cert → authority cert → root) + manifest signature validation