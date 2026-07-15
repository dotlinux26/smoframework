# SMO Architecture — Rationale and Design Decisions

This document explains **why** SMO is built the way it is.
It is not a specification. For precise definitions, see [SPEC.md](./SPEC.md).

---

## Why Contract?

Most distributed systems communicate via RPC or message passing.
SMO communicates via **contracts**.

A contract is a signed, metadata-only document that describes **what** should happen,
not **how**. The receiving node evaluates the contract against its own policy,
capabilities, and trust observations before deciding whether to execute.

This shifts the model from "tell a node what to do" to "propose to a node what could be done."
The node remains sovereign — it always has the final say.

Contracts also create a natural audit trail. Every execution is backed by a signed
document that all participants store independently. This is non-repudiation without
blockchain.

---

## Why Witness?

Broadcasting every operation to every node does not scale.
Asking a single trusted third party for an attestation is cheap and sufficient for most decisions.

The witness is not a consensus participant. It does not vote. It does not decide.
It simply says: "I know these two nodes, and I have no reason to believe anything is wrong."

This gives the Responder an independent signal without requiring a global broadcast.
If no witness is available, the Responder falls back to its local judgment
— the system degrades gracefully instead of halting.

---

## Why Capability (not Role)?

Roles (reader, writer, admin) are coarse and implicit.
A role-based system says: "you are an admin, so you can do everything."

A capability-based system says: "you have been granted CAP_FS_WRITE on /shared for this session."
The grant is explicit, scoped to a session, and must be presented at execution time.

This makes every permission visible, auditable, and bounded.
There is no implicit escalation. There is no "admin" key that unlocks everything.
Every opcode carries the capability set it needs, and the runtime verifies it.

R/W/X still exist — but only as convenience presets that map to capability sets.
The runtime itself does not know what "reader" means. It only knows capability bits.

---

## Why No Global State?

Global state is the single biggest source of complexity in distributed systems.
It requires consensus, replication, conflict resolution, and recovery protocols —
all before you have shipped a single feature.

SMO rejects global state by design.

Every node maintains its own session store, trust store, audit store, and DAG store.
The only thing shared between nodes is evidence: signed contracts, trust digests,
and attestations. There is no global ledger. There is no "cluster state."

This means SMO is eventually consistent by construction.
Two nodes may disagree about a trust score — and that is fine.
The Responder uses its local view to decide. If the view is wrong, the evidence
exists to correct it later.

---

## Why DAG (Directed Acyclic Graph)?

A linear execution queue is simple but wasteful.
Tasks that do not depend on each other should run in parallel.
A FIFO queue cannot express this.

A DAG captures dependencies explicitly. Task B depends on Task A; Task C depends on nothing.
The scheduler reads the graph and runs C immediately, A next, and B only after A finishes.

The DAG is produced once by the compiler and **never modified**.
This guarantees that all nodes involved in an execution see the same plan.
Replaying the execution against the original DAG always produces the same result.

---

## Why Trust Is Local (and Not Absolute)?

If trust were global and absolute, every node would need to agree on every other node's
reputation before making any decision. That is consensus — and consensus is slow, brittle,
and requires a global view that SMO explicitly does not have.

SMO treats trust as a local estimate. Every node computes scores based on its own
observations: heartbeat success, contract outcomes, witness feedback, and consistency checks.

These scores are gossiped as digests, but no node is required to accept another node's
score as truth. The Responder blends its local score with the witness's signal
and makes its own decision.

This design is more resilient. A compromised node can lie about its own trust score,
but it cannot force other nodes to believe that lie.

---

## Why Node Sovereignty?

In SMO, the Responder always has the final decision.
No contract is executed unless the executing node agrees.

This is not a bug. It is a feature.

In incident response, you want every node to be able to say "no" if the contract
violates local policy, even if the requester has the highest authority.
In fleet operations, you want each machine to validate an update against its own
OS version, patch level, and health status before applying it.

Sovereignty also limits blast radius. A compromised Authority node can send malicious
contracts, but every other node independently evaluates them. One bad actor cannot
force a fleet-wide compromise.

---

## Why Identity + Membership (not Just Keys)?

A keypair proves you can sign. It does not prove you belong to the mesh.

SMO separates **identity** (I am node X) from **membership** (node X belongs to Mesh Y with capabilities Z). Identity is a static Ed25519 keypair generated once. Membership is a signed certificate (.smoc) issued by a mesh Authority, carrying a role, capability set, epoch, and expiry.

This separation means:
- One node can belong to multiple meshes with different certificates.
- Compromising a mesh Authority does not compromise the node's base identity.
- Membership can be revoked by epoch increment without changing the node's keypair.
- Any node can verify any other node's membership by walking the certificate chain to the Root Public Key.

The Root Key never touches a network. It is generated during mesh creation, signs the first Authority certificate, exported as an encrypted Recovery Package, and deleted from runtime. Recovery requires M-of-N threshold shares via Shamir Secret Sharing.

---

## Why Four Protocol Layers (not One)?

Most systems use a single protocol for everything. SMO splits the wire into four independent protocol namespaces, each with its own transport characteristics:

- **Discovery Protocol (UDP):** HELLO, PING, heartbeat, gossip. Stateless, lossy, no ordering guarantees. Designed for fast failure detection and membership propagation.
- **Control Protocol (TCP):** Contracts, sessions, certificates, witness attestations, capability grants. Stateful, ordered, reliable. The "control plane" of the mesh.
- **Execution Protocol (TCP):** Execution start, progress, events, results, cancel. Separated from control so that long-running executions do not block certificate or contract operations.
- **Data Protocol (TCP):** Large data transfers via chunked streaming (CHANNEL_OPEN, CHUNK, ACK, NACK, FIN). Separated so that data transfer does not interfere with execution signaling.

This separation guarantees that a bulk data transfer does not delay a witness attestation, and a slow execution does not block certificate renewal. Each protocol gets the transport it needs — unreliable UDP for discovery, reliable TCP for everything else.

---

## Why Connectivity Layer?

Nodes behind NAT cannot accept inbound TCP connections. SMO bakes NAT traversal into the architecture instead of assuming direct connectivity.

The Connectivity Layer implements:
- **STUN (RFC 8489):** Discover the node's own mapped address and port.
- **ICE (RFC 8445):** Gather candidate pairs (host, server-reflexive, relayed), test connectivity, select the best working pair.
- **UDP hole punch:** Establish direct peer-to-peer UDP channels through NAT.
- **TURN relay (RFC 8656):** Fallback when direct paths fail.

This layer is optional for LAN deployments and mandatory for WAN/Internet meshes. It sits below the Session Layer, so higher protocols (Discovery, Control, Execution, Data) are unaware of NAT — they just see a connected session.

No third-party networking libraries. All STUN/ICE/TURN are implemented from RFCs. The Connectivity Layer is the only component that touches NAT; everything above it assumes a working session.

---
## Why Network Layer?

The Network Layer sits between Connectivity and Session layers. It provides **distributed membership services** that transform raw connectivity into a usable mesh.

```
CONNECTIVITY LAYER
  (STUN/ICE/TURN → produces connected sockets)
       │
       ▼
NETWORK LAYER
  Bootstrap          — Seed resolution, HELLO/WELCOME, peer table fetch
  Membership Sync    — Typed event bus: PeerAdded/Removed/Updated/Renamed
  Heartbeat (UDP)    — PING/PONG, RTT measurement, failure detection
  Gossip (UDP)       — SWIM epidemic membership propagation
  PeerStore (SQLite) — Persistent peer cache, survives restarts
       │
       ▼
SESSION LAYER
  (keys, certs, signed nonce)
```

**Why separate Network from Connectivity?**
- Connectivity only cares about "can I reach this IP:port". It knows nothing about peer identity, roles, or mesh membership.
- Network Layer owns the **Membership Table** — the authoritative view of the mesh. It maps NodeIDs to endpoints, tracks state (Online/Suspect/Offline), and exports a `PeerRecord` for the Selection Engine.
- All upper layers (Selection Engine, Runtime, Trust Engine) consume `MembershipTable` / `PeerRecord`. They never touch UDP sockets or bootstrap logic directly.

**Components:**
1. **Bootstrap** — `Bootstrap::find_seed()` connects to seed nodes, performs HELLO→WELCOME→DISCOVER→NODE_INFO handshake, returns initial peer table.
2. **MembershipSync** — Typed event bus (`PeerAdded`, `PeerRemoved`, `PeerUpdated`, `PeerRenamed`, `CapabilityChange`, `CertificateRotate`, `StateChange`). Publishes to GossipEngine, persists to PeerStore, notifies subscribers (Heartbeat, SelectionEngine).
3. **HeartbeatService** — Runs on UDP transport. Periodic PING to all `Online` peers. Measures RTT (moving average). Feeds `HealthMonitor` for suspicion/offline transitions. RTT feeds Selection Engine `NEAREST` mode.
4. **GossipEngine** — SWIM-inspired epidemic protocol. Periodic fanout to random peers. Payload = serialized `MembershipEvent` list. Increments `incarnation` on local state change. Resilient to partitions, eventual consistency.
5. **PeerStore** — SQLite-backed persistent cache (`peer.db`). Schema: `peers` table with full `PeerRecord` columns, `peer_events` for audit. Methods: CRUD, filtered queries (by role/tag/OS/arch/mesh/state), sync with `MembershipTable`, event log.

**Integration in `smo-node`:**
```cpp
MembershipTable membership;
HealthMonitor health;
DiscoveryEngine discovery(membership, health);
PeerStore peer_store;
MembershipSync sync(membership, health);
GossipEngine gossip(membership);
HeartbeatService heartbeat(udp_transport, membership, health);

peer_store.open(data_dir);
peer_store.sync_to_membership(membership);

// Register transports
TransportRegistry::instance().register_transport(std::make_unique<TcpTransport>(), "tcp");
UdpTransport udp_transport;
TransportRegistry::instance().register_transport(std::make_unique<UdpTransport>(), "udp");

// Bootstrap
if (seed_addr) {
    auto rec = Bootstrap::find_seed(seeds, TransportRegistry::instance(), local_id, now);
    if (rec) {
        discovery.handle_welcome(WelcomeMsg{local_id, *rec}, now);
        peer_store.sync_from_membership(membership);
    }
}

// Main loop
while (running) {
    now_ns = monotonic_now();
    heartbeat.tick(now_ns);
    gossip.tick(now_ns);
    discovery.tick(now_ns);
    health.tick(membership, now_ns);

    // Accept TCP + UDP
    auto tcp_session = tcp_listener->accept();
    auto [udp_session, from] = udp_listener->recv_from(65536);
    if (udp_session) parse_discovery_message(udp_session, from);
}
```

---

## Why Session Binding (Two-Factor)?

A certificate proves membership. A signed nonce proves key possession. SMO requires both.

Session establishment:
1. Node A connects to Node B.
2. Node B sends a random 32-byte nonce.
3. Node A signs the nonce with its Ed25519 private key.
4. Node B verifies: signature matches A's PublicKey, PublicKey matches A's certificate, certificate chain verifies up to Root, epoch is current, certificate is not expired.

Two independent security layers must be valid. Certificate alone is not enough (the node could be using a stolen cert). Signed nonce alone is not enough (it proves possession but not membership). Both together prevent replay, impersonation, and key-only attacks.

---

## Why These Twelve Layers?

```
APPLICATION
CLI / SDK
INTENT LANGUAGE
COMPILER + SCHEDULER + FSM
  ┌──────────────────────────────┐
  │  CONTROL PROTOCOL  (TCP)     │
  │  EXECUTION PROTOCOL (TCP)   │
  │  DISCOVERY PROTOCOL (UDP)   │
  │  DATA PROTOCOL     (TCP)    │
  ├──────────────────────────────┤
  │  SESSION + CERTIFICATE       │
  ├──────────────────────────────┤
  │  NETWORK LAYER               │
  │   ├── Bootstrap              │  Seed resolution, HELLO/WELCOME
  │   ├── Membership Sync        │  Event bus for membership changes
  │   ├── Heartbeat (UDP)        │  PING/PONG, RTT, failure detection
  │   ├── Gossip (UDP)           │  SWIM epidemic membership sync
  │   └── PeerStore (SQLite)     │  Persistent peer cache
  ├──────────────────────────────┤
  │  CONNECTIVITY (STUN/ICE)     │
  ├──────────────────────────────┤
  │  TRANSPORT (TCP/UDP)         │
  ├──────────────────────────────┤
  │  IDENTITY + MEMBERSHIP       │
  └──────────────────────────────┘
```

Each layer has exactly one responsibility. No layer bypasses the layer below it.

- **Application** — incident response, fleet ops, etc. Uses SMO, is not part of SMO.
- **CLI/SDK** — user entry point. Speaks in intents via contract JSON/YAML.
- **Intent Language** — contract format that describes *what*, not *how*.
- **Compiler → Scheduler → FSM** — turns intent into an immutable DAG, walks it, governs per-node lifecycle.
- **Four protocol layers** — Discovery (UDP, stateless), Control (TCP, stateful), Execution (TCP, streaming), Data (TCP, chunked). Separated so slow ops never block critical ops.
- **Session + Certificate** — two-factor binding: valid cert chain + signed nonce. Both required.
- **Connectivity** — STUN/ICE/TURN for NAT traversal. Optional on LAN, mandatory on WAN. No third-party networking libs.
- **Transport** — raw TCP/UDP sockets. Pluggable for future QUIC, Unix sockets.
- **Identity + Membership** — Ed25519 keypairs, certificate chain (Root → Authority → Node), Capability Epoch, M-of-N recovery.

The layering guarantees that you can replace any layer without affecting the others.
Swap TCP for QUIC? The FSM does not change.
Swap JSON for FlatBuffers? The scheduler does not change.
Swap the trust formula? The contract format does not change.
Add a new protocol? The transport layer does not change.

---

## Key Architectural Properties

| Property | How SMO Achieves It |
|---|---|---|
| Non-repudiation | Three-party contract records (Requester, Responder, Witness) |
| No single point of failure | No global state; witness fallback to local decision |
| Graceful degradation | Fewer witnesses → local decision. No quorum required |
| Deterministic execution | Immutable DAG + auditable FSM transitions |
| Composable permissions | Capability bitsets; opcodes declare requirements |
| Pluggable transport | Transport interface abstracted from runtime |
| Local autonomy | Responder always has final execution authority |
| Private key sovereignty | Key never leaves the generating node |
| Offline root | Root Key generated, used once, exported, deleted from runtime |
| Epoch-based revocation | Increment epoch to invalidate all old certificates at once |
| M-of-N recovery | Shamir Secret Sharing for Root Key reconstruction |
| NAT-independent protocols | Connectivity layer handles STUN/ICE/TURN; upper layers unaware |
| Two-factor session binding | Certificate (membership) + signed nonce (key possession) |

---

## What SMO Is Not

SMO is not a blockchain. There is no global ledger, no consensus protocol,
no miners, no gas, no on-chain state.

SMO is not a message queue. Contracts are not events. They are executable intents
with authorization, policy, and audit baked in.

SMO is not a remote shell. SSH executes what you tell it. SMO evaluates what you propose.

SMO is not Kubernetes. It does not schedule containers. It schedules execution intents
across a trust-scoped mesh.
