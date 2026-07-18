# Discussion 0036 — Role Model, Join Token, Bootstrap Plane

**Date:** 2026-07-17  
**Participants:** @dotlinux26, @D-O-T-Solutions  
**Status:** 🔴 DRAFT — For discussion, NOT for implementation

---

## 1. Sai lầm hiện tại: 3 hệ thống role chồng chéo

Codebase hiện có **3 hệ thống role không giao nhau**:

```
Core enum:      Root, Authority, Contributor, Reader, Observer
Policy engine:  Member, Contributor, Authority, Auditor, Backup, DevOps...
CLI:            Worker, Validator, Observer, Relay
```

Đây là hậu quả của việc không phân tách **Identity** vs **Policy** vs **Runtime**.

---

## 2. Giải pháp: 3 tầng tách biệt

```
Layer 1 — Identity Role (trong Certificate, bất biến)
  Trả lời: "Bạn là ai?"
  Gồm 6 role: Root, Authority, Member, Contributor, Observer, Recovery

Layer 2 — Runtime Permission (Policy Engine)
  Trả lời: "Bạn được phép làm gì?"
  Gồm 9 policy domain: Filesystem, Process, Transfer, Vault, Mesh,
                        Governance, Workflow, Network, Audit

Layer 3 — Intent / Contract (Runtime)
  Trả lời: "Bạn đang muốn thực hiện hành động gì?"
  Intent → Policy → Scheduler → Execution → Output → Audit
```

Ba tầng này **không trộn**:
- Identity không quyết định trực tiếp quyền — chỉ map sang policy
- Policy không biết intent — chỉ evaluate allow/deny
- Runtime không biết identity — chỉ biết capability

---

## 3. Layer 1 — Identity Role (trong Certificate)

Chỉ **6 role**, bất biến trong suốt vòng đời certificate:

| Role | Mục đích | Ghi chú |
|------|----------|---------|
| **Root** | Bootstrap + Recovery Session | Không online thường xuyên |
| **Authority** | Governance, Sign CSR, Vote, Revoke | Quorum, proposal |
| **Member** | Node bình thường | Chạy runtime, gossip, seed |
| **Contributor** | Member + được publish contract | Deploy contract lên mesh |
| **Observer** | Read-only | Audit, monitor, dashboard |
| **Recovery** | Chỉ tồn tại trong Recovery Session | Cert hết hạn ngay sau recovery |

**So với hiện tại:**

| Cũ (enum) | Mới | Lý do |
|-----------|-----|-------|
| Root (0) | Root (0) | Giữ nguyên |
| Authority (1) | Authority (1) | Giữ nguyên |
| Contributor (2) | Contributor (3) | Giữ nguyên |
| Reader (3) | → **Member** (2) | "Reader" quá thụ động |
| Observer (4) | Observer (4) | Giữ nguyên |
| — | **Recovery** (5) | Mới — cho Recovery Session |

### Các role cũ bị loại khỏi Identity:

| Role cũ | Lý do | Chuyển thành |
|---------|-------|-------------|
| Worker | Không nói lên điều gì | → Member |
| DevOps | Không phải identity | → Policy Preset |
| Backup | Không phải identity | → Policy Package |
| Incident Commander | Không phải identity | → Temporary Policy (30 phút) |
| Validator | Không cần thiết ở tầng identity | → Member + Policy |
| Relay | Không cần thiết ở tầng identity | → Member + Policy |

---

## 4. Layer 2 — Runtime Permission (Policy Engine)

Chín policy domain, độc lập với identity:

```
Filesystem     FS_READ, FS_WRITE, FS_DELETE
Process        PROC_EXEC, PROC_STOP, PROC_ATTACH
Transfer       TRANSFER_SEND, TRANSFER_RECEIVE
Vault          VAULT_READ, VAULT_WRITE, VAULT_DELETE
Mesh           MESH_ADMIN, MESH_CONFIGURE, MESH_OBSERVE
Governance     GOV_PROPOSE, GOV_VOTE, GOV_COMMIT
Workflow       WF_CREATE, WF_EXECUTE, WF_CANCEL
Network        NET_BIND, NET_CONNECT, NET_RELAY
Audit          AUDIT_READ, AUDIT_EXPORT
```

### Role → Policy mapping (mặc định):

```
Authority  → Filesystem + Process + Transfer + Vault + Mesh + Governance + Audit
Contributor→ Filesystem + Process + Transfer + Vault + Workflow
Member     → Filesystem + Transfer + Network
Observer   → Audit (read-only)
Recovery   → Mesh + Governance (temporary)
Root       → Mesh + Governance (temporary, bootstrap/recovery only)
```

**Doanh nghiệp có thể sửa mapping này mà không cần đổi cert.**

---

## 5. Profile — Setup defaults, không phải identity

Profile không phải role. Profile là **cấu hình mặc định** khi join.

```
Profile: Desktop
  → Role: Member
  → Default contracts: filesystem, terminal
  → Default services: none

Profile: Server
  → Role: Member
  → Default contracts: filesystem, process, transfer
  → Default services: runtime, scheduler

Profile: Storage
  → Role: Contributor
  → Default contracts: vault, objectstore, dag
  → Default services: vault

Profile: Gateway
  → Role: Member
  → Default contracts: proxy, dns, relay
  → Default services: relay, dns

Profile: Sensor
  → Role: Observer
  → Default contracts: audit, monitor
  → Default services: heartbeat

Profile: CI
  → Role: Contributor
  → Default contracts: deploy, exec, workflow
  → Default services: none (ephemeral)
```

UX:

```bash
smo mesh invite --role authority                  # identity role
smo mesh invite --role contributor --profile ci   # role + profile
smo mesh invite --role member --profile gateway
```

Certificate chỉ chứa `role`. Profile là setup-time, không ghi vào cert.

---

## 6. Join Token format (chốt)

```cbor
{
  version: 1,
  mesh_id: "company",
  mesh_epoch: 1,
  cipher_suite: 3,

  bootstrap_endpoints: [
    "mesh.company.com:5454",
    "vpn.company.com:5454",
    "140.xxx.xxx.xxx:5454"
  ],

  admission: {
    role: "Authority",          // Layer 1 — Identity Role
    profile: "server",          // Layer phụ — setup defaults
    slot: 2                     // chỉ cho bootstrap authority
  },

  expiry: 86400,
  nonce: "a1b2c3d4",
  issuer: "root:<fingerprint>",
  signature: "..."
}
```

### Giải thích

| Field | Bắt buộc | Ghi chú |
|-------|----------|---------|
| `role` | ✅ | Identity Role (6 giá trị) |
| `profile` | ❌ | Setup defaults, mặc định "server" |
| `slot` | ❌ | Chỉ cho bootstrap authority |
| `bootstrap_endpoints` | ✅ | Endpoint Set (multi) |
| `expiry` | ✅ | Thời gian sống của token |
| `nonce` | ✅ | Chống replay |
| `signature` | ✅ | Issuer ký toàn bộ payload |

---

## 7. Bootstrap Plane vs Mesh Plane (nhắc lại)

```
Bootstrap Plane (1 lần)
───────────────────────
Join Token → Bootstrap Endpoint → CSR → Certificate → GET /mesh/bootstrap
  → Manifest + Peers + Seeds + Policies
  → Token không còn giá trị

Mesh Plane (vòng đời chính)
───────────────────────────
Seed → Discovery → Gossip → Routing → Contracts → Audit
```

---

## 8. Post-join flow

Sau khi join:

```
Node mới:
  1. GET /mesh/bootstrap
  2. Nhận Manifest + Known Peers + Seed Nodes + Policies
  3. Build routing table
  4. Connect peer qua gossip
  5. Bắt đầu vòng đời Mesh Plane

Authority không tham gia discovery nữa.
```

---

## 9. So sánh: Cũ → Mới

| Góc nhìn | Cũ | Mới |
|----------|-----|-----|
| Số hệ thống role | 3 (enum, policy, CLI) | 1 (Identity) |
| Identity Role | 5 (Root, Authority, Contributor, Reader, Observer) | 6 (Root, Authority, Member, Contributor, Observer, Recovery) |
| Worker | Role riêng | → Member |
| DevOps/Backup/IncidentCmd | Role riêng | → Policy Preset |
| Profile | Không có | Desktop/Server/Storage/Gateway/Sensor/CI |
| Certificate chứa | role duy nhất | role (identity) |
| Policy | Gắn với role | Tách biệt, configurable |
| Join Token | role string | role + profile + endpoint set |
| Recovery | Không có role | Recovery role, cert hết hạn |

---

## 10. Đã chốt: Q1–Q5 + 2 quyết định kiến trúc

### Q1: Recovery cert hết hạn thế nào?

**Single-task + TTL.**

```
Recovery Session Certificate:
  TTL:          10 phút (mặc định)
  Cho phép:     --timeout 5m, --timeout 30m
  Giới hạn:     max 60 phút
  max_actions:  1   ← ký xong proposal recovery → tự hủy
```

Không cần chờ timeout. Ký xong → destroy session.

Security: ngay cả khi cert bị leak, attacker chỉ ký được 1 action.

---

### Q2: Profile có cần trong Join Token?

**Có.** Role và Profile khác nhau, cả hai đều trong token:

```
Join Token:
  role:    "Authority"     ← security, bất biến, trong cert
  profile: "server"        ← setup defaults, không trong cert
```

Profile dùng để bootstrap:

```
desktop:   cache nhỏ, no daemon, clipboard enabled
server:    daemon, auto start, log rotate, seed enabled
embedded:  low memory, small cache, heartbeat only
gateway:   relay, dns, routing
```

Sau join, profile lưu trong `node.json`, user có thể đổi:

```bash
smo node profile set desktop
```

**Profile không phải security. Role mới là security.**

---

### Q3: Member vs Contributor khác nhau gì?

Runtime phải enforce:

| Hành vi | Member | Contributor |
|---------|--------|-------------|
| Execute contract | ✅ | ✅ |
| Deploy workflow instance | ✅ | ✅ |
| PUT/GET | ✅ | ✅ |
| Exec | ✅ | ✅ |
| Read | ✅ | ✅ |
| **Publish native contract** | ❌ | ✅ |
| **Publish workflow template** | ❌ | ✅ |
| **Publish plugin** | ❌ | ✅ |
| **Publish script** | ❌ | ✅ |
| **Update version** | ❌ | ✅ |
| Policy/Governance/Authority | ❌ | ❌ |

```
Member = Operator
Contributor = Developer
```

Runtime check:

```
PublishContract Intent
  → Policy
  → role == Contributor ?
  → Allow / Deny
```

---

### Q4: Observer có nên thấy peer list?

**Có.** Observer vẫn là member của mesh.

```
Observer:
  ✅ gossip topology
  ✅ peer discovery
  ✅ routing
  ✅ heartbeat
  ✅ manifest sync
  ❌ filesystem
  ❌ shell
  ❌ workflow
  ❌ transfer
```

Observer giống **read-only replica**: biết topology để hoạt động, nhưng không access dữ liệu.

---

### Q5: Migration role cũ?

**Có.** Backward compat:

```
Reader    → Member     (cert cũ, map tự động + warning)
Worker    → Member
Authority → Authority  (giữ nguyên)
Contributor → Contributor
Observer  → Observer
```

Runtime in warning:

```
Deprecated role: Reader → Mapped to: Member
```

Một hai release sau mới bỏ hẳn.

---

### Quyết định kiến trúc 1: Role là bất biến

Role trong Certificate **không sửa được**. Muốn đổi role:

```
Member → Contributor:
  1. Governance proposal
  2. Issue new certificate (role=Contributor)
  3. Revoke old certificate (role=Member)
```

Audit rất sạch: có trace đầy đủ ai đổi, khi nào, proposal nào.

---

### Quyết định kiến trúc 2: Profile không nằm trong Certificate

Certificate chỉ chứa identity:

```
Certificate:
  Role
  NodeID
  MeshID
  Validity
  Fingerprint
  Signature
```

Profile nằm trong `node.json`:

```
node.json:
  Profile: server/desktop/embedded/gateway
  Node Name
  Daemon Config
  Cache
  Transport
  Logging
```

Profile là cấu hình vận hành, có thể đổi post-join mà không cần cấp lại certificate.

---

## 11. Mô hình cuối cùng

```
Join Token
──────────
Role:       Authority / Member / Contributor / Observer
Profile:    server / desktop / embedded / gateway
Endpoints:  [mesh.company.com:5454, ...]
MeshID, Epoch, Suite, Expiry, Nonce, HMAC

        │
        ▼

Authority sign CSR → Issue Certificate

        │
        ▼

Certificate          node.json
──────────           ─────────
Role                 Profile
NodeID               Node Name
MeshID               Daemon Config
Validity             Cache
Fingerprint          Transport
Signature            Logging

        │
        ▼

Runtime:
  Layer 1 — Identity Role (bất biến, trong cert)
  Layer 2 — Runtime Permission (Policy Engine, configurable)
  Layer 3 — Intent / Contract (Runtime enforce)
```

---

---

## 12. Sprint 36A — Bootstrap Protocol Architecture

**Date:** 2026-07-18
**Status:** ✅ Implemented

### 12.1 Architecture decision: No HTTP between nodes

HTTP bị loại khỏi node-to-node communication. Toàn bộ bootstrap đi qua TCP Transport + Opcodes + CBOR.

```
Before:                  After:
  HTTP REST                TCP Transport
  JSON payloads            CBOR payloads
  BootstrapService         BootstrapProtocol (opcode 0x05)
```

HTTP chỉ tồn tại ở Gateway layer cho Dashboard.

### 12.2 Bootstrap Protocol (namespace 0x05)

| Opcode | Name | Direction |
|--------|------|-----------|
| `0x0001` | BOOTSTRAP_REQUEST | Node → Bootstrap endpoint |
| `0x0002` | BOOTSTRAP_RESPONSE | Bootstrap endpoint → Node |

Flow:

```
Node                          Bootstrap Endpoint
  │                                    │
  │── BOOTSTRAP_REQUEST ──────────────►│
  │    { token_wire, capabilities }    │
  │                                    │── validate token
  │                                    │── build snapshot
  │◄── BOOTSTRAP_RESPONSE ────────────│
  │    { mesh_id, authorities, seeds,  │
  │      crl_digest, health, caps }    │
```

### 12.3 PacketDispatcher

Opcodes được route qua `PacketDispatcher` thay vì if-else chain. Hai entry points:

- `dispatch(Packet&)` — cho high-level `hl::Transport`
- `dispatch_session(opcode_id, data, session)` — cho low-level `TcpSession`

### 12.4 CBOR encoding

BootstrapSnapshot và BootstrapRequest/Response dùng CBOR (thư viện tự viết ~200 LOC trong `core/bootstrap/cbor.hpp/.cpp`), không dùng JSON.

---

## 13. Sprint 36B — Signature Join Token & Root Redesign

**Date:** 2026-07-18
**Status:** ✅ Implemented (architecture frozen)

### 13.1 RootSession is session-centric, not key-centric

**Critical architecture decision:** RootSession không phải là "private key wrapper". Nó là một **phiên làm việc có đặc quyền**:

```
RootSession
├── SessionID
├── Expiry
├── SignerContext    ← opaque handle, không phải raw key
├── SessionPolicy    ← what operations are allowed
├── AuditSink        ← event-driven audit
└── state            ← Active / Expired / Consumed
```

### 13.2 SignerContext abstract class

Thay vì `void*` + function pointers, SignerContext là abstract class:

```
SignerContext (abstract)
├── sign(msg, rng) → signature
├── metadata()     → { backend, algorithm, provider, key_id }
├── valid()        → bool
└── destroy()      → zeroize + invalidate handle

Implementations:
├── SoftwareSignerContext   ← heap-allocated key, zeroed on destroy
├── TpmSignerContext        ← future
├── HsmSignerContext         ← future
├── YubiKeySignerContext    ← future
└── CloudKmsSignerContext   ← future
```

RootSession never touches raw secret key bytes. All implementations are polymorphic via `unique_ptr<SignerContext>`.

### 13.3 execute(Request) pattern

Thay vì `sign()` method riêng lẻ, RootSession cung cấp một API duy nhất:

```cpp
struct RootRequest {
    RootOperation operation;  // SignJoinToken | SignBootstrapCSR | ...
    Bytes         payload;
    uint64_t      timestamp_ns;
    std::string   reason;     // audit-friendly
};

struct RootResult {
    bool   success;
    Bytes  output;
};

// Pipeline per execute():
//   1. Validate session state + expiry
//   2. Policy check (SessionPolicy per-operation)
//   3. Delegate to SignerContext::sign()
//   4. Emit AuditEvent via AuditSink
//   5. Return RootResult
```

### 13.4 SessionPolicy (capability-based)

Policy dùng capability enum, không phải boolean fields:

```cpp
enum class Capability {
    SignJoinToken, SignBootstrapCSR, SignRecovery,
    IssueRecovery, RotateRoot, EmergencyLockdown,
    ResetEpoch, ConstitutionChange
};

// SessionPolicy holds std::set<Capability>
```

### 13.5 AuditSink (event-driven)

RootSession không lưu audit events; nó emit qua callback:

```cpp
using AuditSink = std::function<void(const AuditEvent&)>;

struct AuditEvent {
    uint64_t      timestamp_ns;
    RootOperation operation;
    std::string   reason;
    bool          success;
    std::string   details;
    // future: AuditID (UUID), RequestID, SessionID
};
```

Implementations có thể là NoOp, Console, SQLite, hoặc EventBus.

### 13.6 RecoveryPackage unlock flow

```
RecoveryPackage
│
├── verify passphrase (Blake3 hash)
├── derive AEAD key from passphrase (first 32 bytes)
├── AEAD decrypt (nonce + ciphertext)
├── parse keypair (2-byte pubkey_len + pubkey + secret_key)
├── build SoftwareSignerContext(secret_key, SignerImpl, metadata)
├── wrap in RootSession with default full SessionPolicy + no-op AuditSink
└── return RootSession
```

### 13.7 Join Token v2 wire format

```
[2-byte payload_len] [payload (CBOR)] [2-byte sig_len] [signature]
```

Variable-length signature support (ML-DSA-44/65/87 = ~2.4–4.6 KB).

---

---

## 14. Runtime Architecture Decision (2026-07-18)

**Decision:** Runtime Skeleton first (Sprint 36C), feature migration second (Sprint 36D+).

**Rationale:** SMO is now a **Distributed Operating Platform**, not just mesh+enrollment. Every operation (Join, Bootstrap, Governance, Recovery, File, Process, Vault, Workflow) must flow through a single Runtime Kernel to avoid per-feature handler debt.

### Architecture

```
All Operations
      ↓
Runtime Kernel (Validate→Resolve→Policy→Schedule→Dispatch→Collect→Aggregate→Audit→Return)
      ↓
Dispatcher (agnostic to Native vs WASM vs Python)
      ↓
Contract Interface (unified)
      ↓
Native Contracts  |  WASM Contracts  |  Future Python Contracts
```

### Key Components (Sprint 36C)

| Component | Responsibility |
|-----------|----------------|
| **EventBus** | Pub/Sub backbone; Audit/History are subscribers |
| **RuntimeKernel** | Pipeline: Validate→Resolve→Policy→Schedule→Dispatch→Collect→Aggregate→Audit→Return |
| **Dispatcher** | Doesn't know Native vs WASM — only sees ContractInterface |
| **ContractInterface** | Unified interface; Native/WASM/Python are implementations |
| **OutputManager** | Aggregator (not formatter): summary → drill-down |
| **RuntimeContext** | Per-execution: request_id, policy snapshot, crypto, event_bus, output_mgr |

### Migration Strategy (Sprint 36D)

| Current Handler | Becomes | Contract Type |
|-----------------|---------|---------------|
| `generate_invite` | `JoinContract` | Native |
| `smo join` + bootstrap | `BootstrapContract` | Native |
| `smo mesh propose` | `GovernanceContract` | Native |
| `smo recovery restore/force` | `RecoveryContract` | Native |
| `smo file put/get` | `FileContract` | Native |
| `smo exec ls/uptime` | `ExecContract` | Native |
| `smo vault put/get` | `VaultContract` | Native |
| `smo workflow run` | `WorkflowContract` | Native |

**Pattern:**
```cpp
// OLD: CLI handler does everything
int cmd_join(args) { ... network ... crypto ... done }

// NEW: CLI submits to Runtime
int cmd_join(args) {
    RuntimeRequest req;
    req.contract = "system.join";
    req.input = encode(args);
    req.context = build_context();
    return Runtime::instance().execute(req).match(...);
}
```

### Deferred (Post-Skeleton)

| Component | When |
|-----------|------|
| AuditService | Sprint 36E — EventBus subscriber → SQLite |
| HistoryStore | Sprint 36E — EventBus subscriber → event sourcing |
| Scheduler | Sprint 36F — priority queues, cron, deadlines |
| Transport + Gossip | Sprint 36G — TLS, mTLS, seed resolver, connection pool |
| WASM Contract Runtime | Sprint 36H — wasmtime/wasm3, host functions, gas metering |

### Consequences

| Positive | Negative |
|----------|----------|
| Zero refactor when adding new operations | More upfront skeleton code |
| Single audit/log/policy point | Slightly higher latency for trivial ops |
| Native/WASM/Python unified | Dispatcher complexity |
| Output aggregation built-in | OutputManager abstraction |
| EventBus enables observability | Event ordering guarantees needed |

---

## References

- [Discussion 0035 — PKI & Governance](DISCUSSION_0035_PKI_GOVERNANCE.md)
- [Discussion 0034 — UX Mesh Context](DISCUSSION_0034_UX_MESH_CONTEXT.md)
- [RFC 0032 — Context-Aware CLI](../../RFC/0032-context-cli.md)
- [RFC 0007 — Enrollment Protocol](../../RFC/0007-enrollment-protocol.md)
- [RFC 0034 — Bootstrap Protocol](../../RFC/0034-bootstrap-protocol.md)
- [RFC 0033 — Mesh Genesis & Governance](../../RFC/0033-mesh-genesis-governance.md)
- [RFC 0035 — Runtime Architecture](../../RFC/0035-runtime-architecture.md)
- [`core/certificate/certificate.hpp`](../../core/certificate/certificate.hpp) — Role enum
- [`core/crypto/signer_context.hpp`](../../core/crypto/signer_context.hpp) — SignerContext abstract class
- [`core/genesis/root_session.hpp`](../../core/genesis/root_session.hpp) — RootSession (session-centric)
- [`core/enroll/join_token.hpp`](../../core/enroll/join_token.hpp) — Join Token v2
- [`core/bootstrap/bootstrap_protocol.hpp`](../../core/bootstrap/bootstrap_protocol.hpp) — Bootstrap Protocol
- [`core/network/packet_dispatcher.hpp`](../../core/network/packet_dispatcher.hpp) — PacketDispatcher
