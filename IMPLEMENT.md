# Architecture Review — SMO 3.0

Based on SPEC.md (2991 lines, 33 sections, 23 invariants, 11 golden rules).

---

## 1. IDENTITY + MEMBERSHIP LAYER (Layer 15)

### Modules involved

| Module | Responsibility |
|---|---|
| `core/identity/` | NodeIdentity, Ed25519 keypair, nonce signing |
| `core/mesh/` | MeshID, MeshGenesis, Epoch, MembershipCertificate |
| `core/enroll/` | EnrollRequest, EnrollResponse, ExportFormat |
| `storage/node_store/` | Keypair persistence |
| `storage/mesh_store/` | Certificate + manifest persistence |
| `cmd/smo-node/` | `init`, `import`, daemon mode |
| `cmd/smo-admin/` | `mesh create`, `sign`, `authority issue` |
| `transport/enrollment/` | Format export/import, QR |
| `transport/certificate/` | ImportCertificate auto-detect |

### Invariants

| Invariant | Description |
|---|---|
| I-14 | **Private Key Confinement** — key never leaves node |
| I-15 | **Certificate Chain Verification** — verifiable up to Root |
| I-17 | **Carrier Independence** — enrollment format ≠ transport |
| I-23 | **Discovery Engine Separate from Transport** |

### Interfaces already defined in SPEC

| Interface | SPEC Location |
|---|---|
| `NodeID = Blake3(NodePublicKey)` | §0, §VII.1 |
| `ExportEnrollmentRequest(req, format, out)` | §9.7 |
| `ImportCertificate(data, cert)` | §9.7 |
| Mesh creation flow | §7.2 |
| Certificate JSON schema | §7.4 |
| `.smor` request format | §9.2 |
| `.smoc` certificate format | §9.3 |
| Join Token format | §9.6 |
| Node Identity Lifecycle FSM | §7.10 |
| Mesh Manifest format | §7.11 |
| Manifest verification | §7.11 |
| Multi-tenant directory layout | §7.11 |

### Interfaces missing (MUST define before coding)

| Missing Interface | Rationale |
|---|---|
| `Keypair::generate(suite_id) → Keypair` | Suite ID 1 = Ed25519, but interface should accept Suite ID for future PQ |
| `CertificateChain::verify(up_to_root) → Result` | Walk chain: node → Authority → Root. Not specified how chain is stored/linked |
| `MeshManifest::load(path) → Manifest`, `sign(bytes) → Signature`, `verify(manifest) → bool` | File I/O + signature operations for manifest |
| `EnrollRequest::sign(keypair) → Signature`, `verify(enroll_request) → bool` | Request signing by node + verification by Authority |
| `NodeIdentity::rotate() → (new_keypair, csr)` | Key rotation flow needs CSR generation |
| `NodeState::transition(event) → Result` | Lifecycle FSM needs a generic transition function |
| `RecoveryPackage::encrypt(root_key, password) → Package`, `decrypt(package, password) → RootKey` | AES-256-GCM wrapper for root key export |

### Tests required before implementation

1. `Keypair::generate` produces valid Ed25519 keypair (24 bytes secret + 32 bytes public)
2. `Blake3(public_key)` produces 32-byte NodeID deterministically
3. Certificate chain verification: valid chain → OK, broken chain → REJECT
4. Certificate expiry: expired cert → REJECT, valid cert → OK
5. Epoch < current → REJECT, Epoch >= current → OK
6. EnrollRequest signed by node → Authority verifies
7. EnrollResponse (certificate) signed by Authority → node verifies
8. Mesh manifest import: valid signatures → OK, missing threshold → REJECT
9. Key rotation: old key signs CSR for new key → Authority issues new cert
10. Suspension: suspended cert → no new sessions, unsuspend → sessions allowed
11. Multi-tenant isolation: Mesh A cert does not grant access to Mesh B
12. `ExportEnrollmentRequest` roundtrip: export(JSON) → import → export(TEXT) → all produce identical semantic content
13. Join Token: single-use → second use rejected, time-limited → expired token rejected

---

## 2. TRANSPORT LAYER (Layer 12)

### Modules involved

| Module | Responsibility | MVP Status |
|---|---|---|
| `transport/tcp/` | TCP transport | MVP |
| `transport/udp/` | UDP transport (discovery, heartbeat) | MVP |
| `transport/framing/` | FrameHeader, zero-copy read/write | MVP |
| `transport/serialization/` | CBOR, JSON, ASCII Armor, binary auto-detect | MVP |
| `transport/stun/` | STUN client (RFC 8489) | Post-MVP |
| `transport/ice/` | ICE candidate gathering (RFC 8445) | Post-MVP |
| `transport/nat/` | UDP hole punch | Post-MVP |
| `transport/relay/` | TURN relay (RFC 8656) | Post-MVP |

### Invariants

| Invariant | Description |
|---|---|
| I-10 | **Transport Independence** — runtime never depends on specific transport |
| I-16 | **Protocol Layer Isolation** — transport unaware of protocol |
| Rule 6 | **Private Key Never Travels** — transport never sees private keys |

### Interfaces already defined in SPEC

| Interface | SPEC Location |
|---|---|
| Packet binary format | §6.6 |
| Wire protocol rules | §6.7 |
| Version handshake | §6.10 |
| Version compatibility (N-2) | §6.10.3 |
| Transport assignment table | §6.2 |

### Interfaces missing (MUST define before coding)

| Missing Interface | Rationale |
|---|---|
| `Transport::send(endpoint, bytes) → Result` | Abstract transport interface not defined |
| `Transport::recv() → (endpoint, bytes)` | Async vs sync? Callback-based? Poll-based? |
| `Transport::listen(endpoint) → Result` | Server-side accept loop not defined |
| `FrameHeader::read(stream) → Frame` | Zero-copy frame parser from TCP stream |
| `FrameHeader::write(stream, frame)` | Frame serializer to TCP stream |
| `VersionNegotiator::negotiate(stream) → Version` | Version handshake state machine |
| `Packet::sign(keypair) → SignedPacket`, `Packet::verify(public_key) → bool` | Packet-level signing |
| `Packet::encrypt(session_key) → EncryptedPacket` | Optional payload encryption |
| `SequenceTracker::accept(seq, sender) → bool` | Anti-replay sequence tracking per sender |

### Tests required before implementation

1. FrameHeader serialize → deserialize roundtrip (all fields preserved)
2. FrameHeader exceeds max size → error
3. TCP stream: multiple frames in single read → correctly framed
4. TCP stream: partial frame (split across reads) → buffered and reassembled
5. Version negotiation: A(3.2, 3.1, 3.0) + B(3.1, 3.0) → agreed 3.1
6. Version negotiation: A(3.2) + B(3.0) → agreed 3.0
7. Version negotiation: A(4.0) + B(3.0) → no common version → REJECT
8. Packet signing + verification roundtrip
9. Packet with bad signature → REJECT
10. Sequence number: duplicate seq from same sender → DROP
11. Sequence number: gap → ACCEPT (UDP may lose packets)
12. Transport abstraction: same contract sent over TCP and UDP → identical semantic result

---

## 3. CONNECTIVITY LAYER (Layer 11) — MVP out

### Modules involved

| Module | Responsibility |
|---|---|
| `transport/stun/` | STUN client |
| `transport/ice/` | ICE candidate gathering |
| `transport/nat/` | UDP hole punch |
| `transport/relay/` | TURN relay |

### Invariants

| Invariant | Description |
|---|---|
| I-10 | **Transport Independence** — runtime not affected by connectivity layer |

### Interfaces already defined in SPEC

| Interface | SPEC Location |
|---|---|
| STUN/ICE/TURN as layers that produce a connected socket | §6.3 |

No specific interface signatures defined (MVP out).

### Interfaces missing (MUST define before coding)

| Missing Interface | Rationale |
|---|---|
| `StunClient::discover(server) → MappedAddress` | RFC 8489 binding |
| `IceAgent::gather_candidates() → CandidateSet` | RFC 8445 candidate gathering |
| `IceAgent::pair_candidates(local, remote) → CandidatePair` | Candidate pairing + connectivity check |
| `NatPunch::hole_punch(target) → Result` | UDP hole punch |
| `TurnClient::allocate(server, credentials) → RelayAddress` | RFC 8656 allocation |
| `Connectivity::connect(peer_record) → Socket` | Unified entry point: try direct → STUN → hole punch → TURN |

### Tests required before implementation

1. STUN: request → response → mapped address matches expected
2. ICE: candidate gathering produces host + server-reflexive candidates
3. ICE: connectivity check succeeds on working pair, fails on broken pair
4. NAT hole punch: two nodes behind same NAT → direct P2P connection
5. TURN relay: data sent to relay → forwarded to target → response relayed back
6. Fallback chain: direct fails → STUN shows mapped → hole punch → succeeds
7. Fallback chain: all paths fail → error with clear reason

---

## 4. SESSION LAYER (Layer 10)

### Modules involved

| Module | Responsibility |
|---|---|
| `core/session/` | Session type definitions |
| `protocol/control/` | SESSION_OPEN, SESSION_CLOSE, SESSION_RENEW |
| `protocol/signing/` | Nonce signing |
| `protocol/encryption/` | Payload encryption (optional) |
| `storage/session_store/` | Active session persistence |

### Invariants

| Invariant | Description |
|---|---|
| I-09 | **Capability Ephemerality** — capabilities are session-scoped |
| Rule 6 | **Private Key Never Travels** — proof via signed nonce only |
| — | **Two-factor binding** — cert + signed nonce (§XVI.5) |

### Interfaces already defined in SPEC

| Interface | SPEC Location |
|---|---|
| Session binding flow | §16.5 |
| SESSION_OPEN / CLOSE / RENEW opcodes | §19.3 |
| Session carries Suite ID | §6.6 |

### Interfaces missing (MUST define before coding)

| Missing Interface | Rationale |
|---|---|
| `Session::open(peer, certificate, keypair) → Session` | Combined flow: connect → version → nonce → verify → session |
| `Session::close() → Result` | Clean teardown |
| `Session::renew() → Session` | Extend or re-negotiate |
| `Session::verify_capability(opcode) → bool` | Runtime capability check against session scope |
| `SessionStore::create(session) → id`, `lookup(id) → Session`, `expire(id)` | Session persistence |
| `Nonce::generate() → Nonce`, `Nonce::sign(keypair) → Signature`, `Nonce::verify(nonce, sig, pubkey) → bool` | Nonce challenge |
| `CapabilitySet::has(capability) → bool`, `CapabilitySet::grant(cap) / revoke(cap)` | Set operations on session capabilities |

### Tests required before implementation

1. Session open: valid cert + valid nonce signature → session established
2. Session open: expired cert → REJECT
3. Session open: wrong epoch → REJECT
4. Session open: bad nonce signature → REJECT
5. Session open: cert not in chain to Root → REJECT
6. Session capability check: capability in set → ALLOW, not in set → DENY
7. Session close: subsequent operations on closed session → REJECT
8. Session renew: extends session lifetime
9. Nonce: verification succeeds with correct keypair, fails with wrong key
10. Nonce: replay (same nonce twice) → second attempt rejected (sequence tracking)
11. SessionStore: create → lookup → expire → lookup fails

---

## 5. DISCOVERY ENGINE + ROUTING (Layer 9)

### Modules involved

| Module | Responsibility |
|---|---|
| `protocol/discovery/` | HELLO, PING, DISCOVER, NODE_INFO, OFFLINE, HEARTBEAT |
| `protocol/control/` | (routing uses discovery data) |

### Invariants

| Invariant | Description |
|---|---|
| I-23 | **Discovery Engine separate from Transport** |
| I-16 | **Protocol Layer Isolation** — doesn't know upper layers |

### Interfaces already defined in SPEC

| Interface | SPEC Location |
|---|---|
| Discovery opcodes | §19.2 |
| Gossip message structure | §6.8.3 |
| Peer Record format | §6.9.1 |
| Path selection algorithm | §6.9.2 |
| Bootstrap methods | §6.8.2 |
| Leave detection | §6.8.5 |

### Interfaces missing (MUST define before coding)

| Missing Interface | Rationale |
|---|---|
| `MembershipTable::update(peer, state)`, `peers() → PeerRecord[]` | Core data structure |
| `GossipProtocol::tick() → GossipMessage[]` | Per-round gossip (which peers to contact) |
| `GossipProtocol::process(message)` | Handle incoming gossip |
| `HealthMonitor::ping(peer) → Result` | Direct PING |
| `HealthMonitor::suspect(peer)`, `confirm(peer)` | SWIM suspicion |
| `Bootstrap::find_seed_peers() → PeerRecord[]` | Initial peer discovery |
| `RoutingTable::best_path(target) → Address` | Path selection |
| `RoutingTable::update(peer_record)` | Incorporate new latency/trust data |

**MVP note:** SWIM gossip deferred. MVP uses basic UDP HELLO/PING only.

### Tests required before implementation

1. HELLO → WELCOME: two nodes discover each other on same network
2. PING → PONG: liveness check, timing measured
3. OFFLINE: graceful departure → remaining nodes remove peer
4. DISCOVER: node requests peer list → receives known peers
5. NODE_INFO: metadata exchange (protocol version, capabilities)
6. Heartbeat timeout: 3 consecutive misses → peer marked OFFLINE
7. Membership table: insert → lookup → update → delete
8. Peer Record: serialize → deserialize roundtrip
9. Path selection: multiple addresses → lowest cost selected
10. Path selection: all addresses unreachable → "unreachable" error

---

## 6. PROTOCOL LAYER (Layer 8) — 4 protocols

### Modules involved

| Module | Responsibility |
|---|---|
| `protocol/discovery/` | UDP messages |
| `protocol/control/` | TCP: CONTRACT, SESSION, CSR, WITNESS, REVOKE, GOVERNANCE |
| `protocol/execution/` | TCP: EXEC_START, PROGRESS, RESULT, CANCEL, TIMEOUT, ERROR |
| `protocol/data/` | TCP: CHANNEL_OPEN, CHUNK, ACK, NACK, FIN, CANCEL |
| `protocol/packet/` | Frame structure, zero-copy parsing |
| `protocol/schema/` | Schema definitions |
| `protocol/signing/` | Sign + verify |
| `protocol/encryption/` | Payload encryption |
| `protocol/replay/` | Nonce, timestamp, sequence tracking |

### Invariants

| Invariant | Description |
|---|---|
| I-16 | **Protocol Layer Isolation** — 4 protocols don't know each other |
| I-01 | **Contract Purity** — contract never carries data |
| I-11 | **Opcode Replay Safety** — idempotent or declared non-idempotent |

### Interfaces already defined in SPEC

| Interface | SPEC Location |
|---|---|
| 4 protocol layers + responsibilities | §6.1 |
| Hierarchical opcode namespace | §6.5 |
| Packet binary format | §6.6 |
| Wire rules | §6.7 |
| Control protocol messages | §19.3 |
| Execution protocol messages | §19.4 |
| Data protocol messages | §19.5 |
| Governance message format | §33.6 |
| Contract JSON schema | §10.2 |

### Interfaces missing (MUST define before coding)

| Missing Interface | Rationale |
|---|---|
| `Message::namespace() → Namespace`, `Message::id() → MsgId` | Every message needs these |
| `Message::serialize() → Bytes`, `Message::deserialize(Bytes) → Result<Message>` | Per-message serialization |
| `ControlProtocol::propose_contract(contract) → Result`, `accept()`, `reject()`, `result()` | Per-message handlers |
| `ExecutionProtocol::start(contract) → ExecHandle`, `progress() → Progress`, `cancel() → Result` | Execution lifecycle |
| `DataProtocol::open_channel(target, hash) → Channel`, `chunk(Channel, Bytes) → Ack`, `finish(Channel) → Result` | Data transfer |
| `WitnessProtocol::request(contract) → Attestation` | Witness request/response |
| `GovernanceProtocol::propose(action, payload) → Proposal`, `sign(Proposal) → Signature`, `commit(Proposal)` | Governance lifecycle |
| CSR message format (not just JSON example) | Binary format for certificate signing request |

### Tests required before implementation

1. Every opcode: serialize → deserialize → field equality
2. CONTRACT_PROPOSAL: valid → accepted, invalid (missing fields) → REJECT
3. CONTRACT_PROPOSAL: duplicate contract_id → idempotent (cached result returned)
4. Witness request: valid → attestation, witness timeout → fallback
5. Execution: START → PROGRESS → EVENT → RESULT → sequence order enforced
6. Execution: CANCEL mid-stream → EXEC_CANCEL received, no more PROGRESS
7. Data: CHANNEL_OPEN → CHUNK × N → FIN → all chunks reassembled
8. Data: CHUNK with wrong channel_id → NACK → CANCEL
9. Governance: proposal → M-of-N signatures → committed
10. Governance: proposal expires before threshold → REJECTED
11. Opcode replay: idempotent opcode executed twice → same result
12. Opcode replay: non-idempotent opcode executed twice → error or allowed (per declaration)

---

## 7. COMPILER + SCHEDULER + FSM (Layers 3-5)

### Modules involved

| Module | Responsibility |
|---|---|
| `compiler/parser/` | Contract source parser |
| `compiler/planner/` | Node planning and resource mapping |
| `compiler/optimizer/` | DAG optimization (future) |
| `compiler/graph/` | DAG data structures |
| `compiler/validator/` | Semantic validation |
| `runtime/scheduler/` | DAG-aware scheduler |
| `runtime/executor/` | Opcode execution dispatch |
| `runtime/fsm/` | Node FSM + Mesh FSM + Consensus FSM |
| `runtime/fsm/transitions/` | State transition definitions |
| `runtime/audit/` | Audit logging |
| `runtime/recovery/` | State recovery |
| `runtime/workerpool/` | Concurrent work |
| `runtime/sandbox/` | Execution isolation (future) |
| `core/intent/` | Intent types |
| `core/opcode/` | Opcode enum |
| `core/state/` | FSM state types |
| `storage/dag_store/` | DAG persistence |

### Invariants

| Invariant | Description |
|---|---|
| I-03 | **Execution Graph Immutability** — DAG never mutated after compile |
| I-04 | **Deterministic State Transition** — same input + state = same output |
| I-05 | **Auditable Transition** — every transition logged |
| I-06 | **Replayable Transition** — audit log reconstructs execution |
| I-07 | **Serializable Transition** — state serializable |
| I-08 | **No Global Mutable State** — per-node isolated |
| I-19 | **Every FSM State Has Timeout and Failure Transition** |
| Rule 1 | Invariant First |
| Rule 2 | Opcode Replay Safety |
| Rule 3 | No Silent Mutation |
| Rule 5 | Execution Graph Immutability |

### Interfaces already defined in SPEC

| Interface | SPEC Location |
|---|---|
| Compiler pipeline | §13.2 |
| DAG JSON format | §13.1 |
| DAG Immutability Rule | §13.3 |
| FSM node states | §14.2 |
| FSM contract execution sub-FSM | §14.3 |
| FSM implementation rules | §14.1 |
| Scheduler priority queues | §13.4 |
| Retry policy | §13.4 |
| Deadline propagation | §13.4 |
| Cancellation flow | §13.4 |

### Interfaces missing (MUST define before coding)

| Missing Interface | Rationale |
|---|---|
| `Compiler::compile(intent) → Result<Dag>` | Main entry point |
| `Dag::node_count()`, `nodes() → NodeView[]`, `dependencies(task_id) → TaskID[]` | DAG traversal |
| `NodeView::opcode() → Opcode`, `parameters() → Params`, `priority() → u8`, `deadline() → Duration` | Per-node access |
| `Scheduler::schedule(dag) → ExecutionPlan` | Schedule DAG respecting priorities |
| `Scheduler::dispatch(plan) → ExecutionHandle` | Begin execution |
| `Scheduler::cancel(handle) → Result` | Cancel running execution |
| `Scheduler::status(handle) → ExecutionStatus` | Query status |
| `Fsm::transition(event) → Result<State>` | Generic FSM transition |
| `Fsm::current_state() → State` | State query |
| `Fsm::timeout(state) → Duration` | Per-state timeout |
| `Fsm::on_timeout(state) → State` | Timeout transition |
| `AuditLog::record(entry)`, `replay(contract_id) → State[]` | Audit + replay |
| `ExecutionStatus` enum | PENDING | RUNNING | COMPLETED | FAILED | CANCELLED | TIMEOUT |

### Tests required before implementation

1. Compiler: valid JSON contract → valid DAG
2. Compiler: invalid contract (missing opcode) → error
3. Compiler: contract with capability requirement missing → error
4. DAG: 3 independent tasks → scheduler can parallelize
5. DAG: A→B→C (linear) → scheduler runs A, then B, then C
6. DAG: A→B, A→C → scheduler runs A, then B+C in parallel
7. DAG: cyclic dependency → compiler error
8. Scheduler priority: High priority task queued after Low → High runs first
9. Scheduler retry: failure → retries with exponential backoff → eventually succeeds
10. Scheduler retry: max_attempts exhausted → FAILED
11. Scheduler cancellation: running task → cancelled → no further progress
12. Scheduler cancellation: cancelled task → downstream tasks cancelled
13. Scheduler deadline: task exceeds deadline → FAILED, downstream notified
14. FSM: every valid transition roundtrip (e.g., ACTIVE → SUSPENDED → ACTIVE)
15. FSM: every state has a timeout → timeout trigger → correct fallback state
16. FSM: invalid transition (e.g., NEW → ACTIVE) → error
17. Audit log: record all transitions → replay → identical final state
18. FSM serialization: serialize → deserialize → state equality

---

## 8. GOVERNANCE PROTOCOL (Layer 6)

### Modules involved

| Module | Responsibility |
|---|---|
| `protocol/control/` | GOVERNANCE_PROPOSAL (0x02 0x70) |
| `acl/policy/` | Mesh-level policy change |

### Invariants

| Invariant | Description |
|---|---|
| I-20 | **Governance Is Tiered by Impact** — 5 levels, configurable thresholds |
| I-22 | **Governance History Is Append-Only** |
| Rule 10 | **Governance Threshold Defined in Mesh Manifest** |
| §33.1 | **Governance does NOT govern execution** |

### Interfaces already defined in SPEC

| Interface | SPEC Location |
|---|---|
| 5 governance levels | §33.2 |
| Configurable thresholds | §33.3 |
| Conflict resolution | §33.4 |
| Compromised Authority recovery | §33.5 |
| Governance protocol message | §33.6 |
| Governance flow | §33.7 |
| Mesh split handling | §33.8 |
| Root Key usage table | §33.9 |

### Interfaces missing (MUST define before coding)

| Missing Interface | Rationale |
|---|---|
| `GovernanceProposal::new(action, payload) → Proposal` | Create proposal |
| `GovernanceProposal::sign(authority_keypair) → Signature` | Sign proposal |
| `GovernanceProposal::verify(proposal) → Result` | Verify all signatures meet threshold |
| `GovernanceProposal::threshold_from_manifest(manifest) → u8` | Read threshold per level |
| `GovernanceProposal::commit(proposal) → Result` | Apply decision to all nodes |
| `GovernanceLog::append(entry)`, `history(from, to) → Entry[]` | Append-only governance history |

### Tests required before implementation

1. Governance proposal: create → sign (1-of-1) → commit → decision applied
2. Governance proposal: create → sign (1-of-3) → not committed, sign (2-of-3) → committed
3. Governance proposal: threshold not met by expiry → REJECTED
4. Conflicting proposals: first to reach threshold wins, second is REJECTED
5. POLICY_CONFLICT: conflicting ALLOW/DENY → capability marked CONFLICTED → contract rejected
6. POLICY_CONFLICT resolution: new governance proposal → conflict cleared
7. Compromised Authority: B + C revoke A → A's cert invalidated
8. Compromised Authority: certificates signed by A become invalid (epoch increment)
9. Manifest threshold change: governance modifies manifest → new thresholds apply next proposal
10. Root Key never used after Genesis: attempt to use Root for Level 2 decision → REJECTED

---

## 9. TRUST ENGINE + WITNESS (Layer 7 + cross-cutting)

### Modules involved

| Module | Responsibility |
|---|---|
| `trust/scoring/` | Score computation |
| `trust/decay/` | Score decay over time |
| `trust/store/` | Trust data persistence |
| `trust/exchange/` | Trust digest gossip |
| `consensus/witness/` | Witness selection and protocol |
| `consensus/attestation/` | Attestation collection and verification |
| `consensus/weighting/` | Trust-weighted decision support |
| `storage/trust_store/` | Persistence |

### Invariants

| Invariant | Description |
|---|---|
| I-13 | **Trust Is Eventually Consistent** — local estimate, not global truth |
| Rule 4 | **Trust Is Eventually Consistent** — trust informs, policy decides |

### Interfaces already defined in SPEC

| Interface | SPEC Location |
|---|---|
| 4 trust components | §12.1 |
| Composite score formula | §12.2 |
| Score decay rule | §12.3 |
| Execution decision formula | §12.4 |
| Trust digest handling | §12.5 |
| Penalties | §12.6 |
| Witness role | §11.1 |
| Witness selection priority | §11.2 |
| Witness redundancy | §11.3 |
| Witness confirmation payload | §11.4 |

### Interfaces missing (MUST define before coding)

| Missing Interface | Rationale |
|---|---|
| `TrustScore::compute(component) → Score` | Per-component computation |
| `TrustScore::composite() → Score` | Weighted combination |
| `TrustScore::decay(time_window) → Score` | Sliding window decay |
| `TrustEngine::evaluate(requester_id) → Score` | Overall assessment for decision |
| `TrustEngine::penalize(requester_id, reason)` | Apply penalty |
| `TrustDigest::blend(local, remote, sender_trust) → Score` | Hint blending formula |
| `WitnessSelector::select(requester, responder) → WitnessID` | Witness selection |
| `WitnessSelector::fallback(primary) → WitnessID` | Secondary selection |
| `Attestation::verify(attestation) → Result` | Signature verification |

### Tests required before implementation

1. Trust score: good behavior → score increases over time
2. Trust score: bad behavior → score decreases (penalty applied)
3. Trust score: no activity → score decays toward neutral (0.5)
4. Composite score: each component weight affects output correctly
5. Decision formula: all conditions met AND trust >= threshold → EXECUTE
6. Decision formula: capability invalid → REJECT regardless of trust
7. Decision formula: trust < threshold without override → REJECT
8. Trust digest: local score not overwritten by remote digest (hint only)
9. Trust digest: blending weighted by sender's own trust
10. Witness: valid attestation → Responder proceeds
11. Witness: timeout → fallback to secondary → secondary also times out → local decision
12. Witness: invalid attestation (bad signature) → penalty to witness
13. Penalty: repeated rejected contracts → trust score decreases faster

---

## 10. RESOURCE MODEL (Layer 13) — MVP out

### Interfaces defined in SPEC

| Interface | SPEC Location |
|---|---|
| Resource declaration JSON schema | §30.1 |
| Default quota table | §30.2 |
| Linux primitive mapping | §30.3 |
| Resource accounting | §30.4 |

### Tests (deferred to Stage 4)

1. Resource declaration: parse → validate → enforce
2. Resource check: available >= required → ACCEPT, available < required → REJECT
3. Quota enforcement: per-mesh quota exceeded → REJECT

---

## 11. PLUGIN ABI (Layer 14) — MVP out

### Interfaces defined in SPEC

| Interface | SPEC Location |
|---|---|
| `smo_plugin_v1` C ABI struct | §31.1 |
| ABI version constant | §31.1 |
| 4 lifecycle functions: init/shutdown/execute/health | §31.1 |
| Language binding table | §31.3 |
| Sandboxing roadmap | §31.4 |

### Tests (deferred to Stage 7)

1. Plugin ABI: load `.so` → init → execute → shutdown → unload
2. Plugin ABI: abi_version mismatch → REJECTED
3. Plugin execute: valid contract → return SMO_OK
4. Plugin execute: invalid contract → return error code
5. Plugin is reentrant: multiple concurrent execute calls

---

## 12. STORAGE MODEL (§XV) — Cross-cutting

### Interfaces defined in SPEC

| Interface | SPEC Location |
|---|---|
| 8 store types | §15.1 |
| Store scopes: all per-node, no shared state | §15.2 |
| `session_store`, `trust_store`, `audit_store`, `dag_store`, `node_store`, `mesh_store`, `peer_store`, `governance_store` | §15.1 |

### Interfaces missing (MUST define before coding)

| Missing Interface | Rationale |
|---|---|
| `Store<T>::put(key, value) → Result` | Generic key-value interface |
| `Store<T>::get(key) → Result<T>` | |
| `Store<T>::delete(key) → Result` | |
| `Store<T>::list(prefix) → Result<Vec<Key>>` | |
| `Store::begin_transaction() → Transaction`, `commit()`, `rollback()` | Atomicity for multi-store operations |
| `Store::backup(path)`, `restore(path)` | Disaster recovery |

**Design decision needed:** Filesystem (SQLite? LevelDB? Custom binary?) SPEC does not specify. This must be decided before coding storage layer.

---

## 13. FAILURE MODEL (§XXIX) — Cross-cutting

### Interfaces defined in SPEC

| Interface | SPEC Location |
|---|---|
| 9 failure classes | §29.1 |
| Requester failure transitions (3 scenarios) | §29.2 |
| Responder failure transitions (3 scenarios) | §29.3 |
| Witness failure transitions (3 scenarios) | §29.4 |
| Network failure transitions (4 scenarios) | §29.5 |
| Security failure transitions (5 scenarios) | §29.6 |
| Half-execution transitions (3 scenarios) | §29.7 |
| Governance failure transitions (3 scenarios) | §29.8 |
| 5 failure invariants | §29.9 |

### Interfaces missing (MUST define before coding)

| Missing Interface | Rationale |
|---|---|
| `FailureDetector::monitor(peer) → Health` | Wraps heartbeat + suspicion |

### Tests required

Each failure scenario in §29.2-29.8 needs a dedicated test:

1. Requester crash after proposal → Responder times out → ORPHANED → GC
2. Responder crash mid-execution → restart → recover or FAILED
3. Witness timeout → secondary → local decision
4. Network partition → both sides continue → heal → governance fork detected
5. Half-execution → orphan process → cleanup on restart
6. Certificate expired mid-session → connection rejected
7. Epoch mismatch → REJECT
8. Nonce replay → DROP + penalty

---

## Summary: Missing Interfaces by Priority

| Priority | Module | Missing Interfaces |
|---|---|---|
| **P0** (Stage 1) | `transport/` | `Transport::send/recv/listen`, `FrameHeader::read/write`, `VersionNegotiator`, `Packet::sign/verify` |
| **P0** | `core/identity/` | `Keypair::generate`, `CertificateChain::verify` |
| **P0** | `protocol/` | `Message::serialize/deserialize` for all 25+ messages |
| **P0** | `runtime/fsm/` | `Fsm::transition`, `timeout`, `on_timeout` |
| **P0** | `runtime/audit/` | `AuditLog::record/replay` |
| **P0** | `session/` | `Session::open/close/renew`, `SessionStore` |
| **P0** | `core/errors/` | Error type hierarchy (needed by every module) |
| **P1** (Stage 2) | `core/enroll/` | `EnrollRequest::sign/verify`, `MeshManifest::load/verify` |
| **P1** | `storage/` | `Store<T>::put/get/delete/list`, transaction support |
| **P1** | `compiler/` | `Compiler::compile`, `Dag::nodes/dependencies` |
| **P1** | `scheduler/` | `Scheduler::schedule/dispatch/cancel` |
| **P1** | `governance/` | `GovernanceProposal::new/sign/verify/commit` |
| **P2** (Stage 3) | `discovery/` | `MembershipTable`, `GossipProtocol`, `HealthMonitor`, `Bootstrap` |
| **P2** | `routing/` | `RoutingTable::best_path`, `PeerRecord` CRUD |
| **P2** | `witness/` | `WitnessSelector`, `Attestation::verify` |
| **P2** | `trust/` | `TrustScore::compute/composite/decay`, `TrustEngine::evaluate` |

---

## Architectural Gaps (missing design decisions)

| Gap | Impact | Decision Required Before |
|---|---|---|
| **Storage backend** — SQLite? LevelDB? Custom binary? | Affects all `Store<T>` interfaces | Stage 1 |
| **Async runtime** — io_uring? epoll? asio? Custom event loop? | TCP transport design depends on this | Stage 1 |
| **Error type hierarchy** — SPEC §V mentions `core/errors/` but no types defined | Every function return type depends on this | Stage 1 |
| **Thread safety model** — Single-threaded event loop? Thread pool? Actor model? | All shared state + mutex rules | Stage 1 |
| **Config file format** — TOML? YAML? JSON? | Node config, trust weights, plugin paths | Stage 1 |
| **Logging interface** — SPEC §XXI defines directories but no API | spdlog is a dependency but interface not specified | Stage 1 |
| **Crypto abstraction** — How does runtime switch from Ed25519 to ML-DSA? | Virtual dispatch? Compile-time switch? Suite ID dispatch? | Stage 1 |
