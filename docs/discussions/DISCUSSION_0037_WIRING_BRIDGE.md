# DISCUSSION 0037 — Wiring Bridge: Nối Transport + RuntimeKernel + Contracts

**Ngày:** 2026-07-18
**Trạng thái:** Draft — chờ review

---

## Nhận định

Transport đã chạy, Runtime đã chạy, Contracts đã chạy — nhưng hai khối chưa được nối với nhau. Đây là trạng thái rất thường gặp ở các framework runtime.

Nếu nhìn theo kiến trúc hiện tại thì pipeline của ShellMap như sau:

```
                    NETWORK
              ┌────────────────┐
              │ TCP Listener   │
              │ UDP Listener   │
              └───────┬────────┘
                      │
          recv()/send()/heartbeat
                      │
              PacketDispatcher
                      │
          lookup opcode handler
                      │
          Packet -> RuntimeRequest
                      │
              RuntimeKernel
                      │
           Dispatcher / PlanExecutor
                      │
          Contract::execute(...)
                      │
             ContractResult
                      │
             NextActions
          ┌───────────┴────────────┐
          │                        │
    Dispatch Packet         Dispatch Contract
          │                        │
     TCP/UDP send            RuntimeKernel
```

Đến hiện tại đã hoàn thành tới đây:

```
TCP/UDP             ✓
Packet Parser       ✓
Packet Dispatcher   ✓
RuntimeKernel       ✓
Contracts           ✓

NHƯNG

PacketDispatcher
      │
      X
RuntimeKernel
```

Thiếu đúng cầu nối (bridge) giữa `PacketDispatcher` và `RuntimeKernel`.

---

## 1. Kiến trúc tổng thể hiện tại

```
cmd/smo-node/main.cpp              (daemon chính)
├── core/transport/                 (TCP + framing + registry)
├── core/network/                   (PacketDispatcher + UDP + Heartbeat)
├── core/session/                   (Session FSM + SessionManager)
├── core/runtime/                   (RuntimeKernel + Dispatcher + Contracts)
│   └── core/runtime/contracts/     (6 NativeContracts)
├── core/bootstrap/                 (CBOR + BootstrapProtocol + Snapshot)
├── core/governance/                (GovernanceEngine + 2-tier proposals)
├── core/recovery/                  (RecoveryEngine + CRL)
├── core/genesis/                   (SlotRing + Manifest + RecoveryPackage + RootSession)
├── core/enroll/                    (JoinToken v2 CBOR + signature)
├── core/identity/                  (NodeID, keypair)
├── core/certificate/               (Certificate chain)
├── core/authority/                 (MeshAuthority)
├── core/mesh/                      (MeshState + MeshFsm)
├── core/opcode/                    (OpcodeRegistry)
├── core/storage/                   (8 SQLite stores)
├── protocol/packet/                (Packet serde)
└── transport/                      (High-level TCP — unused)
```

---

## 2. Trạng thái từng module

| Module | File | Trạng thái |
|--------|------|------------|
| TCP Transport (framing + handshake) | `core/transport/tcp_transport.cpp` | ✅ |
| UDP Transport | `core/network/udp/` | ✅ |
| PacketDispatcher (route by opcode) | `core/network/packet_dispatcher.cpp` | ✅ |
| Packet serde (packet_from/to_buffer) | `protocol/packet/packet.cpp` | ✅ |
| OpcodeRegistry (10 builtin) | `core/opcode/opcode_registry.cpp` | ✅ |
| RuntimeKernel (8-stage pipeline) | `core/runtime/runtime_kernel.cpp` | ✅ |
| Dispatcher (contract lookup + execute) | `core/runtime/dispatcher.cpp` | ✅ |
| PlanExecutor (12-step FSM + NextAction) | `core/runtime/plan_executor.cpp` | ✅ |
| EventBus (pub/sub) | `core/runtime/event_bus.cpp` | ✅ |
| Middlewares | `core/runtime/middleware.cpp` | ✅ |
| OutputManager | `core/runtime/output_manager.cpp` | ✅ |
| ContractRegistry + Manager | `core/runtime/contract_registry.cpp` | ✅ |
| 15 Service Interfaces | `core/runtime/services/*.hpp` | ✅ |
| RuntimeTypes (ContextValue, NextAction...) | `core/runtime/runtime_types.hpp` | ✅ |
| JoinContract | `core/runtime/contracts/join_contract.cpp` | ✅ |
| BootstrapContract | `core/runtime/contracts/bootstrap_contract.cpp` | ✅ |
| GovernanceContract | `core/runtime/contracts/governance_contract.cpp` | ✅ |
| RecoveryContract | `core/runtime/contracts/recovery_contract.cpp` | ✅ |
| FileContract | `core/runtime/contracts/file_contract.cpp` | ✅ |
| ProcessContract | `core/runtime/contracts/process_contract.cpp` | ✅ |
| Session FSM (7 states, 8 events) | `core/session/session.cpp` | ✅ |
| SessionManager (open/lookup/close/tick/GC) | `core/session/session.cpp` | ✅ |
| BootstrapProtocol (CBOR) | `core/bootstrap/` | ✅ |
| GovernanceEngine (2-tier, 16 actions) | `core/governance/governance.cpp` | ✅ |
| RecoveryEngine (Soft/Hard) | `core/recovery/recovery_engine.cpp` | ✅ |
| CRL (revoke/check/sync) | `core/recovery/crl.hpp` | ✅ |
| MeshGenesis (SlotRing, Manifest) | `core/genesis/` | ✅ |
| RootSession (session-centric signing) | `core/genesis/root_session.cpp` | ✅ |
| JoinToken v2 (CBOR + Ed25519) | `core/enroll/join_token.cpp` | ✅ |
| HeartbeatService (UDP) | `core/network/udp/heartbeat_service.cpp` | ✅ |
| **PacketDispatcher → RuntimeKernel bridge** | — | ❌ |
| **RuntimeKernel::dispatch() stage** | `core/runtime/runtime_kernel.cpp` | ❌ (stub) |
| **SessionManager wired into network** | — | ❌ |
| **General opcode → contract routing** | — | ❌ (chỉ có BOOTSTRAP_REQUEST) |

---

## 3. Input/Output của từng khớp nối

### 3.1 TCP Listener → PacketDispatcher

```
Input:  raw bytes từ socket accept()
        ↓
        frame_read() → unframe (9-byte header + payload)
        ↓
        packet_from_buffer() → Packet struct
        ↓
        lookup handler by packet.opcode_id
        ↓
Output: gọi handler(packet, remote_endpoint, transport_adapter)
```

**Hiện tại:** chỉ có 1 handler `BOOTSTRAP_REQUEST` được đăng ký (main.cpp:932).

---

### 3.2 Packet → RuntimeRequest (CẦN XÂY)

```
Input:  Packet { header, session_id, intent_id, opcode_id,
                 timestamp, nonce, payload, signature }
        ↓
        opcode_registry.resolve(opcode_id) → contract_id + method
        ↓
        payload → ContextValue (CBOR)
        ↓
Output: RuntimeRequest { contract_id, method, arguments, session_id, timestamp }
```

**Đây là bridge cần xây.** Mỗi opcode cần map tới một contract + method:

| Opcode | Contract | Method |
|--------|----------|--------|
| BOOTSTRAP_REQUEST (0x05/0x01) | system.bootstrap | bootstrap_request |
| BOOTSTRAP_RESPONSE (0x05/0x02) | system.bootstrap | bootstrap_response |
| GOV_PROPOSE (0x24) | system.governance | propose |
| GOV_VOTE (0x25) | system.governance | vote |
| GOV_COMMIT (0x26) | system.governance | commit |
| RECOVERY_SESSION (0x22) | system.recovery | start / sign / execute |
| CRL_SYNC (0x23) | system.recovery | crl_sync |
| REVOKE_CERT (0x20) | system.recovery | crl_revoke |
| LS (0x01) / PUT (0x02) / ... | system.file | list / write / ... |
| EXEC (0x04) | system.process | exec |

---

### 3.3 RuntimeKernel → Dispatcher → Contract

```
Input:  RuntimeRequest { contract_id, method, arguments }
        ↓
        RuntimeKernel::execute():
          1. validate()
          2. resolve() → ExecutionPlan
          3. run_middlewares("before_resolve")
          4. execute_plan() → PlanExecutor
          5. run_middlewares("before_dispatch")
          6. dispatch()        ← HIỆN TẠI RỖNG
          7. collect()         ← HIỆN TẠI RỖNG
          8. aggregate()
          9. audit()           ← HIỆN TẠI RỖNG
          10. complete()       ← HIỆN TẠI RỖNG
        ↓
Output: ContractResult { status, data, binary, next_actions, metrics }
```

Mỗi step nhận `RuntimeContext` chứa:
- `ExecutionInfo` (const) — request_id, contract_id, method, requester, deadline
- `Variables` (mutable) — key-value store cho plan execution
- `RuntimeServices*` — 15 service pointers (crypto, vault, storage, fs, network...)

---

### 3.4 ContractResult → NextActions → Network

```
Input:  ContractResult::next_actions = vector<NextAction>
        ↓
        PlanExecutor::dispatch_next_actions():
          std::visit(overloaded {
            ActionDispatchContract c  → gọi contract khác (local)
            ActionDispatchMessage m  → gửi packet qua network  ← QUAN TRỌNG
            ActionScheduleRetry r    → retry sau
            ActionEmitEvent e        → pub EventBus
            ActionStoreContext s     → lưu biến
            ActionSpawnPlan p        → spawn plan mới
            ActionNotify n           → notify
            ActionCompensate c       → rollback
            ActionAbort a            → hủy
          }, action);
        ↓
Output: Với ActionDispatchMessage → TransportAdapter::send(packet, remote)
```

**Đây là output bridge.** Contract không gọi network trực tiếp — nó trả về `NextAction`, và PlanExecutor hoặc RuntimeKernel thực thi việc gửi.

---

### 3.5 Heartbeat (UDP)

```
Input:  udp_listener → recvfrom() → heartbeat packet
        ↓
        HeartbeatService::tick() → parse → update peer table
        ↓
Output: metrics + audit (qua EventBus)
```

---

### 3.6 SessionManager (CẦN WIRE)

```
Input:  TCP accept() → new connection
        ↓
        SessionManager::open() → Session FSM (Handshake → Established → Active)
        ↓
        Session key = f(node_id, mesh_id)
        ↓
Output: Session cho mọi packet tiếp theo (bỏ qua handshake)
```

**Hiện tại:** connection vào bỏ qua Session hoàn toàn — không có session establishment.

---

## 4. Các giải thuật đã hoàn thiện

### 4.1 Gossip

Gossip engine trong `core/network/gossip.cpp`:
- `GossipEngine` — epidemic-style broadcast
- `MembershipSync` — đồng bộ peer table
- `HealthMonitor` — health check định kỳ
- Đã tích hợp vào main loop của smo-node

### 4.2 Discovery

`core/discovery/`:
- Ping/pong response
- Seed resolver (kết nối tới seed node để lấy peer table)
- LAN discovery (broadcast)

### 4.3 Mesh FSM (đục lỗ — hole punching)

`core/mesh/mesh_fsm.cpp`:
- 12 transition rules
- 5 timeouts (JoinTimeout, BootstrapTimeout, HealthTimeout, RecoveryTimeout, SyncTimeout)
- States: Null → Initializing → Joining → Bootstrapping → Online → Degraded → Recovery → Offline

### 4.4 SlotRing (đục lỗ cho bootstrap)

`core/genesis/bootstrap_slot.hpp`:
- SlotRing — vòng tròn slot cho bootstrap
- Mỗi slot có state: Waiting, Fulfilled, Expired, Revoked
- Khi tất cả slot Fulfilled → Mesh Online, Root → Dormant

### 4.5 Governance (2-tier)

`core/governance/governance.cpp`:
- Tier A (Membership): registry-only, 1 authority = 1 vote
- Tier B (Constitution): thay đổi manifest, epoch, policy — cần quorum cao hơn
- 16 GovernanceAction codes
- MeshHealth: Healthy → Warning → Critical → Recovery

### 4.6 Recovery (Soft/Hard)

`core/recovery/recovery_engine.cpp`:
- Soft: còn quorum → epoch++ chỉ authority bị replace
- Hard: mất quorum → epoch++ → invalidate ALL certs + CRL mới → bootstrap lại

---

## 5. Luồng thực tế nếu hoàn thiện

### 5.1 Node A khởi động

```
main()
  → load config
  → Identity
  → Vault
  → Crypto
  → Transport
  → RuntimeKernel
  → Register NativeContracts
  → listen TCP
  → listen UDP
```

Node Ready.

---

### 5.2 Join mesh

```bash
smo join
```

CLI tạo `RuntimeRequest`:

```
contract = system.join
method   = join
arguments: { mesh_id, seed, certificate }
```

→ RuntimeKernel → Dispatcher → JoinContract

JoinContract quyết định:
- Bootstrap
- Handshake
- Identity verify
- Network send

---

### 5.3 JoinContract gửi BOOTSTRAP_REQUEST

JoinContract sinh `ActionDispatchMessage`:

```
opcode = BOOTSTRAP_REQUEST
payload = { nonce, node_id, cert_fingerprint }
```

→ RuntimeKernel → TransportAdapter → TCP send() → Seed Node

---

### 5.4 Seed nhận packet

```
TCP → PacketDispatcher → Opcode = BOOTSTRAP_REQUEST
```

**Đây chính là đoạn còn thiếu.**

Thay vì `handler(packet)`, nó phải chuyển thành:

```
RuntimeRequest req
req.contract = "system.bootstrap"
req.method   = "bootstrap_request"
req.arguments = ...
```

→ RuntimeKernel.execute() → BootstrapContract → ContractResult → ActionDispatchMessage → BOOTSTRAP_RESPONSE → TCP send()

---

### 5.5 Node A nhận response

```
TCP → PacketDispatcher → RuntimeRequest → BootstrapContract
  → ContractResult → NextAction::DispatchContract → system.join
  → JoinContract tiếp tục
    → Identity verify
    → Membership update
    → Audit
    → Success
```

---

## 6. Dự định sát nhập (Wiring Plan)

### Bước 1: Opcode → Contract mapping table

Tạo `RuntimeBridge` — singleton chứa mapping opcode → (contract_id, method):

```cpp
// core/runtime/runtime_bridge.hpp
struct OpcodeRoute {
    std::string contract_id;
    std::string method;
};

class RuntimeBridge {
public:
    void register_route(Opcode opcode, OpcodeRoute route);
    Result<OpcodeRoute> resolve(Opcode opcode) const;

    // Bridge chính
    Result<void> dispatch_packet(
        const Packet& pkt,
        const std::string& remote,
        TransportAdapter& transport);

private:
    std::unordered_map<Opcode, OpcodeRoute, OpcodeHash> routes_;
    RuntimeKernel* kernel_;
};
```

---

### Bước 2: PacketDispatcher → RuntimeBridge

Sửa `PacketDispatcher::dispatch_session()` để:

```cpp
auto route = bridge.resolve(static_cast<Opcode>(packet.opcode_id));
if (route) {
    RuntimeRequest req;
    req.contract_id = route->contract_id;
    req.method = route->method;
    req.arguments = ContextValue::from_bytes(packet.payload);
    req.requester = packet.session_id;

    RuntimeContext ctx = kernel_.create_context(req);
    auto result = kernel_.execute(req, ctx);

    for (auto& action : result.next_actions) {
        if (auto* msg = std::get_if<ActionDispatchMessage>(&action)) {
            Packet resp = build_packet(msg->opcode, msg->payload, packet.session_id);
            adapter.send(resp, remote);
        }
    }
}
```

---

### Bước 3: TransportAdapter

Wrapper cho phép cả TCP và UDP gửi packet:

```cpp
class TransportAdapter {
public:
    void send(const Packet& pkt, const std::string& remote);
    void broadcast(const Packet& pkt);

private:
    TransportRegistry* registry_;
    std::string preferred_protocol_;
};
```

---

### Bước 4: Wire SessionManager vào accept loop

```
TCP accept()
  → SessionManager.open_or_lookup(remote_addr, cert)
    → Nếu mới: Handshake → Established
    → Nếu cũ: lookup session
  → Session gắn với mọi packet tiếp theo
```

---

### Bước 5: RuntimeKernel::dispatch() + collect() + audit()

Ba step đang rỗng cần implement:

- `dispatch()`: lấy `ActionDispatchMessage` từ `next_actions` và gửi qua TransportAdapter
- `collect()`: thu thập kết quả từ các node khác (nếu cần)
- `audit()`: emit AuditEvent qua AuditService (EventBus subscriber → SQLite)

---

## 7. Thứ tự triển khai

| # | Task | Thời gian |
|---|------|-----------|
| 1 | RuntimeBridge (opcode → contract mapping) | 1-2 ngày |
| 2 | PacketDispatcher → RuntimeBridge integration | 1-2 ngày |
| 3 | TransportAdapter (send/broadcast) | 1 ngày |
| 4 | RuntimeKernel::dispatch() (gửi NextAction ra network) | 1 ngày |
| 5 | Wire SessionManager vào accept loop | 2-3 ngày |
| 6 | RuntimeKernel::audit() + AuditService | 1-2 ngày |
| 7 | Integration test: 2-3 node Join + Bootstrap | 2-3 ngày |
| | **Total** | **~10-14 ngày** |

---

## 8. Kết luận

Các thành phần giống như những mảnh LEGO đã hoàn chỉnh:

| Module | Trạng thái |
|--------|------------|
| TCP/UDP Transport | ✅ |
| Packet + Framing | ✅ |
| PacketDispatcher | ✅ |
| RuntimeKernel (8 stages) | ✅ |
| Dispatcher | ✅ |
| Native Contracts (6) | ✅ |
| PlanExecutor + NextAction | ✅ |
| Gossip + Discovery + Mesh FSM | ✅ |
| Session FSM + Manager | ✅ |
| **Bridge PacketDispatcher ↔ RuntimeKernel** | **❌** |

Nếu viết unit test lúc này, chỉ kiểm tra từng khối riêng lẻ mà chưa chứng minh được luồng thực tế hoạt động.

Thứ tự ưu tiên:
1. **Wiring**: `PacketDispatcher ↔ RuntimeKernel ↔ NativeContracts ↔ TransportAdapter`
2. **Integration tests**: Join, Bootstrap, Heartbeat, Governance giữa 2-3 node
3. **Unit tests**: bổ sung cho từng contract/service
4. Sau khi có end-to-end ổn định → **Sprint 37** (tính năng mới)

Đó là thứ sẽ biến framework từ "đã implement đủ module" thành "một mesh network thực sự có thể giao tiếp và thực thi contract".

---

## 9. Review kiến trúc 3 tầng (bổ sung 2026-07-18)

Đọc qua toàn bộ codebase, kiến trúc ShellMap hiện tại có thể chia thành **3 tầng** rõ ràng:

```
                    CLI / RPC / UI
                          │
                    RuntimeRequest
                          │
                   RuntimeKernel
                          │
                 Dispatcher / PlanExecutor
                          │
                  Native Contracts (RFC36)
                          │
          RuntimeServices (Crypto, Vault, Network...)
                          │
                TCP / UDP Transport Layer
                          │
                      Remote Node
```

Điều đáng chú ý là **tầng Runtime đã hoàn thiện**, còn **network vẫn chưa được nối vào Runtime**.

---

### 9.1 Luồng hiện tại (build hiện nay)

Giả sử Node A muốn Join Mesh. Hiện giờ thực tế đang chạy như này:

```
main()
  → Load Config
  → Identity
  → Crypto
  → TransportRegistry
  → TCP Listener
  → UDP Listener
  → Heartbeat
  → Bootstrap
  → PacketDispatcher
  → while() {
      accept tcp
      parse packet
      lookup opcode
      handler(packet)
    }
```

Tức là handler xử lý packet trực tiếp. Nó **không đi qua RuntimeKernel**. Nên JoinContract thực ra chưa bao giờ được execute.

---

### 9.2 Luồng mà RFC đang hướng tới

Sau Sprint 36D thì đúng ra nó phải thành:

```
TCP recv
  → Frame
  → Packet
  → PacketDispatcher
  → RuntimeRequest
  → RuntimeKernel::execute()
  → Dispatcher
  → JoinContract.execute()
  → ContractResult
  → NextAction
  → Dispatcher xử lý NextAction
  → Transport send()
```

Đây mới là flow của framework.

---

### 9.3 TCP và UDP sẽ chia việc như thế nào?

Theo RFC và code hiện tại, phân chia rất hợp lý:

#### UDP — traffic realtime

| Use case | Đặc điểm |
|----------|----------|
| Heartbeat | nhanh, không đảm bảo, không retry, không session |
| Peer Discovery | |
| Gossip | |
| Broadcast | |
| Metrics / Presence | |
| Latency probe | |
| NAT traversal | |

Ví dụ:

```
Node A
  → Heartbeat Packet
  → UDP sendto
  → Node B
  → Heartbeat handler
  → update peer status
```

Không cần Runtime. Chỉ Packet Handler là đủ.

#### TCP — operation có state

| Use case | Đặc điểm |
|----------|----------|
| Join | có session, handshake, authenticated |
| Recovery | |
| Governance | |
| Vault sync | |
| File transfer | |
| Contract invoke | |
| Bootstrap | |

Ví dụ:

```
TCP accept
  → Session
  → Handshake
  → Authenticated
  → Packet
  → RuntimeRequest
  → RuntimeKernel
  → Contract
  → ContractResult
  → reply
  → close
```

---

### 9.4 File Transfer

Sau này FileContract sẽ chạy:

```
CLI
  → put file
  → RuntimeRequest
  → FileContract
  → chunk file
  → ActionDispatchMessage
  → Transport
  → TCP
  → remote FileContract
  → write chunk
  → ack
```

Toàn bộ logic nằm trong Contract. Network chỉ vận chuyển packet.

---

### 9.5 Governance

Ví dụ vote:

```
vote
  → GovernanceContract
  → verify signature
  → store proposal
  → ActionNotify
  → Transport
  → broadcast
  → other nodes
  → GovernanceContract
  → update state
```

---

### 9.6 Recovery

```
recover
  → RecoveryContract
  → VaultService
  → CryptoService
  → NetworkService
  → ContractResult
```

---

### 9.7 Heartbeat

Heartbeat sẽ **không cần Runtime**:

```
HeartbeatService
  → UDP
  → Packet
  → HeartbeatHandler
  → PeerTable
```

Bởi vì đây là traffic rất thường xuyên (ví dụ mỗi 1–5 giây), đưa qua Runtime sẽ tạo overhead không cần thiết.

---

### 9.8 Bootstrap

Bootstrap hiện nay gần đúng:

```
Node Start
  → Bootstrap seed
  → TCP
  → BOOTSTRAP_REQUEST
  → BootstrapHandler
  → PeerTable
```

Sau khi wire Runtime thì sẽ thành:

```
BOOTSTRAP packet
  → RuntimeRequest
  → BootstrapContract
  → ContractResult
  → Peer list
  → reply
```

---

### 9.9 Join

Đây là luồng chuẩn mà Contract vừa được implement:

```
Node A
  → JOIN packet
  → PacketDispatcher
  → RuntimeRequest
  → RuntimeKernel
  → Dispatcher
  → JoinContract
  → IdentityService
  → CryptoService
  → PolicyEngine
  → Storage
  → ContractResult
  → ActionNotify
  → Transport
  → Node B
```

---

### 9.10 Toàn bộ ứng dụng sau khi wire xong

```
             CLI
              │
              ▼
      RuntimeRequest
              │
              ▼
      RuntimeKernel
              │
      Dispatcher
              │
     Native Contracts
              │
 RuntimeServices
(Crypto/Vault/Storage/...)
              │
      NextAction
              │
      PacketDispatcher
              │
     TCP / UDP Transport
              │
         Remote Node
```

Đây là kiến trúc khá giống các runtime hiện đại: **Contract chỉ quyết định "làm gì", Runtime điều phối thực thi, Transport chỉ chịu trách nhiệm truyền dữ liệu**.

---

### 9.11 Đánh giá hiện tại

| Layer | Mức độ hoàn thiện | Ghi chú |
|-------|-------------------|---------|
| Runtime Framework | ~95–98% | RFC 36 đã khá hoàn chỉnh |
| Native Contracts | ~90–95% | Join, Bootstrap, Governance, Recovery, File, Process |
| Transport (TCP/UDP) | ~85–90% | Listener, framing, session, packet |
| **Wiring PacketDispatcher ↔ RuntimeKernel** | **~0%** | Mảnh ghép cuối còn thiếu |

Điểm còn thiếu lớn nhất là **wiring giữa PacketDispatcher và RuntimeKernel**. Đó cũng là "mảnh ghép cuối" để biến các contract từ những module đã biên dịch được thành các thành phần thực sự xử lý lưu lượng mạng. Sau bước wiring, hệ thống mới chạy đúng theo kiến trúc mà toàn bộ RFC 0035–0040 đã thiết kế.

---

## 10. Đề xuất RFC 0041–0054 (bổ sung 2026-07-18)

Đọc toàn bộ RFC 0035→0040 và bản phân tích ở trên, framework đã gần hoàn thiện tầng Runtime. Không đề xuất thêm contract mới, mà nên đóng các **kiến trúc còn thiếu**. Ưu tiên theo thứ tự dưới đây.

---

### 10.1 RFC 0041 — Runtime Bridge & Protocol Mapping ⭐⭐⭐⭐⭐

Nâng DISCUSSION này thành RFC chính thức. Freeze cầu nối:

```
Packet → Opcode → RuntimeRequest → RuntimeKernel → Contract → ContractResult → NextAction → Packet
```

Định nghĩa:
- OpcodeRegistry
- RuntimeBridge
- RuntimeRequest Builder
- RuntimeResponse Builder
- Packet ↔ ContextValue mapping
- Packet correlation
- Response routing

**RFC quan trọng nhất hiện nay.**

---

### 10.2 RFC 0042 — Transport Session & Channel Model ⭐⭐⭐⭐⭐

Session có nhưng chưa được define đầy đủ. Chuẩn hóa:

```
Connection → Session → Channel → Invocation
```

Ví dụ:
```
TCP → Session → Channel 1 = Governance, Channel 2 = File, Channel 3 = Join, Channel 4 = Recovery
```

Định nghĩa:
- Session lifecycle, timeout, rekey, resume
- Stream ID, Flow Control

---

### 10.3 RFC 0043 — Runtime Serialization Layer ⭐⭐⭐⭐☆

Freeze serialization pipeline:

```
Packet → CBOR → ContextValue → ContractInput
ContractResult → ContextValue → CBOR → Packet
```

---

### 10.4 RFC 0044 — EventBus & Event Model ⭐⭐⭐⭐☆

EventBus hiện khá sơ khai. Freeze:

```
Event → Subscribers → Audit → Metrics → History → Notification
```

Ví dụ: `ContractExecuted → Audit + Metrics + CLI + Remote + History`

Mọi thứ đều event-driven.

---

### 10.5 RFC 0045 — Distributed Execution Model ⭐⭐⭐⭐⭐

Runtime hiện chỉ chạy local. RFC này cho phép:

```
Node A → Runtime → Dispatch Contract → Node B → execute() → Result → Node A
```

Runtime local và remote dùng cùng ABI. Đây mới là điểm mạnh của ShellMap.

---

### 10.6 RFC 0046 — Mesh RPC Framework ⭐⭐⭐⭐☆

```
invoke() → Contract → Packet → Remote Runtime → Contract → Result
```

Giống actor model.

---

### 10.7 RFC 0047 — Scheduler & Async Execution ⭐⭐⭐⭐⭐

Scheduler hiện khá đơn giản. Bổ sung:

```
Immediate, Delayed, Cron, Dependency, Timeout, Retry, Future, Promise
```

PlanExecutor sẽ mạnh hơn rất nhiều.

---

### 10.8 RFC 0048 — Runtime Security Model ⭐⭐⭐⭐⭐

Freeze:
```
Capability → Service → Policy → Audit → Isolation
```

Ví dụ: JoinContract được phép dùng Crypto (YES), Filesystem (NO), Vault (NO), Network (YES).

Giống capability-based OS.

---

### 10.9 RFC 0049 — Mesh Storage Model ⭐⭐⭐⭐☆

Storage mới chỉ là service. Chuẩn hóa:

```
Contract → KV → SQLite → Replication → Snapshot → Recovery
```

---

### 10.10 RFC 0050 — Runtime Observability ⭐⭐⭐⭐☆

Freeze:
```
Metrics, Tracing, Audit, History, Health, Logs
```

Giống OpenTelemetry.

---

### 10.11 RFC 0051 — Contract SDK ⭐⭐⭐⭐⭐

Định nghĩa SDK cho plugin:
```
Contract → SDK → Runtime
```

Native, WASM, Python, Rust đều dùng chung SDK.

---

### 10.12 RFC 0052 — Runtime Plugin Model ⭐⭐⭐⭐☆

Không cần compile vào binary:
```
.so → Plugin Loader → ContractManager
```

---

### 10.13 RFC 0053 — Network Reliability Layer ⭐⭐⭐⭐⭐

Transport hiện chưa có:
```
ACK, Window, Duplicate detect, Fragment, Retransmit, Ordering
```

Đặc biệt cho UDP.

---

### 10.14 RFC 0054 — Distributed Transaction / Saga ⭐⭐⭐⭐⭐

PlanExecutor đang rất gần với Saga:

```
Contract A → Contract B → Contract C → Rollback
```

---

### 10.15 Thứ tự khuyến nghị

| RFC | Độ ưu tiên | Nên làm |
|-----|-----------|---------|
| RFC 0041 Runtime Bridge | ⭐⭐⭐⭐⭐ | Ngay bây giờ |
| RFC 0042 Session Model | ⭐⭐⭐⭐⭐ | Sau 0041 |
| RFC 0043 Serialization | ⭐⭐⭐⭐☆ | Sau Wiring |
| RFC 0044 Event Model | ⭐⭐⭐⭐☆ | Song song |
| RFC 0045 Distributed Execution | ⭐⭐⭐⭐⭐ | Sprint kế |
| RFC 0047 Scheduler | ⭐⭐⭐⭐⭐ | Sau Distributed |
| RFC 0048 Security Model | ⭐⭐⭐⭐⭐ | Sau Scheduler |
| RFC 0054 Saga | ⭐⭐⭐⭐⭐ | Khi mesh ổn định |

**Kiến nghị tổng thể:**

Đừng vội sang Sprint 37 để thêm tính năng mới. Hiện dự án đã có rất nhiều module mạnh nhưng còn thiếu "chất kết dính". Dành **2–3 RFC cuối để khóa kiến trúc** trước:

1. **RFC 0041 – Runtime Bridge** (Packet ↔ Runtime ↔ Contract).
2. **RFC 0042 – Session & Channel Model**.
3. **RFC 0043 – Serialization Layer**.

Sau ba RFC này, framework có kiến trúc gần như hoàn chỉnh, và việc phát triển các tính năng phân tán (distributed execution, mesh RPC, saga...) sẽ diễn ra trên một nền tảng ổn định thay vì phải thay đổi kiến trúc lõi liên tục.
