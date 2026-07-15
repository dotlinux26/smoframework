# Sprint 3 — Contract Runtime Layer

## Final Plan (Locked)

Agreed between @dotlinux26 and @D-O-T-Solutions on 2026-07-16.

---

## Sprint 3.1 — Stabilization (2026-07-16)

### Scope
Fix E2E test failures, implement CSR signing, stabilize CLI, create `core/network/` skeleton.

### Completed

| Task | Status |
|------|--------|
| Fix `smo-admin sign` — placeholder → real CSR signing | ✅ |
| Fix CLI `--dry-run` without `--opcode` | ✅ |
| Fix E2E test expected strings (scope, session, selection) | ✅ |
| Create `core/network/` skeleton (tcp, udp, bootstrap, transport) | ✅ |
| Update docs (PLAN.md, ARCHITECTURE.md) | ✅ |

### Issues Fixed

| # | Issue | Root Cause | Fix |
|---|-------|-----------|-----|
| 1 | `smo-admin sign` no-op | `cmd/smo-admin/main.cpp` was `return 0` placeholder | Implemented JSON CSR reader + certificate writer |
| 2 | `smo exec --dry-run` requires `--opcode` | `cmd/smo-cli/main.cpp` L250 hard error | Allow `--dry-run` without opcode → selection-only mode |
| 3 | Step 9 expected "session" | Output is "Session opened" | Updated expected string |
| 4 | Step 10 scope test | Ping didn't include scope in output | Added scope field to ping JSON response |
| 5 | Docker port conflict | Port 7777 used by other services | Changed node-a host port to 27777 |
| 6 | blake3 SSE2 on ARM | Unconditional SSE/AVX sources | Conditional compilation per arch |

### Remaining (Sprint 4)

- `core/network/` implementation (TCP/UDP transport, bootstrap, heartbeat)
- NAT traversal (STUN/ICE/TURN)
- SWIM-inspired gossip membership
- Real peer table (not local-only discovery)

---

## Sprint 4 — Network Layer (2026-07-16)

### Completed

| Task | Status |
|------|--------|
| 4.1 PeerStore (SQLite) — persistent peer cache | ✅ |
| 4.2 Bootstrap integration — real seed connect | ✅ |
| 4.3 UDP Transport + HeartbeatService | ✅ |
| 4.4 MembershipSync — typed event bus | ✅ |
| 4.5 GossipEngine — SWIM epidemic membership | ✅ |
| 4.6 CLI + Selector wiring — `exec`/`select` via Selector | ✅ |

### Sprint 4 Summary

| Component | Files | Purpose |
|-----------|-------|---------|
| PeerStore | `core/discovery/peer_store.{hpp,cpp}` | SQLite persistent peer cache, filtered queries, event log |
| Bootstrap | `core/discovery/discovery.cpp::Bootstrap::find_seed` | HELLO/WELCOME/DISCOVER/NODE_INFO handshake |
| UDP Transport | `core/network/udp/udp_transport.{hpp,cpp}` | Connectionless UDP sessions, listener, connect |
| HeartbeatService | `core/network/udp/heartbeat_service.{hpp,cpp}` | PING/PONG, RTT, HealthMonitor integration |
| MembershipSync | `core/network/sync/membership_sync.{hpp,cpp}` | Typed event bus: PeerAdded/Removed/Updated/Renamed |
| GossipEngine | `core/discovery/gossip.{hpp,cpp}` | SWIM epidemic membership sync, fanout |
| CLI Selector | `cmd/smo-cli/main.cpp::cmd_exec` | `exec --name/--role/--where --dry-run` → Selector → NodeSet |
| Daemon | `cmd/smo-node/main.cpp` | Bootstrap + MembershipTable + DiscoveryEngine + Heartbeat + Gossip |

### Architecture Update

Network Layer inserted between Connectivity and Session:
```
CONNECTIVITY (STUN/ICE) → NETWORK (Bootstrap/Sync/Heartbeat/Gossip/PeerStore)
                                          ↓
                              MembershipTable + PeerRecord
                                          ↓
                              Selector → NodeSet → Runtime Dispatch
```

---

### Phase 5 — Discovery (Completion)

| Task | File |
|------|------|
| handle_ping/handle_pong from no-op → real response | core/discovery/discovery.cpp |
| Gossip piggyback membership | core/discovery/gossip.hpp/.cpp (done) |
| Seed priority fallback | core/discovery/discovery.cpp |

---

### Phase 6 — Tests

| Test | Scope |
|------|-------|
| Parser round-trip | JSON → AST → JSON |
| SMIR lowering | AST → SMIR correctness |
| Semantic Validator | ABI hash mismatch, missing capability |
| Planner | Node selection logic |
| Builder → DAG | DAG structure, dependency edges |
| Final Validator | Cycle rejection, max depth overflow |
| Compiler integration | Intent → DAG (full pipeline) |
| Executor dispatch | Kernel contracts via Runtime::execute() |
| Discovery | Ping/pong, gossip propagation |
| Bootstrap integration | Seed connect → peer table |
| Heartbeat + RTT | PING/PONG → RTT update |
| Gossip propagation | Event fanout → remote membership update |
| Selector dry-run | `smo exec --name X --dry-run` → NodeSet |

---

### Phase 7 — Network Layer Hardening (Post-Sprint 4)

| Task | Scope |
|------|-------|
| STUN client (RFC 8489) | `transport/stun/` |
| ICE candidate gathering (RFC 8445) | `transport/ice/` |
| UDP hole punch (NAT traversal) | `transport/nat/` |
| TURN relay (RFC 8656) | `transport/relay/` |
| PeerStore vacuum/retention | GC old peer_events |
| Gossip compression | Delta sync for large meshes |

---

### Phase 8 — NAT Traversal (Tier 2)

| Task | Scope |
|------|-------|
| STUN client implementation | `transport/stun/client.cpp` |
| ICE candidate gathering | `transport/ice/gatherer.cpp` |
| UDP hole punch | `transport/nat/punch.cpp` |
| TURN relay | `transport/relay/turn.cpp` |

---

### Phase 9 — Mesh Governance & Recovery

| Task | Scope |
|------|-------|
| Mesh manifest signing | `cmd/smo-admin` |
| Authority rotation | `smo mesh authority add/revoke` |
| Epoch increment | `smo mesh epoch increment` |
| Recovery package | `smo mesh recover` |

---

### Changes from Original Plan

| Old Plan | Final Plan |
|----------|-----------|
| 4 RFCs (0025-0028) | 2 RFCs (0025-0026) |
| JSON → Parser → DAG | JSON → AST → SMIR → Planner → DAG |
| Validator 1 pass | Validator 2 passes (Semantic + Final) |
| Kernel contracts Sprint 4 | Kernel contracts Sprint 3 |
| No Contract ABI | Contract ABI + ABI Hash is first-class |
| No Runtime::execute() | Single `Runtime::execute(ContractID, ExecutionContext)` |
| Kernel may hardcode opcodes | **Polymorphic registration, zero hardcode** |
| Discovery MVP only | **Full Network Layer (Bootstrap/Sync/Heartbeat/Gossip/PeerStore)** |
| Single-node framework | **Distributed mesh runtime with Selection Engine** |

---

### Key Interfaces

```cpp
// The single runtime entry point
ExecutionResult Runtime::execute(const ContractID&, const ExecutionContext&);

// Contract ABI
struct ContractABI {
    uint32_t abi_version;
    Schema input_schema;
    Schema output_schema;
    CapabilityMask capability_mask;
    std::vector<OpcodeID> opcode_dependencies;
    Hash256 abi_hash;       // BLAKE3(canonical_abi_json)
    Hash256 semantic_hash;  // BLAKE3(abi_hash + contract_json)
    Version min_runtime_version;
    Version max_runtime_version;
};

// Compiler pipeline
struct Compiler {
    Result<ExecutionGraph> compile(const Intent&, const ContractDefinition&);
};

// Executor (ignorant of contract category)
struct Executor {
    Result<ExecutionResult> execute(const ExecutionGraph&, const Session&);
};

// Selection Engine
namespace smo::select {
    Result<NodeSet> select(const MembershipTable&, const SelectQuery&);
}

// PeerStore
class PeerStore {
    Result<void> open(std::string_view path);
    Result<void> upsert(const PeerRecord&);
    Result<PeerRecord> lookup(const NodeID&);
    Result<PeerRecord> lookup_by_name(std::string_view);
    Result<std::vector<PeerRecord>> peers_by_role(Role);
    Result<std::vector<PeerRecord>> peers_by_tag(std::string_view);
    Result<void> sync_from_membership(const MembershipTable&);
    Result<void> sync_to_membership(MembershipTable&) const;
};
```

---

### Phase 1 — RFCs

| RFC | Title | Content |
|-----|-------|---------|
| RFC 0025 | Contract Runtime Architecture | Compiler pipeline (6-stage + SMIR), Executor interface, 3-tier contract (Kernel/Native/Mesh), Runtime::execute() is the single interface |
| RFC 0026 | Contract ABI Specification | ABI format (Input Schema, Output Schema, Capability Mask, Opcode Dependencies, Min/Max Runtime Version), ABI Hash (BLAKE3), Semantic Hash, ContractID → ABI Hash → DAG Hash linkage |

---

### Phase 2 — Compiler Pipeline

```
JSON Contract / DSL / YAML / AI-gen
          ↓
     ┌──────────┐
     │  Parser   │  Parse ContractDefinition → AST
     └──────────┘
          ↓
     ┌──────────┐
     │    AST    │  Contract AST (opcodes, params, edges, conditions)
     └──────────┘
          ↓
     ┌──────────┐
     │   SMIR    │  SMO Intermediate Representation — canonical IR
     └──────────┘
          ↓
     ┌──────────┐
     │ Semantic  │  ABI Hash match, capability req, opcode dep check
     │ Validator │  (Pass 1)
     └──────────┘
          ↓
     ┌──────────┐
     │  Planner  │  Target node selection, shard mapping
     └──────────┘
          ↓
     ┌──────────┐
     │  Builder  │  SMIR → ExecutionGraph (DAG)
     └──────────┘
          ↓
     ┌──────────┐
     │ Optimizer │  Prune nodes, merge reads, constant folding
     └──────────┘
          ↓
     ┌──────────┐
     │  Final    │  Acyclic, max depth, node reachability
     │ Validator │  (Pass 2)
     └──────────┘
          ↓
     ExecutionGraph (DAG)
```

**New files:**

| File | Content |
|------|---------|
| `compiler/ast/ast.hpp` | Contract AST node types |
| `compiler/ast/ast.cpp` | AST builder from ParsedIntent |
| `compiler/smir/smir.hpp` | SMIR opcodes, operands, basic blocks |
| `compiler/smir/smir.cpp` | AST → SMIR lowering |
| `compiler/parser/parser.hpp/.cpp` | JSON → AST (real implementation) |
| `compiler/validator/semantic.hpp/.cpp` | Pass 1: Semantic validation |
| `compiler/validator/final.hpp/.cpp` | Pass 2: DAG structural validation |
| `compiler/planner/planner.hpp/.cpp` | Target selection |
| `compiler/graph/builder.hpp/.cpp` | SMIR → ExecutionGraph |
| `compiler/optimizer/optimizer.hpp/.cpp` | DAG optimization passes |
| `compiler/compiler.cpp` | Concrete Compiler |

---

### Phase 3 — Executor & Runtime

| File | Content |
|------|---------|
| `runtime/executor/executor.hpp/.cpp` | resolve(ContractID) → DAG (cache hit) → execute(DAG, Session) |
| `runtime/sandbox/sandbox.hpp/.cpp` | Seccomp/namespace isolation per task node |
| `runtime/workerpool/workerpool.hpp/.cpp` | Thread pool, parallel task dispatch |
| `runtime/runtime.hpp/.cpp` | `ExecutionResult execute(const ContractID&, const ExecutionContext&)` |

**Principle:** Runtime does NOT know opcodes. No `if(op == PING)`.

---

### Phase 4 — Kernel Contracts

Registered polymorphically:

```cpp
registry.register_kernel("ping",             KernelPingContract{});
registry.register_kernel("whoami",           KernelWhoamiContract{});
registry.register_kernel("session_open",     KernelSessionOpenContract{});
registry.register_kernel("session_close",    KernelSessionCloseContract{});
registry.register_kernel("discover",         KernelDiscoverContract{});
registry.register_kernel("node.info",        KernelNodeInfoContract{});
registry.register_kernel("identity.rotate",  KernelIdentityRotateContract{});
```

All implement `Contract` interface. Executor calls `contract->execute()`, unaware of category.

---

### Phase 5 — Discovery (Completion)

| Task | File |
|------|------|
| handle_ping/handle_pong from no-op → real response | core/discovery/discovery.cpp |
| Gossip piggyback membership | core/discovery/gossip.hpp/.cpp (new) |
| Seed priority fallback | core/discovery/discovery.cpp |

---

### Phase 6 — Tests

| Test | Scope |
|------|-------|
| Parser round-trip | JSON → AST → JSON |
| SMIR lowering | AST → SMIR correctness |
| Semantic Validator | ABI hash mismatch, missing capability |
| Planner | Node selection logic |
| Builder → DAG | DAG structure, dependency edges |
| Final Validator | Cycle rejection, max depth overflow |
| Compiler integration | Intent → DAG (full pipeline) |
| Executor dispatch | Kernel contracts via Runtime::execute() |
| Discovery | Ping/pong, gossip propagation |

---

### Changes from Original Plan

| Old Plan | Final Plan |
|----------|-----------|
| 4 RFCs (0025-0028) | 2 RFCs (0025-0026) |
| JSON → Parser → DAG | JSON → AST → SMIR → Planner → DAG |
| Validator 1 pass | Validator 2 passes (Semantic + Final) |
| Kernel contracts Sprint 4 | Kernel contracts Sprint 3 |
| No Contract ABI | Contract ABI + ABI Hash is first-class |
| No Runtime::execute() | Single `Runtime::execute(ContractID, ExecutionContext)` |
| Kernel may hardcode opcodes | **Polymorphic registration, zero hardcode** |

---

### Key Interfaces

```cpp
// The single runtime entry point
ExecutionResult Runtime::execute(const ContractID&, const ExecutionContext&);

// Contract ABI
struct ContractABI {
    uint32_t abi_version;
    Schema input_schema;
    Schema output_schema;
    CapabilityMask capability_mask;
    std::vector<OpcodeID> opcode_dependencies;
    Hash256 abi_hash;       // BLAKE3(canonical_abi_json)
    Hash256 semantic_hash;  // BLAKE3(abi_hash + contract_json)
    Version min_runtime_version;
    Version max_runtime_version;
};

// Compiler pipeline
struct Compiler {
    Result<ExecutionGraph> compile(const Intent&, const ContractDefinition&);
};

// Executor (ignorant of contract category)
struct Executor {
    Result<ExecutionResult> execute(const ExecutionGraph&, const Session&);
};
```
