# Sprint 3 — Contract Runtime Layer

## Final Plan (Locked)

Agreed between @dotlinux26 and @D-O-T-Solutions on 2026-07-16.

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
