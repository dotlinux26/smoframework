# Phase 1 — PKI & Governance (Discussion 35) — ✅ COMPLETE

**Agreed between @dotlinux26 and @D-O-T-Solutions on 2026-07-17.**  
**Implementation completed: 2026-07-17.**

**Spec frozen:** [RFC 0033 — Mesh Genesis & Governance](../RFC/0033-mesh-genesis-governance.md)

---

## Sprint 35A — Root Session & Genesis ✅

**Mục tiêu:** Root từ ephemeral key → **Node** với role=ROOT, có store, API, FSM.  
Sinh Genesis Manifest + Recovery Package + Bootstrap Slots.

### Files tạo
- `core/genesis/genesis_manifest.hpp/.cpp` — GenesisManifest, DeploymentProfile, AuthorityRange, QuorumConfig, profile defaults, JSON serialization
- `core/genesis/bootstrap_slot.hpp/.cpp` — BootstrapSlot, SlotStatus, SlotRing (claim/fulfill/expire/revoke)
- `core/genesis/recovery_package.hpp/.cpp` — RecoveryPackage, EmergencyRecoveryToken
- `core/genesis/root_session.hpp/.cpp` — RootSession, RootSessionManager (start/validate/consume/expire)
- `core/genesis/genesis.hpp/.cpp` — GenesisWizard, GenesisStage, 2-stage flow (Stage 0 + Stage 1)
- `core/genesis/CMakeLists.txt` — Build target `smo_genesis`
- `core/mesh/mesh_state.hpp` — MeshState enum + transition validation
- `core/mesh/mesh_fsm.hpp/.cpp` — MeshFsm wrapper (12 transition rules + 5 timeouts)

### Files sửa
- `core/errors/error.hpp` — +ErrorCategory::Genesis (15) + SMO_ERR_GENESIS macro
- `core/errors/error_codes.md` — +10 GENESIS error codes (1400-1409)
- `core/CMakeLists.txt` — +add_subdirectory(genesis)
- `core/mesh/CMakeLists.txt` — +mesh_fsm.cpp

---

## Sprint 35B — Authority as Node (Role-based) ✅

**Mục tiêu:** Authority → **Role** gắn với node, bootstrap qua Slot.

### Files sửa
- `core/certificate/certificate.hpp` — +Recovery (6), deprecate Reader→Member, role_deprecate_reader(), Role_Max
- `core/authority/authority.hpp` — create_mesh_keys [[deprecated]], +BootstrapSignRequest + sign_bootstrap_csr()
- `cmd/smo-cli/intent_parser.hpp` — +IntentType::Genesis
- `cmd/smo-cli/intent_parser.cpp` — +genesis command
- `cmd/smo-cli/main.cpp` — +handle_genesis() (create/status/manifest), +genesis auto-complete
- `cmd/smo-cli/CMakeLists.txt` — +smo_genesis link
- `cmd/smo-admin/main.cpp` — +deprecation warning trong cmd_create_mesh()

---

## Sprint 35C — Governance Engine (Level A + Level B) ✅

**Mục tiêu:** GovernanceEngine → phân tách Membership vs Constitution.

### Files sửa
- `core/governance/governance.hpp` — +GovernanceTier (Membership/Constitution/Unanimous), +GovernanceAction (16 actions), +action_to_tier(), +default_quorum(), +MeshHealth + compute_health(), +ProposalState::Conflicted, +tier field, +error codes 812-813
- `core/governance/governance.cpp` — +default_quorum(), action_to_tier(), MeshHealth::to_display(), engine auto-assign tier+TTL
- `core/errors/error_codes.md` — +812 PROPOSAL_CONFLICT, 813 ACTION_NOT_ALLOWED
- `cmd/smo-cli/intent_parser.hpp/.cpp` — +Governance IntentType + command
- `cmd/smo-cli/main.cpp` — +handle_governance() (propose/list/status), +mesh health, +governance auto-complete

---

## Sprint 35D — Recovery & Revocation ✅

**Mục tiêu:** Soft Recovery, Hard Recovery, Revocation pipeline.

### Files tạo
- `core/recovery/recovery_engine.hpp/.cpp` — RecoveryEngine, RecoverySession, RecoveryMode (Soft/Hard), assess_mode(), start_soft/hard(), add_signature(), execute(), cancel()
- `core/recovery/crl.hpp` — CRL, CRLEntry, RevokeCertMsg, RevokeAckMsg
- `core/recovery/CMakeLists.txt` — Build integration

### Files sửa
- `core/errors/error.hpp` — +ErrorCategory::Recovery (16) + SMO_ERR_RECOVERY
- `core/errors/error_codes.md` — +Recovery codes 1500-1505
- `core/CMakeLists.txt` — +add_subdirectory(recovery)
- `core/opcode/opcode.h` — +4 opcodes REVOKE_CERT/EPOCH_INCREMENT/RECOVERY_SESSION/CRL_SYNC
- `cmd/smo-cli/intent_parser.hpp/.cpp` — +Recovery IntentType + command
- `cmd/smo-cli/main.cpp` — +handle_recovery() (restore/force/status), +recovery auto-complete

---

## Sprint 35 Summary

| Metric | Count |
|--------|-------|
| Files created | 15 source files + 2 standalone headers |
| Files modified | 18 files |
| New error codes | 18 (Genesis 10 + Governance 2 + Recovery 6) |
| New error categories | 2 (Genesis + Recovery) |
| New opcodes | 4 |
| Build targets | smo_genesis (static lib) |
| RFC spec | RFC 0033 — Mesh Genesis & Governance |

## Next: Sprint 36 — Role Model & Join Token

Reference: [DISCUSSION_0036_JOIN_TOKEN_BOOTSTRAP.md](discussions/DISCUSSION_0036_JOIN_TOKEN_BOOTSTRAP.md)

---

# Phase 2 — Bootstrap & Role Model (Discussion 36)

**Architecture frozen:** 2026-07-18.

**Spec:** [RFC 0034 — Bootstrap Protocol](../RFC/0034-bootstrap-protocol.md)  
**Discussion:** [DISCUSSION_0036_JOIN_TOKEN_BOOTSTRAP.md](discussions/DISCUSSION_0036_JOIN_TOKEN_BOOTSTRAP.md)

---

## Sprint 36A — Bootstrap Protocol ✅

**Mục tiêu:** Replace HTTP BootstrapService with opcode-based Bootstrap Protocol over TCP Transport. Wire CBOR encoding. PacketDispatcher.

### Files tạo
- `RFC/0034-bootstrap-protocol.md` — Bootstrap Protocol spec
- `core/bootstrap/cbor.hpp/.cpp` — Minimal CBOR encoder/decoder (~200 LOC)
- `core/bootstrap/bootstrap_snapshot.hpp/.cpp` — CBOR-serialized BootstrapSnapshot
- `core/bootstrap/bootstrap_protocol.hpp/.cpp` — BootstrapRequest/Response, `handle_bootstrap_request()`, `register_bootstrap_handler()`
- `core/network/packet_dispatcher.hpp/.cpp` — PacketDispatcher routing by opcode_id

### Files xóa
- `core/bootstrap/bootstrap_service.hpp/.cpp` — HTTP BootstrapService removed

### Files sửa
- `core/mesh/mesh_manager.hpp/.cpp` — Moved from `smo_mesh` to `smo_core` (fix circular dep)
- `core/errors/error.hpp` — +ErrorCategory::Bootstrap (17)
- `core/errors/error_codes.md` — +Bootstrap codes 1700-1703
- `cmd/smo-node/main.cpp` — Wired PacketDispatcher + Bootstrap handler (skeleton)

---

## Sprint 36B — Signature Join Token & Root Redesign ✅

**Mục tiêu:** Join Token chuyển từ HMAC sang chữ ký số. RootSession trở thành session-centric thay vì key-centric. SignerContext abstract class cho TPM/HSM/YubiKey.

### Kiến trúc chốt

```
Recovery Package
    ↓ passphrase + HashImpl + AeadImpl + SignerImpl
 unlock()
    ↓
 SoftwareSignerContext   ← hoặc TPM/HSM/YubiKey
    ↓
 RootSession { SessionPolicy, AuditSink }
    ↓
 execute(RootRequest{op, payload, reason})
    ↓ Policy → SignerContext → AuditSink → RootResult
    ↓
 destroy() → zeroize + invalidate handle + audit
```

### Files tạo
- `core/crypto/signer_context.hpp` — SignerContext abstract class + SoftwareSignerContext + SignerMetadata

### Files sửa
- `core/genesis/root_session.hpp/.cpp` — SignerContext (unique_ptr) thay vì raw key; execute() thay vì sign(); SessionPolicy; AuditSink; AuditEvent; destroy() invalidates handle
- `core/genesis/recovery_package.hpp/.cpp` — unlock() tạo SoftwareSignerContext, +version verify
- `core/genesis/genesis.hpp/.cpp` — run_stage_0 nhận unique_ptr<SignerContext>
- `core/enroll/join_token.hpp/.cpp` — validate_token() giờ verify signature qua SignerImpl
- `cmd/smo-cli/main.cpp` — Placeholder SignerContext
- `cmd/smo-admin/main.cpp` — generate-invite dùng RecoveryPackage → RootSession → execute(SignJoinToken)

### Open
- `cmd/smo-admin generate-invite` — ✅ Done (signature-based)

---

## Sprint 36C — Runtime Foundation ✅ COMPLETE

**Mục tiêu:** EventBus → RuntimeKernel → Dispatcher → ContractInterface → OutputManager → RuntimeContext.

### Files created
- `core/runtime/event_bus.hpp/.cpp` — Pub/Sub backbone
- `core/runtime/runtime_kernel.hpp/.cpp` — Pipeline: Validate→Resolve→Middleware→Plan→Dispatch→Collect→Aggregate→Audit→Return
- `core/runtime/dispatcher.hpp/.cpp` — Contract-agnostic dispatcher
- `core/runtime/contract_interface.hpp` — ContractInterface abstract + NativeContract base
- `core/runtime/output_manager.hpp/.cpp` — Aggregator (summary → drill-down)
- `core/runtime/runtime_context.hpp` — Per-execution context (deduplicated, ExecutionPlan as inner Plan)
- `core/runtime/runtime_types.hpp` — Single source of truth for all runtime types
- `core/runtime/CMakeLists.txt` — Build target `smo_runtime`

---

## Sprint 36C.5 — Execution Model ✅ COMPLETE

**Mục tiêu:** Execution Plan = IR of Runtime. Mọi request đều qua ExecutionPlan (DAG). Middleware 4 stages. PlanResolver/PlanExecutor tách biệt.

### Files created
- `core/runtime/runtime_types.hpp` — RuntimeRequest/Result, ExecutionPlan/DAG, Step, RuntimeError, ContextValue, NextAction, PlanContext, ExecutionMiddleware, PlanResolver
- `core/runtime/plan_executor.hpp/.cpp` — DAG executor with parallel support, compensation, rollback
- `core/runtime/middleware.hpp/.cpp` — 4-stage middleware pipeline + 6 built-in middlewares (Auth, Policy, Tracing, Timeout, Metrics, Audit)
- `core/runtime/dispatcher.hpp/.cpp` — Minimal contract-agnostic Dispatcher

### Key decisions executed
- **Single Source of Truth:** `runtime_types.hpp` — all duplicates removed
- **RuntimeContext::Plan = ExecutionPlan** — no type conversion needed
- **RuntimeError → Error** via `operator Error()` for Result<T> compatibility
- **SMO_TRY macro** for pipeline (Result's operator= is deleted)
- **Scheduler excluded** from runtime build (not yet needed)

---

## Sprint 36C.6 — Runtime API Freeze (NEXT)

**Mục tiêu:** Freeze Contract ABI, Runtime Services, State Machine, NextAction, and Lifecycle before any Native Contract is built.

### RFCs (all APPROVED)

| RFC | Title | Status |
|-----|-------|--------|
| 0036 | Contract ABI Freeze | ✅ APPROVED — ContextValue arguments, partial-const RuntimeContext, strong metrics |
| 0037 | Runtime Service Injection | ✅ APPROVED — ClockService + RandomService, no reinterpret_cast |
| 0038 | Execution State Machine | ✅ APPROVED — 12-state + Compensating, no TimedOut terminal |
| 0039 | NextAction Model | ✅ APPROVED — ActionDispatchContract/Message, no ActionComplete |
| 0040 | Contract Lifecycle + Metadata + Capabilities | ✅ APPROVED — Registry/Manager split, no Executing lifecycle |

### Implementation plan (after approval)
1. Refactor `contract_interface.hpp` per RFC 0036
2. Refactor `runtime_context.hpp` per RFC 0037
3. Update `StepStatus` / `PlanContext` per RFC 0038
4. Replace `NextAction` enum+struct with variant per RFC 0039
5. Add `ContractRegistry`, `ContractLifecycle` per RFC 0040
6. Add 30-40 runtime tests: DAG topo sort, cycle detection, parallel exec, retry, compensation, middleware order, NextAction chaining, context binding, dispatcher lookup, EventBus emission
7. Full project build + verify

---

## Sprint 36D — Feature Migration to Native Contracts

Migrate existing features to Native Contracts:
- JoinContract
- BootstrapContract
- GovernanceContract
- RecoveryContract
- FileContract
- ProcessContract

---

## Sprint 36E — Audit + History + SQLite

- SqliteAuditSubscriber
- HistoryStore
- EventStore
- Dashboard query API

---

## Sprint 36F — Scheduler

- Priority queues (realtime/high/normal/low)
- Cron-style recurring jobs
- Deadline enforcement + cancellation

---

## Sprint 36G — Transport + Gossip

- TLS session layer + mTLS
- Gossip protocol (epidemic sync)
- Seed resolver + LAN discovery
- Connection pooling + keepalive

---

## Sprint 36H — WASM Contract Runtime

- wasmtime/wasm3 embed
- Host functions: FS, Process, Vault, Network, Crypto
- Gas metering / resource limits

---

## Sprint Roadmap

| Sprint | Focus | Status |
|--------|-------|--------|
| **35A–D** | PKI & Governance | ✅ COMPLETE |
| **36A** | Bootstrap Protocol | ✅ COMPLETE |
| **36B** | Signature Join Token & Root Redesign | ✅ COMPLETE |
| **36C** | Runtime Skeleton | ✅ COMPLETE |
| **36C.5** | Execution Model (DAG, PlanExecutor, Middleware) | ✅ COMPLETE |
| **36C.6** | **Runtime API Freeze (5 RFCs)** | **✅ COMPLETE (spec) — 9.7/10** |
| **36D.1** | JoinContract | ✅ COMPLETE |
| **36D.2** | BootstrapContract | ✅ COMPLETE |
| **36D.3** | GovernanceContract | ✅ COMPLETE |
| **36D.4** | RecoveryContract | ✅ COMPLETE |
| **36D.5** | FileContract / ProcessContract | ✅ COMPLETE |
| **36E** | Audit + History + SQLite | ⏳ |
| **36F** | Scheduler (priority/cron/deadline) | ⏳ |
| **36G** | Transport + Gossip | ⏳ |
| **36H** | WASM Contract Runtime | ⏳ |

---

## References

- [RFC 0033 — Mesh Genesis & Governance](../RFC/0033-mesh-genesis-governance.md)
- [RFC 0034 — Bootstrap Protocol](../RFC/0034-bootstrap-protocol.md)
- [RFC 0035 — Runtime Architecture](../RFC/0035-runtime-architecture.md)
- [RFC 0036 — Contract ABI Freeze](../RFC/0036-contract-abi-freeze.md) (DRAFT)
- [RFC 0037 — Runtime Service Injection](../RFC/0037-runtime-service-injection.md) (DRAFT)
- [RFC 0038 — Execution State Machine](../RFC/0038-execution-state-machine.md) (DRAFT)
- [RFC 0039 — NextAction Model](../RFC/0039-nextaction-model.md) (DRAFT)
- [RFC 0040 — Contract Lifecycle + Metadata + Capabilities](../RFC/0040-contract-lifecycle-metadata-capabilities.md) (DRAFT)