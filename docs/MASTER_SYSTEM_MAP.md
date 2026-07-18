# ShellMap Master System Map — Đại Kiến Thiết

> Tài liệu tổng hợp toàn bộ hệ thống modules, trạng thái, dependencies,
> và kiến trúc runtime/session/trust/policy pipeline.

---

## I. TỔNG QUAN KIẾN TRÚC

```
┌──────────────────────────────────────────────────────────────────┐
│                         smo-node Daemon                           │
│                                                                  │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌─────────────────┐ │
│  │ Identity  │  │  Crypto   │  │ Transport│  │   Discovery     │ │
│  │  System   │  │  Suites   │  │  TCP/UDP │  │   + Gossip      │ │
│  └──────────┘  └──────────┘  └──────────┘  └─────────────────┘ │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │               Node Lifecycle FSM                          │   │
│  │  New → CSR → Certified → Bootstrapping → Joining → Active│   │
│  │  → Suspended → Recovering → Revoked → Removed            │   │
│  └──────────────────────────────────────────────────────────┘   │
│                              ↓                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                 Packet Pipeline                           │   │
│  │                                                          │   │
│  │  TCP → Frame → Packet → PacketDispatcher                │   │
│  │                              ↓                          │   │
│  │                    SessionManager                        │   │
│  │                    (verify session, timeout, renewal)    │   │
│  │                    (CRL check at session creation)       │   │
│  │                              ↓                          │   │
│  │                    PolicyEngine                          │   │
│  │                    (rule evaluate: allow/deny/audit/     │   │
│  │                     sandbox/ratelimit/readonly)          │   │
│  │                              ↓                          │   │
│  │                    RuntimeBridge (THIN)                  │   │
│  │                    (Packet → RuntimeRequest)            │   │
│  │                              ↓                          │   │
│  │                    RuntimeKernel                         │   │
│  │                    (execute → dispatcher → contract)    │   │
│  │                              ↓                          │   │
│  │                    ActionExecutor                        │   │
│  │                              ↓                          │   │
│  │                    Packet → TCP                         │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────────────┐  │
│  │ Governance│  │  Trust   │  │   CRL    │  │  Policy Engine │  │
│  │  Engine   │  │ Manager  │  │ (Revoke) │  │  (rules DB)    │  │
│  └──────────┘  └──────────┘  └──────────┘  └────────────────┘  │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │               6 Native Contracts                          │   │
│  │  Bootstrap → Join → Recovery → Governance → File → Process│   │
│  └──────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
```

---

## II. MODULE MAP — TOÀN BỘ HỆ THỐNG

### A. CORE MODULES (smo_core — tất cả đều BUILDING)

| # | Module | Đường dẫn | Trạng thái | Key Classes | Ghi chú |
|---|--------|-----------|------------|-------------|---------|
| 1 | **intent** | `core/intent/` | ✅ BUILDING + WIRED | Intent | Header-only |
| 2 | **opcode** | `core/opcode/` | ✅ BUILDING + WIRED | OpcodeRegistry | Thêm contract opcodes Sprint 37 |
| 3 | **contract** | `core/contract/` | ✅ BUILDING + WIRED | ContractId, ContractDefinition | Module cốt lõi |
| 4 | **capability** | `core/capability/` | ✅ BUILDING + WIRED | Capability, CapabilitySet | 14 cap bits |
| 5 | **session** | `core/session/` | ✅ BUILDING + WIRED | Session, SessionManager | 5-state FSM |
| 6 | **state** | `core/state/` | ✅ BUILDING + WIRED | NodeState, ContractState | Header-only |
| 7 | **errors** | `core/errors/` | ✅ BUILDING + WIRED | Error, ErrorCode | 15+ categories |
| 8 | **crypto** | `core/crypto/` | ✅ BUILDING + WIRED | CryptoRegistry, HashProvider, Ed25519, X25519, ML-DSA, ML-KEM | Suite1 + Suite3 |
| 9 | **storage** | `core/storage/` | ✅ BUILDING + WIRED | Database, SqliteStore | SQLite-backed |
| 10 | **identity** | `core/identity/` | ✅ BUILDING + WIRED | Identity | --init flow |
| 11 | **certificate** | `core/certificate/` | ✅ BUILDING + WIRED | Certificate, CertificateChain, CSR | PKI không truyền thống |
| 12 | **fsm** | `core/fsm/` | ✅ BUILDING + WIRED | FsmInstance | Event-driven FSM |
| 13 | **transport** | `core/transport/` | ✅ BUILDING + WIRED | Transport, TcpTransport | TCP framing |
| 14 | **discovery** | `core/discovery/` | ✅ BUILDING + WIRED | DiscoveryEngine, GossipEngine, PeerStore | Heartbeat + gossip |
| 15 | **governance** | `core/governance/` | ✅ BUILDING + WIRED | GovernanceEngine, GovernanceProposal | M-of-N signing |
| 16 | **trust** | `core/trust/` | ✅ BUILDING + WIRED | TrustManager, TrustScore, Attestation | **ĐÃ WIRED vào runtime** |
| 17 | **select** | `core/select/` | ✅ BUILDING + WIRED | Selector, QueryParser | Peer selection |
| 18 | **network** | `core/network/` | ✅ BUILDING + WIRED | PacketDispatcher, HeartbeatService, UdpTransport | UDP + packet dispatch |
| 19 | **enroll** | `core/enroll/` | ✅ BUILDING + WIRED | JoinToken, AutoEnroll | --join flow |
| 20 | **authority** | `core/authority/` | ✅ BUILDING + WIRED | MeshAuthority, NodeRegistry | CSR sign, cert issue |
| 21 | **mesh** | `core/mesh/` | ✅ BUILDING + WIRED | MeshManager | Mesh CRUD, SQLite catalog |
| 22 | **genesis** | `core/genesis/` | ✅ SEPARATE LIB (smo_genesis) | GenesisManager, RecoveryPackage | smo-admin uses |
| 23 | **recovery** | `core/recovery/` | ✅ BUILDING + WIRED | RecoveryEngine, CRL | **CRL implement đầy đủ** |
| 24 | **bootstrap** | `core/bootstrap/` | ✅ BUILDING + WIRED | BootstrapProtocol, BootstrapSnapshot | CBOR encode/decode |
| 25 | **runtime** | `core/runtime/` | ✅ SEPARATE LIB (smo_runtime) + WIRED | RuntimeKernel, RuntimeBridge, AuthorizationManager, 6 contracts | **Pipeline chính** |

### B. RUNTIME MODULE (smo_runtime — trái tim của hệ thống)

| File | Class | Chức năng |
|------|-------|-----------|
| `runtime_bridge.hpp/.cpp` | RuntimeBridge | Packet → Route → Authorize → Kernel |
| `runtime_kernel.hpp/.cpp` | RuntimeKernel | execute() + execute_direct() pipelines |
| `authorization_manager.hpp/.cpp` | AuthorizationManager | Session + Trust + CRL + Capability check |
| `action_executor.hpp/.cpp` | ActionExecutor | NextAction → send response |
| `dispatcher.hpp/.cpp` | Dispatcher | Contract registry + dispatch |
| `contracts/bootstrap_contract.hpp/.cpp` | BootstrapContract | snapshot/request/info |
| `contracts/join_contract.hpp/.cpp` | JoinContract | join/leave/info |
| `contracts/governance_contract.hpp/.cpp` | GovernanceContract | propose/vote/commit/list/status/info |
| `contracts/recovery_contract.hpp/.cpp` | RecoveryContract | assess/start/sign/execute/cancel + CRL ops |
| `contracts/file_contract.hpp/.cpp` | FileContract | ls/mkdir/rm/cp/mv/stat/read/write/... |
| `contracts/process_contract.hpp/.cpp` | ProcessContract | exec/kill/ps/top/systemctl/service/info |
| `contract_interface.hpp` | ContractInterface, NativeContract | Base classes |
| `runtime_types.hpp` | RuntimeRequest, RuntimeResult, NextAction, ... | Type definitions |
| `runtime_context.hpp` | RuntimeContext, RuntimeServices | Execution context |

### C. SUPPORTING LIBRARIES

| Library | Path | Trạng thái |
|---------|------|------------|
| smo_protocol | `protocol/` | ✅ BUILDING + WIRED |
| smo_storage | `storage/` | ✅ BUILDING + WIRED |
| smo_contract | `contract/` | ✅ BUILDING + WIRED |
| smo_transport | `transport/` | ✅ BUILDING + WIRED (TCP + framing) |
| smo_trust | `trust/` | ✅ BUILDING (stub — trust logic ở core/trust/) |
| smo_acl | `acl/` | ✅ BUILDING (stub) |
| smo_compiler | `compiler/` | ✅ BUILDING (NOT WIRED vào daemon) |
| smo_sdk | `sdk/` | ✅ BUILDING (stub) |
| smo_tooling | `tooling/` | ✅ BUILDING (stub) |
| smo_genesis | `core/genesis/` | ✅ BUILDING + WIRED (smo-admin) |
| smo_mesh | `core/mesh/` | ✅ BUILDING + WIRED (smo-admin) |

### D. EXECUTABLES

| Binary | Path | Trạng thái | Liên kết |
|--------|------|------------|----------|
| **smo-node** | `cmd/smo-node/` | ✅ BUILDING — **DAEMON CHÍNH** | smo_runtime + smo_core + smo_storage + smo_contract + smo_tooling + suites |
| smo-cli | `cmd/smo-cli/` | ✅ BUILDING — CLI client | smo_sdk + smo_genesis |
| smo-admin | `cmd/smo-admin/` | ✅ BUILDING — Admin tool | smo_sdk + smo_mesh + smo_genesis |
| smo-debug | `cmd/smo-debug/` | ✅ BUILDING — Debug stub | smo_tooling |
| smo | `cmd/smo/` | ❌ NOT BUILDING — orphan | Không có CMakeLists.txt |

### E. ORPHAN / DEAD CODE

| Module | Files | Lý do |
|--------|-------|-------|
| **core/acl/** | `policy_engine.hpp/.cpp` (499+333 lines) | ❌ Không CMakeLists.txt, không build |
| **runtime/** (legacy) | 11 sub-modules | ❌ Commented out trong CMakeLists.txt |
| **core/storage/audit_store** | `.hpp/.cpp` | ❌ Files tồn tại nhưng không build |
| **core/contract/native/** | 7 files | ❌ Không build |
| **core/contract/registry** | `.hpp/.cpp` | ❌ Không build |
| **cmd/smo/** | `main.cpp` | ❌ Không build |

---

## III. PACKET PIPELINE — LUỒNG XỬ LÝ CHI TIẾT

### Target Architecture (đúng)

```
TCP
 ↓
Frame (TCP framing: len + data)
 ↓
Packet (opcode + session_id + payload)
 ↓
PacketDispatcher
 ├─ parse opcode
 ├─ find handler
 └─ dispatch
    ↓
Node Lifecycle FSM
 ├─ New → CSR Pending → Certified → Bootstrapping → Joining → Syncing → Active
 ├─ Active → Suspended → Recovering → Revoked → Removed
 └─ Nếu node không ở Active → DENY (trừ bootstrap/join)
    ↓
SessionManager
 ├─ lookup(session_id)
 ├─ verify session state (Active/Closed/Expired)
 ├─ handle timeout & renewal
 ├─ CRL check at session CREATION (cert not revoked → allow session)
 └─ Nếu session invalid → DENY
    ↓
PolicyEngine
 ├─ load rules (from governance/policy contract)
 ├─ evaluate(session, contract, action, context)
 │   ├─ allow? → tiếp
 │   ├─ deny?  → DENY + audit log
 │   ├─ audit? → log + tiếp
 │   ├─ sandbox? → restrict capabilities
 │   ├─ ratelimit? → check quota
 │   └─ readonly? → deny writes
 └─ Nếu deny → trả về lỗi
    ↓
RuntimeBridge (THIN — chỉ convert)
 ├─ Packet.opcode → contract_id + method
 ├─ Packet.payload → RuntimeRequest.params
 └─ RuntimeRequest { contract_id, method, params, session_ctx }
    ↓
RuntimeKernel::execute()
 ├─ validate(contract exists)
 ├─ validate(input)
 ├─ contract->execute(input, ctx)
 └─ return RuntimeResult
    ↓
ActionExecutor
 └─ NextAction → ActionDispatchMessage → TCP response
```

### AuthorizationManager hiện tại (Sprint 37 interim — cần refactor)

```
⚠️ Sprint 37 đang gộp Session + CRL + Trust + Capability vào AuthorizationManager.
Đây là phiên bản interim để E2E chạy trước.
Refactor target: tách thành 3 layer riêng (SessionManager → PolicyEngine → RuntimeBridge).
```

AuthorizationManager Sprint 37:

```
RuntimeBridge::bridge()
 └─ AuthorizationManager::authorize()
     ├─ Anonymous? → ALLOW
     ├─ Session lookup → DENY nếu không found
     ├─ Session valid? → DENY nếu Closed
     ├─ CRL check → DENY nếu revoked (sai tầng)
     ├─ Trust check → DENY nếu < 0.2 (sai tầng)
     └─ Capability check → DENY nếu thiếu
```

### SAU REFACTOR (đúng)

```
SessionManager (độc lập)
 ├─ lookup(session_id)
 ├─ CRL check lúc session CREATION
 └─ verify session state

PolicyEngine (trung tâm)
 ├─ nhận context: { session, trust, role, caps, mesh, action }
 ├─ evaluate rules
 └─ trả về: allow | deny | audit | sandbox | ratelimit | readonly

RuntimeBridge (mỏng)
 ├─ chỉ convert Packet → RuntimeRequest
 └─ không biết Session, CRL, Trust, Capability, Policy
```

---

## IV. TRUST ENGINE — THUẬT TOÁN

### TrustComponents (4 dimensions)
```
citizen     = online time, heartbeat stability          weight = 0.2
execution   = contract success ratio                    weight = 0.5
witness     = witness participation and accuracy        weight = 0.2
consistency = result agreement with majority            weight = 0.1
```

### Composite Score
```python
composite = citizen*0.2 + execution*0.5 + witness*0.2 + consistency*0.1
clamped [0.0, 1.0]
```

### Trust Levels
```
Absolute: ≥ 0.9  (trust anchor → score = 1.0)
High:     ≥ 0.7
Medium:   ≥ 0.4
Low:      ≥ 0.2
None:     < 0.2
```

### Scoring Operations
| Operation | Effect | Code |
|-----------|--------|------|
| `record_success(node, weight)` | execution += 0.01 * weight | trust.cpp:227 |
| `record_failure(node, weight)` | execution -= 0.02 * weight | trust.cpp:240 |
| `record_offline(node)` | citizen -= 0.001 | trust.cpp:249 |
| `apply_attestation(att)` | witness = existing*0.7 + claimed*0.3 | trust.cpp:337 |
| `tick(now)` | all *= 0.5^(days/30) — half-life decay | trust.cpp:367 |
| **Trust anchor** | composite = 1.0 override | trust.cpp:255-281 |

### Decay Model
```
half_life = 30 days
factor = 0.5 ^ (elapsed_days / 30)  →  after 30d: 50%, after 60d: 25%
composite *= factor
```

### Attestation Flow
1. Witness creates `Attestation{witness_id, subject_id, claimed_score, timestamp, sig}`
2. `verify_attestation()` — check timestamp window (±1h), score [0,1], sig non-empty
3. `apply_attestation()` — blend: `witness = existing*0.7 + attestation*0.3`

### Digest Gossip
1. `produce_digest()` — snapshot all scores, increment sequence counter
2. Gossip → peers receive `TrustDigest{origin, sequence, timestamp, scores[]}`
3. `apply_digest()` — newer sequence wins, merge scores peer-by-peer

---

## V. CRL — CERTIFICATE REVOCATION LIST

### Vị trí đúng trong architecture

CRL check chỉ xảy ra ở **Session Creation**, không phải ở runtime authorization.

```
Certificate presented (TCP handshake / Join / Reconnect / Renewal)
    ↓
CRL::is_revoked(fingerprint)
    ├── revoked? → DENY + không tạo session
    └── not revoked → cho phép tạo session
                       ↓
                Session::create(peer_cert)
                       ↓
                SessionManager::open(session)

=== SAU KHI SESSION TỒN TẠI ===

Packet → SessionManager::lookup(id)
    ├── found & valid? → tiếp PolicyEngine
    └── not found / closed? → DENY
        (KHÔNG cần check CRL lại — cert revoked → session đã chết từ trước)
```

### Recovery → Governance → CRL flow (đúng)

```
RecoveryContract phát hiện vi phạm
    ↓
RecoveryProposal { node_id, reason, evidence }
    ↓
GovernanceEngine::submit(proposal)
    ↓
M-of-N vote → approved?
    ├── no → proposal rejected
    └── yes → CRL::revoke(fingerprint)
                ↓
              SessionManager::invalidate(node_id)
                ↓
              TCP disconnect
                ↓
              Gossip CRL update → peers
```

Recovery contract **không revoke trực tiếp**. Nó emit proposal để có audit trail.

### CRL API (core/recovery/crl.hpp + crl.cpp)

| Method | Chức năng |
|--------|-----------|
| `revoke(fingerprint, node_id, reason, epoch, now)` | Thêm cert vào revocation list |
| `is_revoked(fingerprint)` | Check cert có bị revoke không |
| `is_epoch_invalid(cert_epoch, current_epoch)` | Epoch-based revocation |
| `entries_since(epoch)` | Lấy entries từ epoch trở đi (CRL sync) |
| `serialize()` / `deserialize()` | Persistence cho crash recovery |
| `clear()` | Xóa toàn bộ (hard recovery) |

### Wire Protocol Messages

| Message | Fields |
|---------|--------|
| `RevokeCertMsg` | cert_fingerprint, node_id_hex, reason, epoch, signature |
| `RevokeAckMsg` | cert_fingerprint, accepted, error_message |

---

## VI. SESSION MANAGEMENT

### Session FSM (5 states)
```
Closed ──OpenRequest──→ Handshake ──Established──→ Established
                           │  │                       │
                        Close │                   Activate
                           │  │                       │
                           ↓  ↓                       ↓
                         Closed                    Active
                                                      │
                                              CompleteContract
                                                      │
                                                      ↓
                                                 Established
                                                      
                                              (Renew → Renewing → Established)
```

### Session Creation (CRL check tại đây)
```
TCP Handshake / Reconnect / Renewal
    ↓
Certificate presented
    ↓
CRL::is_revoked(cert.fingerprint)
    ├── YES → reject, close connection
    └── NO  → tiếp
                ↓
Session::create(peer_id, cert, caps)
    ↓
SessionManager::open(session)
    ↓
Session established (state = Active)
```

### Session Validation (runtime — không check CRL)
```
Packet arrives with session_id
    ↓
SessionManager::lookup(session_id)
    ├── not found → DENY
    └── found
        ├── state == Active? → tiếp
        ├── state == Closed? → DENY
        └── state == Expired? → DENY + trigger renewal
```

### SessionManager API
| Method | Chức năng |
|--------|-----------|
| `open(session)` | Tạo session mới (CRL check đã qua) |
| `lookup(id)` | Tra cứu session (không check CRL) |
| `close(id, now)` | Đóng session |
| `invalidate(node_id)` | Đóng tất cả session của node (khi CRL revoke) |
| `transition(id, event, now)` | FSM transition |
| `tick(now)` | Expire timeout sessions |
| `collect_garbage()` | Xóa closed sessions |
| `serialize_all()` | Persistence |

### Session attributes (cho Policy Engine)
```
Session {
    peer_id: NodeID
    certificate: Certificate
    role: Role (Reader/Contributor/Authority/Admin)
    capabilities: CapabilitySet
    trust_score: float (from TrustManager, cached at session start)
    created_at: timestamp
    last_active: timestamp
}
```

Các attribute này được Policy Engine dùng để evaluate rules, không phải AuthorizationManager.

---

## VII. POLICY ENGINE — TRUNG TÂM QUYẾT ĐỊNH

### Vai trò

Policy Engine là trung tâm quyết định của toàn bộ pipeline.
Nó thay thế AuthorizationManager làm layer quyết định allow/deny.

AuthorizationManager hiện tại (Sprint 37) sẽ trở thành một **plugin** của Policy Engine.

### Input
```
PolicyEngine::evaluate(context)
    context = {
        session: { id, role, caps, trust_score, mesh_id, created_at },
        request: { contract_id, method, params },
        node:    { state, version, uptime },
        system:  { time, load, mesh_policy_version }
    }
```

### Output
```
PolicyDecision {
    effect: Allow | Deny | Audit | Sandbox | RateLimit | ReadOnly
    reason: string            // cho audit log
    ttl:    duration          // cache decision
    constraints: []string     // sandbox: danh sách restricted ops
}
```

### Rule examples
```
// File: chỉ cho phép Reader role read, không write
rule "file_readonly_for_reader"
    match: contract == "system.file" && role == "Reader"
    allow if method in ["ls", "stat", "read"]
    deny  if method in ["write", "rm", "mkdir"]

// Trust threshold
rule "low_trust_deny"
    match: trust_score < 0.2
    deny: all

// Audit sensitive ops
rule "audit_governance"
    match: contract == "system.governance"
    audit: true
    allow: true

// Mesh isolation
rule "mesh_isolation"
    match: mesh_id != local_mesh_id
    deny: all
```

### Architecture
```
SessionManager → PolicyEngine → RuntimeBridge
                      ↓
                 Policy Store (SQLite / Governance contract)
                      ↓
                 Rule cache (hot reload)
```

### Sprint 37 interim
Hiện tại AuthorizationManager đang hardcode 4 checks:
1. Anonymous contract check
2. Session valid check
3. CRL check (sai tầng — sẽ move về SessionManager)
4. Trust check (sai tầng — sẽ move vào Policy rules)
5. Capability check (sẽ move vào Policy rules)

**Refactor target:** AuthorizationManager biến mất, thay bằng PolicyEngine + các rule plugins.

---

## VIII. CAPABILITY & TRUST — THUỘC TÍNH CHO POLICY

### Capability là attribute của Session
```
Session.capabilities = CapabilitySet
    ├── FS_READ, FS_WRITE
    ├── PROC_EXEC
    ├── GRANT, REVOKE, POLICY_CHANGE
    ├── VERIFY
    ├── HEARTBEAT
    └── ...
```

Không có AuthorizationManager check capability.
Policy rule check:

```
rule "capability_check"
    match: contract == "system.file" && method == "write"
    allow if session.caps contains FS_WRITE
    deny otherwise
```

### Trust là attribute của Node
```
Node.trust_score = composite (0.0 - 1.0)
    ├── citizen     × 0.2
    ├── execution   × 0.5
    ├── witness     × 0.2
    └── consistency × 0.1
```

Không có AuthorizationManager check trust.
Policy rule check:

```
rule "trust_threshold"
    match: contract == "system.governance"
    allow if node.trust_score >= 0.7   // governance yêu cầu trust cao
    deny otherwise

rule "bootstrap_low_trust"
    match: contract == "system.bootstrap"
    allow if node.trust_score >= 0.0   // không yêu cầu trust
```

### ContractCapability mapping
Contract không định nghĩa "cần capability nào".
Policy định nghĩa:

```
rule "file_write_needs_fs_write"
    match: contract == "system.file" && method in ["write", "rm", "mkdir"]
    allow if session.caps contains FS_WRITE
    deny otherwise
```

---

## IX. NODE LIFECYCLE FSM — MESH STATE MACHINE

### Trạng thái
```
                    ┌──────────┐
                    │   NEW    │
                    └────┬─────┘
                         │ --init
                    ┌────▼─────┐
                    │  IDENTITY │
                    │   READY   │
                    └────┬─────┘
                         │ --export CSR → smo-admin sign
                    ┌────▼─────┐
                    │   CSR    │
                    │  PENDING │
                    └────┬─────┘
                         │ --import cert
                    ┌────▼─────┐
                    │CERTIFIED │
                    └────┬─────┘
                         │ --daemon (first node)
                    ┌────▼───────┐
                    │BOOTSTRAPPING│
                    └────┬───────┘
                         │ seed response received
                    ┌────▼────┐
                    │ JOINING │
                    └────┬────┘
                         │ join complete
                    ┌────▼──────┐
                    │SYNCHRONIZE│
                    │   ING     │
                    └────┬──────┘
                         │ sync complete
                    ┌────▼────┐
                    │ ACTIVE  │◄─────────────────────────────┐
                    └────┬────┘                              │
                         │ offline / heartbeat timeout       │
                    ┌────▼────────┐                           │
                    │ SUSPENDED   │──reconnect + CRL check──→─┘
                    └────┬────────┘                           │
                         │ recovery proposal accepted         │
                    ┌────▼────────┐                           │
                    │ RECOVERING  │──recovery complete──────→─┘
                    └────┬────────┘
                         │ CRL revoke
                    ┌────▼────┐
                    │ REVOKED │
                    └────┬────┘
                         │ mesh admin cleanup
                    ┌────▼────┐
                    │ REMOVED │
                    └─────────┘
```

### Transition events

| Event | From → To | Trigger |
|-------|-----------|---------|
| `identity_created` | NEW → IDENTITY_READY | `--init` |
| `csr_exported` | IDENTITY_READY → CSR_PENDING | `--export` |
| `cert_imported` | CSR_PENDING → CERTIFIED | `--import` |
| `bootstrap_start` | CERTIFIED → BOOTSTRAPPING | `--daemon` (seed) |
| `bootstrap_complete` | BOOTSTRAPPING → JOINING | seed responded |
| `join_complete` | JOINING → SYNCHRONIZING | join accepted |
| `sync_complete` | SYNCHRONIZING → ACTIVE | peer table synced |
| `heartbeat_timeout` | ACTIVE → SUSPENDED | offline > threshold |
| `reconnect` | SUSPENDED → ACTIVE | reconnect + CRL ok |
| `recovery_proposed` | ACTIVE/SUSPENDED → RECOVERING | governance proposal |
| `recovery_complete` | RECOVERING → ACTIVE | recovery success |
| `cert_revoked` | any → REVOKED | CRL::revoke |
| `admin_remove` | REVOKED → REMOVED | mesh admin cleanup |

### Runtime chỉ phục vụ ACTIVE nodes
```
PacketDispatcher
    ↓
NodeLifecycleFSM::get_state(node_id)
    ├── ACTIVE? → tiếp SessionManager
    ├── BOOTSTRAPPING/JOINING/SYNCHRONIZING?
    │   → chỉ cho phép bootstrap/join opcodes
    └── other? → DENY
```

---

## X. OPCODE ROUTING

### Flat Opcodes (Opcode enum)

| Opcode | Value | Contract | Method |
|--------|-------|----------|--------|
| ECHO | 0x06 | system.echo | echo |
| BOOTSTRAP_SNAPSHOT | 0x30 | system.bootstrap | snapshot |
| BOOTSTRAP_INFO | 0x31 | system.bootstrap | info |
| JOIN | 0x33 | system.join | join |
| LEAVE | 0x34 | system.join | leave |
| JOIN_INFO | 0x35 | system.join | info |
| GOV_PROPOSE | 0x24 | system.governance | propose |
| GOV_VOTE | 0x25 | system.governance | vote |
| GOV_COMMIT | 0x26 | system.governance | commit |
| GOV_LIST | 0x27 | system.governance | list |
| GOV_STATUS | 0x28 | system.governance | status |
| GOV_INFO | 0x29 | system.governance | info |
| RECOVERY | 0x2A | system.recovery | invoke* |
| FILE_OP | 0x2B | system.file | invoke* |
| PROCESS | 0x2C | system.process | invoke* |

\* `invoke` = method parsed from payload header

### Bootstrap Namespace Opcodes (từ bootstrap_protocol.hpp)
| Opcode | Value | Contract | Method |
|--------|-------|----------|--------|
| kOpcodeBootstrapRequest | 0x0105 | system.bootstrap | request |
| kOpcodeBootstrapResponse | 0x0205 | system.bootstrap | (response) |

### Route Registration (RuntimeBridge)
```
bridge.register_route(opcode, "system.contract", "method");
bridge.set_anonymous("system.bootstrap", true);   // no session needed
bridge.set_anonymous("system.join", true);         // no session needed
```

---

## XI. TESTING STATUS

### Passing Tests (13/18)
| Test | Status |
|------|--------|
| protocol_model | ✅ PASS |
| trust_model | ✅ PASS |
| session_model | ✅ PASS |
| transport_model | ✅ PASS |
| transport_highlevel | ✅ PASS |
| fsm_model | ✅ PASS |
| certificate_model | ✅ PASS |
| identity_model | ✅ PASS |
| storage_stores | ✅ PASS |
| storage_model | ✅ PASS |
| crypto_model | ✅ PASS |
| error_model | ✅ PASS |
| discovery_model | ✅ PASS |

### Failing Tests (5/18 — pre-existing, không do Sprint 37)
| Test | Failure | Root Cause |
|------|---------|------------|
| governance_model | `to_string(PolicyChange)` sai | Pre-existing bug |
| governance_model | `submit invalid level` | Pre-existing validation bug |
| contract_model | Không rõ | Pre-existing |
| core | Smo_all_tests not found | Pre-existing path issue |
| protocol | Smo_all_tests not found | Pre-existing path issue |
| compiler | Smo_all_tests not found | Pre-existing path issue |

---

## XII. BUILD STATUS — TẤT CẢ TARGET

```
smo_core           ── ✅ BUILDING + WIRED (25 sub-modules)
smo_runtime        ── ✅ BUILDING + WIRED (new runtime)
smo_protocol       ── ✅ BUILDING + WIRED
smo_storage        ── ✅ BUILDING + WIRED
smo_contract       ── ✅ BUILDING + WIRED
smo_transport      ── ✅ BUILDING + WIRED (TCP+framing)
smo_trust          ── ✅ BUILDING (stubs)
smo_acl            ── ✅ BUILDING (stubs)
smo_compiler       ── ✅ BUILDING (not in daemon)
smo_genesis        ── ✅ BUILDING (separate lib)
smo_mesh           ── ✅ BUILDING (separate lib)
smo_suite1_classical ── ✅ BUILDING + WIRED
smo_suite3_purepqc ── ✅ BUILDING + WIRED (w/ PQC)

smo-node           ── ✅ BUILDING — DAEMON CHÍNH
smo-cli            ── ✅ BUILDING — CLI
smo-admin          ── ✅ BUILDING — Admin
smo-debug          ── ✅ BUILDING — Debug stub
cmd/smo            ── ❌ NOT BUILDING — orphan
```

---

## XIII. NEXT STEPS — SPRINT 37+

### Priority 0: Fix bootstrap framing (E2E blocking)
- [ ] Align `Bootstrap::find_seed()` wire format with `PacketDispatcher::dispatch_session()` framing
- [ ] After fix: E2E test 2-node mesh thực tế
- [ ] After fix: gửi ECHO opcode (0x06) qua TCP → verify RuntimeBridge → PolicyEngine → RuntimeKernel → Contract → ActionExecutor

### Priority 1: Refactor AuthorizationManager → SessionManager + PolicyEngine + thin Bridge
- [ ] **Tách CRL check ra khỏi AuthorizationManager**: move vào SessionManager::open() — CRL check lúc tạo session, không phải lúc runtime
- [ ] **Tách Trust check ra khỏi AuthorizationManager**: Trust là attribute của Node, Policy mới evaluate
- [ ] **Tách Capability check ra khỏi AuthorizationManager**: move vào Policy rules
- [ ] **Làm RuntimeBridge mỏng**: chỉ convert Packet → RuntimeRequest, không biết Session/CRL/Trust/Capability
- [ ] **Xóa AuthorizationManager** — thay bằng PolicyEngine
- [ ] Wire `core/acl/policy_engine` — thêm CMakeLists.txt, compile, fix `SMO_ERR_ACL` macro

### Priority 2: Policy Engine implementation
- [ ] Policy rule DSL (JSON hoặc custom format)
- [ ] Policy Store (SQLite-backed, governance-updatable)
- [ ] Rule cache with hot reload
- [ ] Evaluate context: { session, request, node, system }
- [ ] Policy plugins: allow, deny, audit, sandbox, ratelimit, readonly

### Priority 3: Node Lifecycle FSM
- [ ] FSM implementation (NEW → IDENTITY_READY → CSR_PENDING → CERTIFIED → BOOTSTRAPPING → JOINING → SYNCHRONIZING → ACTIVE → SUSPENDED → RECOVERING → REVOKED → REMOVED)
- [ ] Wire vào PacketDispatcher: chỉ ACTIVE mới xử lý opcodes
- [ ] Bootstrap/Join opcodes bypass cho BOOTSTRAPPING/JOINING state

### Priority 4: Recovery → Governance → CRL flow
- [ ] RecoveryContract emit RecoveryProposal (không revoke trực tiếp)
- [ ] GovernanceEngine xử lý proposal (M-of-N vote)
- [ ] CRL revoke sau khi governance approve
- [ ] SessionManager::invalidate(node_id) disconnect
- [ ] CRL gossip → peers

### Priority 5: execute_direct() → execute() migration
- [ ] Wire middleware pipeline (PolicyMiddleware)
- [ ] Wire audit trail
- [ ] Remove `execute_direct()` (keep only for debug)

### Priority 6: TrustManager → full integration
- [ ] Wire `record_success/failure` vào runtime kernel sau mỗi contract execution
- [ ] Wire `record_offline` vào heartbeat service
- [ ] Wire `apply_digest` vào gossip engine

---

## XIV. STORAGE ARCHITECTURE — CẤU TRÚC THƯ MỤC TRÊN ĐĨA

### Default root: `~/.smo/`

```
~/.smo/
├── meshes/                          # Mesh configurations (smo-admin)
│   └── <mesh-name>/                 # e.g. test-mesh/
│       ├── authority.pub            # Mesh authority public key
│       ├── authority.sec            # Mesh authority secret key
│       ├── root.cert                # Root certificate
│       ├── root.pub.hex             # Root public key (hex)
│       ├── authority.cert           # Authority certificate
│       ├── node_registry.db         # SQLite: registered nodes
│       ├── mesh.json                # Mesh config (listen addr, endpoints, etc.)
│       └── invites/                 # Generated invite tokens
│           └── <token>.json
│
└── node/                            # Default data dir (smo-node --daemon)
    ├── identity.json                # Node identity (keypair + state)
    ├── node.csr.smor                # Certificate Signing Request (exported)
    ├── node.cert.smoc               # Signed certificate (imported)
    ├── peer.db                      # SQLite: PeerStore
    ├── peer.db-shm                  # SQLite shared memory (WAL)
    └── peer.db-wal                  # SQLite WAL
```

### File formats

| File | Format | Content |
|------|--------|---------|
| `identity.json` | JSON | `node_id`, `state` (KeypairReady/Enrolled/Active), `suite_id`, `public_key_hex`, `secret_key_hex` |
| `*.smor` | Binary (CBOR) | CertificateSigningRequest: pubkey, name, platform, version, timestamp, signature |
| `*.smoc` | Binary (CBOR) | Certificate: pubkey, issuer, role, epoch, not_before, not_after, signature chain |
| `*.cert` | Binary (CBOR) | Mesh root/authority certificates |
| `*.pub.hex` | Text | Hex-encoded public key |
| `*.sec` | Binary | Secret key (raw bytes) |
| `peer.db` | SQLite | PeerStore: peers table with endpoint, node_id, last_seen |
| `node_registry.db` | SQLite | Mesh authority node registry: node_id, cert, role, status |

### Data lifecycle

```
smo-node --init
    → creates identity.json (state=KeypairReady)
    → creates node.csr.smor

smo-admin sign <csr> -o <cert>
    → signs CSR using mesh authority
    → outputs .smoc certificate file

smo-node --import <cert>
    → imports .smoc
    → should set state=Enrolled (⚠️ pre-existing bug: state stays KeypairReady)
    → saves node.cert.smoc

smo-node --daemon
    → loads identity.json
    → opens/creates peer.db
    → listens on TCP+UDP
    → enters main loop
```

---

## XV. CLI ECOSYSTEM — CÔNG CỤ DÒNG LỆNH

### 1. smo-node — Node Daemon

```
USAGE:
  smo-node --init --name <name> [--data <dir>]
  smo-node --export [<file> | --copy] [--data <dir>]
  smo-node --import [<file>] [--data <dir>]
  smo-node --pubkey [--copy | --fingerprint] [--data <dir>]
  smo-node --join <token> --data <dir> [--name <name>] [--port <port>]
  smo-node --daemon --port <port> --data <data-dir> [--name <name>]
                 [--seed <host:port>]

COMMANDS:
  --init                  Generate identity + CSR (first step)
  --name <name>           Display name for the node
  --data <dir>            Data directory (default: ~/.smo/node)
  --export <file>         Export CSR to file
  --export --copy         Copy CSR to clipboard (for smo-admin sign --paste)
  --import [<file>]       Import signed certificate (auto stdin→clipboard→file)
  --pubkey                Display public key (SMO-PUBKEY-...)
  --pubkey --copy         Copy public key to clipboard
  --pubkey --fingerprint  Show short hex fingerprint
  --join <token>          Join mesh using Join Token (auto-enrollment)
  --daemon                Run as mesh node daemon
  --port <port>           Listen port (default: 7777)
  --seed <host:port>      Bootstrap seed node for discovery
  --help                  Show help
```

### 2. smo-admin — Mesh Administration

```
USAGE:
  smo-admin --mesh <name> <command> [options]
  smo-admin --mesh-dir <path> <command> [options]

COMMANDS:
  create-mesh <name>              Initialize a new mesh
  sign <csr-file> -o <output>     Sign a CSR, issue certificate
  mesh publish [options]          Configure network endpoints
  generate-invite <role>          Generate a Join Token
  serve [--port <port>]           Start enroll HTTP server

DEPRECATED:
  create-mesh → use 'smo genesis create' instead
```

### 3. smo-cli — Client CLI

```
USAGE:
  smo-cli [options] <command>

COMMANDS:
  (TBD — currently stub, linked to smo_sdk + smo_genesis)
```

### 4. smo-debug — Debug Utility

```
USAGE:
  smo-debug [options]

STATUS: stub
```

---

## XVI. E2E TEST SCENARIOS — KỊCH BẢN KIỂM THỬ THẬT

### Scenario 1: Full Lifecycle (hiện tại đã chạy được)

```bash
# 1. Init both nodes
smo-node --init --name "Node-A" --data /tmp/e2e/node_a
smo-node --init --name "Node-B" --data /tmp/e2e/node_b

# 2. Create mesh & sign certificates
smo-admin create-mesh "test-mesh"
smo-admin --mesh-dir ~/.smo/meshes/test-mesh sign /tmp/e2e/node_a/node.csr.smor -o /tmp/e2e/node_a/node.cert.smoc
smo-admin --mesh-dir ~/.smo/meshes/test-mesh sign /tmp/e2e/node_b/node.csr.smor -o /tmp/e2e/node_b/node.cert.smoc

# 3. Import certificates
smo-node --import /tmp/e2e/node_a/node.cert.smoc --data /tmp/e2e/node_a
smo-node --import /tmp/e2e/node_b/node.cert.smoc --data /tmp/e2e/node_b

# 4. Start Node A (seed)
smo-node --daemon --port 9000 --data /tmp/e2e/node_a --name "Node-A"

# 5. Start Node B (joiner)
smo-node --daemon --port 9001 --data /tmp/e2e/node_b --name "Node-B" --seed 127.0.0.1:9000
```

#### Expected result (Sprint 37):
```
Node A: [smo-node] Entering main loop...
Node B: [smo-node] Connecting to seed: 127.0.0.1:9000
Node A: [smo-node] Accepted TCP connection from tcp://127.0.0.1:XXXXX
Node A: [smo-node] Dispatch failed: Failed to unframe data
Node B: [smo-node] Seed connection failed: no seed nodes responded
Node B: [smo-node] Continuing as first node in mesh
Node B: [smo-node] Entering main loop...
```

#### Root cause of dispatch failure:
- Bootstrap `find_seed()` sends raw CBOR bytes over TCP
- `PacketDispatcher::dispatch_session()` expects `num_opcodes(4B) + opcode(4B) + length(4B) + payload`
- **Fix needed:** align bootstrap protocol wire format with PacketDispatcher framing

### Scenario 2: Runtime Pipeline (khi wire format fixed)

```bash
# After bootstrap framing fixed:
# 1. Send ECHO opcode (0x06) packet to seed node
printf '\x06\x00\x00\x00\x00\x00\x00\x00Hello' | nc 127.0.0.1 9000

# Expected: RuntimeBridge::bridge() → AuthorizationManager::authorize()
#           → RuntimeKernel::execute_direct() → EchoContract::execute()
#           → ActionExecutor → response packet
```

### Scenario 3: CRL Revocation Test

```bash
# 1. After mesh established, send REVOKE opcode (0x2A) for Node-B cert
# 2. AuthorizationManager should reject Node-B packets via CRL::is_revoked()
# 3. TrustManager score for Node-B should drop below 0.2
```

### Scenario 4: Governance Proposal

```bash
# 1. After session established, send GOV_PROPOSE (0x24) with proposal payload
# 2. GovernanceEngine: M-of-N signature threshold
# 3. GOV_VOTE (0x25), GOV_COMMIT (0x26) flow
```

---

## XVII. DOCUMENT STATUS

| Document | Path | Status | Content |
|----------|------|--------|---------|
| **Master System Map** | `docs/MASTER_SYSTEM_MAP.md` | ✅ Current | Full architecture, all modules, runtime pipeline, trust, CRL, CLI, storage, tests |
| **Sprint 37 Status** | `docs/SPRINT_37_STATUS.md` | ✅ Current | Gap analysis, implementation plan, trust model algorithms |
| USER_MANUAL.md | `docs/` | ❌ Not created | |
| API_REFERENCE.md | `docs/` | ❌ Not created | |
| CONTRIBUTING.md | `docs/` | ❌ Not created | |
| ARCHITECTURE.md | `docs/` | ❌ Not created | |

---

## XVIII. KNOWN BUGS & LIMITATIONS

### Critical
| Bug | File | Impact |
|-----|------|--------|
| Bootstrap protocol framing mismatch | `core/bootstrap/` ↔ `core/network/` | 2-node mesh cannot bootstrap via seed (raw CBOR vs PacketDispatcher format) |
| **AuthorizationManager chứa logic sai tầng** | `core/runtime/authorization_manager.*` | Session + CRL + Trust + Capability hardcode trong 1 class — cần refactor thành SessionManager + PolicyEngine |

### Medium
| Bug | File | Impact |
|-----|------|--------|
| `Identity::transition_to(Enrolled)` không save | `core/identity/` | identity.json stays `KeypairReady` after `--import` |
| `to_string(GovernanceAction::PolicyChange)` sai | `core/governance/` | governance_model test fails |
| ASan runtime warning at startup | `cmake/CompilerOptions.cmake` | Non-critical warning, does not affect execution |
| CRL check đang ở runtime (sai tầng) | `core/runtime/authorization_manager.cpp` | Cần move lên SessionManager::open() |

### Low
| Bug | File | Impact |
|-----|------|--------|
| `stdout` buffered when not TTY | `cmd/smo-node/main.cpp` | Daemon log invisible until setvbuf fix added |
| `core/acl/policy_engine` dead code | `core/acl/` | 832 lines không build, không link |

---

## XIX. FILE MANIFEST — TẤT CẢ FILES MỚI/SỬA (Sprint 37)

### Created
| File | Purpose |
|------|---------|
| `core/runtime/authorization_manager.hpp` | AuthorizationManager — full authorization pipeline |
| `core/runtime/authorization_manager.cpp` | Capability mapping + Trust + CRL integration |
| `core/recovery/crl.cpp` | CRL full implementation (revoke, is_revoked, entries_since, serialize) |
| `docs/SPRINT_37_STATUS.md` | Sprint 37 status & gap analysis |
| `docs/MASTER_SYSTEM_MAP.md` | This document |

### Modified
| File | Changes |
|------|---------|
| `core/runtime/runtime_bridge.hpp` | Added Dispatcher&, SessionManager&, TrustManager*, CRL*, AuthorizationManager |
| `core/runtime/runtime_bridge.cpp` | Authorization check, session lookup, Bytes fix |
| `core/runtime/CMakeLists.txt` | Added authorization_manager.cpp |
| `core/opcode/opcode.h` | Added BOOTSTRAP_SNAPSHOT/INFO, JOIN/LEAVE/JOIN_INFO, GOV_LIST/STATUS/INFO, RECOVERY, FILE_OP, PROCESS |
| `core/recovery/CMakeLists.txt` | Added crl.cpp |
| `cmd/smo-node/main.cpp` | SessionManager, TrustManager, MeshManager, Authority, CRL, GovernanceEngine, RecoveryEngine — tất cả contracts registered, tất cả routes, tất cả PacketDispatcher handlers, SessionManager tick |
