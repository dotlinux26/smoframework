# RFC 0035 — SMO Runtime Architecture (LOCKED SPEC)

**Status:** FROZEN — Architecture frozen for Sprint 36C.5 implementation  
**Date:** 2026-07-18  
**Authors:** @dotlinux26, @D-O-T-Solutions  
**Supersedes:** —  
**Extends:** RFC 0033 (Mesh Genesis), RFC 0034 (Bootstrap Protocol)

---

## 1. Motivation

SMO has evolved from a mesh+enrollment tool into a **Distributed Operating Platform**.  
Every operation — Join, Bootstrap, Governance, Recovery, File, Process, Vault, Workflow — must flow through a **single, unified Runtime pipeline** to avoid technical debt from per-feature handlers.

**Before (feature-first, debt-prone):**
```cpp
handle_join()
handle_bootstrap()
handle_governance()
handle_put()
handle_exec()
```

**After (runtime-first, unified):**
```
All Operations
      ↓
Runtime Kernel
      ↓
Dispatcher → Native Contract / WASM Contract
      ↓
Policy → Audit → Output
```

---

## 2. Core Principles (LOCKED)

| Principle | Description |
|-----------|-------------|
| **Single Pipeline** | Every intent/contract goes through one Runtime Kernel |
| **Kernel ≠ Engine** | Kernel = Validate→Resolve→Middleware→PlanExecutor→Dispatch→Collect→Aggregate→Audit→Return |
| **EventBus = Side Channel** | EventBus only for Audit/History/Metrics subscribers; NOT in execution path |
| **Dispatcher Agnostic** | Dispatcher doesn't know Native vs WASM vs Python — only ContractInterface |
| **OutputManager = Aggregator** | Not a formatter; collects, summarizes, then drills down |
| **Contract Interface** | Unified interface; Native/WASM/Python are implementations |
| **Deferred Storage** | EventBus first; persistence hooks in later |
| **Kernel Drives Scheduler** | Contracts return RetryLater; Kernel re-queues via Scheduler |
| **Execution Plan as IR** | Every request becomes an ExecutionPlan (DAG); Kernel executes DAG |
| **Middleware Pipeline** | 4 stages: BeforeResolve, BeforeDispatch, AfterDispatch, AfterCommit |
| **PlanResolver** | Separates plan resolution from execution |
| **PlanExecutor** | Executes DAG with parallel support, compensation, rollback |

---

## 3. Architecture Layers (LOCKED)

```
┌─────────────────────────────────────────────────────────────┐
│                        UI Layer                              │
│  CLI / TUI / Dashboard / REST Gateway                       │
└──────────────────────┬──────────────────────────────────────┘
                       │ Runtime.submit(RuntimeRequest)
┌──────────────────────▼──────────────────────────────────────┐
│                    Service Layer                             │
│  JoinContract · BootstrapContract · GovernanceContract      │
│  RecoveryContract · FileContract · ProcessContract · ...    │
└──────────────────────┬──────────────────────────────────────┘
                       │ Runtime.submit(request)
┌──────────────────────▼──────────────────────────────────────┐
│                     RUNTIME KERNEL                           │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ Validate → Resolve → Middleware(BeforeResolve)       │  │
│  │ PlanExecutor (DAG) → Middleware(BeforeDispatch)      │  │
│  │ Dispatch → Middleware(AfterDispatch)                 │  │
│  │ Collect → Aggregate → Middleware(AfterCommit) → Return│  │
│  └──────────────────────────────────────────────────────┘  │
│                          │                                   │
│          ┌───────────────┼───────────────┐                  │
│          ▼               ▼               ▼                  │
│    ┌─────────┐     ┌────────────┐   ┌───────────┐         │
│    │ Dispatcher │   │OutputManager │ │  EventBus  │        │
│    └─────────┘     └────────────┘   └───────────┘         │
└──────────────────────┬──────────────────────────────────────┘
                       │
         ┌─────────────┴─────────────┐
         ▼                           ▼
  ┌─────────────┐              ┌─────────────┐
  │ Native      │              │ WASM /      │
  │ Contracts   │              │ Future      │
  └─────────────┘              └─────────────┘
         │                           │
         └─────────────┬─────────────┘
                       ▼
              ┌────────────────┐
              │ Transport Layer│
              │ TCP/TLS/Gossip │
              └────────────────┘
                       │
         ┌─────────────┴─────────────┐
         ▼                           ▼
    ┌─────────┐                ┌─────────┐
    │ PKI /   │                │ Mesh /  │
    │ Gov /   │                │ State   │
    │ Recovery│                └─────────┘
    └─────────┘
```

---

## 4. Execution Model (36C.5) — LOCKED

### 4.1 Execution Plan = IR of Runtime

Every request becomes an **ExecutionPlan** — a DAG of steps:

```cpp
struct ExecutionPlan {
    std::string plan_id;
    ExecutionMode mode;      // Sequential / Parallel / Pipeline
    std::vector<Step> steps;
    std::string rollback_policy; // AllOrNothing / BestEffort / Compensating
    uint64_t total_timeout_ns;
};

struct Step {
    std::string step_id;
    std::string contract_id;
    std::vector<std::string> depends_on;  // DAG edges
    std::string input_template;           // JSON with {{var}} substitution
    std::string output_binding;           // context key for output
    std::string compensation_id;          // rollback step
    bool optional = false;                // failure doesn't abort plan
    uint64_t timeout_ns = 30s;
    int max_retries = 3;
};
```

**Every request** (Join, Bootstrap, Governance, Recovery, File, Process) becomes an ExecutionPlan.
CLI, REST, RPC, Scheduler, AI Agent — all submit `RuntimeRequest` → `PlanResolver` → `ExecutionPlan` → `PlanExecutor`.

### 4.2 PlanResolver

Separates plan resolution from execution:

```cpp
class PlanResolver {
    Result<ExecutionPlan> resolve(const std::string& plan_id);
    // Built-in resolvers for system contracts:
    // system.join, system.bootstrap, system.governance, system.recovery
};
```

### 4.3 PlanExecutor

Executes DAG with parallel support, compensation, rollback:

```cpp
class PlanExecutor {
    Result execute(PlanContext& ctx);
    // - Topological sort
    // - Parallel execution of independent steps
    // - Compensation/rollback on failure
    // - NextAction chaining
};
```

### 4.4 Middleware Pipeline (4 stages — LOCKED)

| Stage | Middlewares | Purpose |
|-------|-------------|---------|
| **BeforeResolve** | AuthMiddleware | Verify identity, cert |
| **BeforeDispatch** | PolicyMiddleware, TracingMiddleware | Capability check, tracing |
| **AfterDispatch** | MetricsMiddleware, TimeoutMiddleware | Metrics, deadline check |
| **AfterCommit** | AuditMiddleware | Emit audit event |

---

## 4.5 Key Types (LOCKED)

### RuntimeRequest / RuntimeResult

```cpp
struct RuntimeRequest {
    std::string request_id;       // UUID
    std::string mesh_id;
    std::string requester;        // node_id / "admin" / "system"
    std::string contract_id;      // or plan_id
    std::string opcode;           // human-readable action
    std::string payload;          // JSON/CBOR
    std::unordered_map<std::string, std::string> headers;
    uint64_t deadline_ns = 0;     // absolute deadline
    uint32_t flags = 0;           // ASYNC=1, NO_AUDIT=2, DRY_RUN=4

    enum Flag : uint32_t {
        ASYNC       = 1 << 0,
        NO_AUDIT    = 1 << 1,
        DRY_RUN     = 1 << 2,
        NO_RETRY    = 1 << 3,
        PRIORITY    = 1 << 4
    };
};

struct RuntimeResult {
    enum class Status { Success, Denied, Timeout, Error, Pending, Compensated };
    Status status = Status::Error;
    std::string request_id;
    std::string execution_id;
    std::string output;                 // JSON/CBOR
    std::vector<NextAction> next_actions;
    RuntimeError error;
    std::unordered_map<std::string, std::string> metrics;
    std::vector<std::string> events_emitted; // correlation IDs
    uint64_t elapsed_ns = 0;
};
```

### NextAction (LOCKED)

```cpp
struct NextAction {
    enum class Type {
        NextContract,     // chain to another contract
        Retry,            // retry with backoff
        EmitEvent,        // emit event to EventBus
        StoreContext,     // store value in execution context
        AbortPlan,        // abort current plan
        ForkPlan,         // fork sub-plan
        MergePlan,        // merge with another plan
        ScheduleRetry,    // schedule retry via Scheduler
        Compensate,       // trigger compensation
        Complete          // finish successfully
    };

    Type type = Type::Complete;
    std::string contract_id;           // for NextContract
    std::string payload;               // transformed payload
    uint64_t delay_ns = 0;             // for ScheduleRetry
    std::string event_type;            // for EmitEvent
    std::string event_payload;         // for EmitEvent
    std::string context_key;           // for StoreContext
    std::string context_value;         // for StoreContext
    std::string sub_plan_id;           // for ForkPlan/MergePlan
    std::string compensation_id;       // for Compensate
    std::unordered_map<std::string, std::string> metadata;

    static NextAction next_contract(const std::string& id, const std::string& payload = "") {
        return {Type::NextContract, id, payload};
    }
    static NextAction retry(uint64_t delay_ns = 1'000'000'000) {
        return {Type::Retry, "", "", delay_ns};
    }
    static NextAction emit_event(const std::string& event_type, const std::string& payload) {
        return {Type::EmitEvent, "", "", 0, event_type, payload};
    }
    static NextAction store_context(const std::string& key, const std::string& value) {
        return {Type::StoreContext, "", "", 0, "", "", key, value};
    }
    static NextAction abort() {
        return {Type::AbortPlan};
    }
    static NextAction fork(const std::string& sub_plan_id) {
        return {Type::ForkPlan, "", "", 0, "", "", "", "", sub_plan_id};
    }
    static NextAction complete() {
        return {Type::Complete};
    }
};
```

### ContextValue (Type-Safe Context)

```cpp
class ContextValue {
    std::variant<
        std::monostate,           // null
        bool,
        int64_t,
        double,
        std::string,
        Bytes,
        std::unordered_map<std::string, std::string>,  // JSON-like
        std::vector<std::string>                       // array
    > value_;
    
    // Type-safe get<T>(), to_string(), operator[]
};
```

### RuntimeError (Structured + Retryable)

```cpp
struct RuntimeError {
    enum class Category {
        Unknown,
        Validation,
        NotFound,
        Unauthorized,
        PolicyDenied,
        Timeout,
        Network,
        Resource,
        Internal,
        Contract,
        Compensation
    };

    Category category;
    int code;
    std::string message;
    bool retryable;
    bool fatal;

    static RuntimeError validation(const std::string& msg);
    static RuntimeError not_found(const std::string& msg);
    static RuntimeError timeout(const std::string& msg);
    static RuntimeError network(const std::string& msg);
    static RuntimeError internal(const std::string& msg);
    // ...
};
```

---

## 5. Architecture Components (LOCKED)

### 5.1 EventBus (Side Channel)
```cpp
class EventBus {
    void publish(const Event& event);
    Subscription subscribe(EventType type, Subscriber fn);
    // Subscribers: AuditService, HistoryStore, Metrics, Notification
    // Runtime Kernel does NOT publish for flow control
};
```

### 5.2 RuntimeKernel (Heart of Platform)
```cpp
class RuntimeKernel {
    Result<RuntimeResult> execute(const RuntimeRequest& req, RuntimeContext& ctx);
    // Pipeline: Validate → Resolve → Middleware(BeforeResolve) 
    // → PlanExecutor → Middleware(BeforeDispatch)
    // → Dispatcher → Middleware(AfterDispatch)
    // → Collect → Aggregate → Middleware(AfterCommit) → Return
};
```

### 5.2 Dispatcher (Contract-Agnostic, Dumb)
```cpp
class Dispatcher {
    void register_contract(ContractID id, std::unique_ptr<ContractInterface> impl);
    Result<ExecutionResult> dispatch(const ContractID& id,
                                      const ExecutionInput& input,
                                      RuntimeContext& ctx);
    // ONLY routes ContractID → ContractInterface::execute()
    // NO policy, NO audit, NO storage, NO retry, NO timeout
};
```

### 5.3 ContractInterface (Unified, Minimal)
```cpp
class ContractInterface {
public:
    virtual Result<ExecutionOutput> execute(const ExecutionInput& input,
                                             RuntimeContext& ctx) = 0;
    virtual const ContractMetadata& metadata() const = 0;
    virtual CapabilitySet required_capabilities() const = 0;
    // That's it. No onStart/onTick/onStop lifecycle hooks.
};

class NativeContract : public ContractInterface { ... };
class WasmContract   : public ContractInterface { ... };
// Future: PythonContract, GoContract, ...
```

### 5.4 OutputManager (Aggregator, NOT Formatter)
```cpp
class OutputManager {
    struct AggregatedOutput {
        uint64_t total = 0, success = 0, denied = 0, timeout = 0, offline = 0, error = 0;
        std::unordered_map<std::string, uint64_t> by_contract;
        std::unordered_map<std::string, uint64_t> by_node;
        std::vector<std::string> error_samples;

        std::string summary() const {
            return "Executed: " + std::to_string(total) +
                   " | Success: " + std::to_string(success) +
                   " | Denied: " + std::to_string(denied) +
                   " | Timeout: " + std::to_string(timeout) +
                   " | Offline: " + std::to_string(offline) +
                   " | Error: " + std::to_string(error);
        }
    };

    void add_result(const NodeResult& r);
    AggregatedOutput summarize() const;
    DetailedOutput drill_down(const DrillFilter& filter) const;
    void clear();
};
// UI/CLI sees summary first; --debug triggers drill_down()
```

### 5.6 RuntimeContext (Per-Execution Service Bundle)
```cpp
struct RuntimeContext {
    CryptoProvider* crypto = nullptr;
    Storage* storage = nullptr;
    Transport* transport = nullptr;
    PolicyEngine* policy = nullptr;
    Scheduler* scheduler = nullptr;
    Clock* clock = nullptr;
    Logger* logger = nullptr;

    // Type-safe context store
    std::unordered_map<std::string, ContextValue> context;

    uint64_t execution_id = 0;
    uint64_t deadline_ns = 0;
    CapabilitySet granted_caps;

    // Contract never creates services; only uses what Context provides
};
```

---

## 6. Audit vs History — Separate Concerns

```cpp
// AUDIT: Who, When, Why, Signature (legal/legal compliance)
struct AuditEvent {
    std::string audit_id;
    std::string actor;          // who
    uint64_t timestamp_ns;      // when
    std::string action;         // what
    std::string reason;         // why
    std::string signature;      // proof
};

// HISTORY: Request, Result, Duration, Status (debug/replay)
struct HistoryRecord {
    std::string request_id;
    RuntimeRequest request;     // full request
    RuntimeResult result;       // full result
    uint64_t duration_ns;       // how long
    std::string status;         // success/denied/timeout/error
};
// Separate stores, separate schemas, separate retention policies.
```

---

## 6. Native Contract Migration (Sprint 36D)

| Feature | Before | After (Native Contract) |
|---------|--------|-------------------------|
| Join | `handle_join()` | `JoinContract::execute()` |
| Bootstrap | `handle_bootstrap()` | `BootstrapContract::execute()` |
| Governance | `handle_governance()` | `GovernanceContract::execute()` |
| Recovery | `handle_recovery()` | `RecoveryContract::execute()` |
| File | `handle_put()` | `FileContract::execute()` |
| Process | `handle_exec()` | `ProcessContract::execute()` |

All contracts implement `ContractInterface` and are registered in `Dispatcher`.

---

## 7. Scheduler — Kernel-Driven (Sprint 36F)

```cpp
// Contract returns RetryLater
Result<ExecutionOutput> execute(input, ctx) {
    if (resource_busy) return RetryLater{.delay_ms = 100};
    return output;
}

// Kernel re-queues via Scheduler
auto res = dispatch(job);
if (res.is_retry()) {
    scheduler.enqueue(RetryJob{job, delay_ms});
    return Pending;
}
```

Scheduler is NEVER called directly from Contract.

---

## 7. Sprint Plan (CORRECTED)

| Sprint | Focus | Deliverables |
|--------|-------|--------------|
| **36C** | Runtime Skeleton | EventBus, RuntimeKernel, Dispatcher, ContractInterface, NativeContract, OutputManager, RuntimeContext, RuntimeRequest/Result |
| **36C.5** | **Execution Model** | RuntimeRequest/Result, ExecutionPlan/DAG, PlanResolver, PlanExecutor, Middleware (4 stages), ContextValue, NextAction, PlanExecutor |
| **36D** | Feature Migration | JoinContract, BootstrapContract, GovernanceContract, RecoveryContract, FileContract, ProcessContract |
| **36E** | EventBus + Audit + History | EventBus, AuditService (subscriber), HistoryStore (subscriber), Sqlite persistence |
| **36F** | Scheduler | Priority queues (realtime/high/normal/low), cron, deadline enforcement, cancellation, RetryEngine |
| **36G** | Transport + Gossip | TLS session, mTLS, Gossip protocol, Seed resolver, LAN discovery, Connection pooling |
| **36H** | WASM Contract Runtime | wasmtime/wasm3 embed, Host functions (FS/Process/Vault/Network/Crypto), Gas metering |

---

## 8. Consequences

### Positive
- **Zero refactor** when adding new operations
- **Single pipeline** for policy, audit, output, scheduling
- **Contract agnostic** — Native/WASM/Python all first-class
- **Observable** — every execution emits EventBus events (side channel)
- **Scalable** — OutputManager handles 1000+ node fan-out
- **Parallel execution** — DAG executor auto-detects independent steps
- **Compensation/rollback** — Built into plan execution

### Negative
- **Upfront cost** — Runtime Skeleton must exist before any feature works
- **Complexity** — More layers than direct handler calls
- **Learning curve** — Team must understand ContractInterface + Dispatcher + ExecutionPlan

### Migration
- Existing `handle_*` in CLI become thin wrappers calling `Runtime.execute(Contract)`
- No behavior change, just plumbing

---

## 8. Architecture FROZEN

**Runtime Skeleton + Execution Model architecture is FROZEN.**  
No feature work (Join, Bootstrap, Governance, Recovery, etc.) proceeds until Runtime Kernel + Dispatcher + ContractInterface + OutputManager + ExecutionPlan + Middleware + PlanExecutor are implemented and tested with a trivial contract.

---

## References

- [RFC 0033 — Mesh Genesis & Governance](../RFC/0033-mesh-genesis-governance.md)
- [RFC 0034 — Bootstrap Protocol](../RFC/0034-bootstrap-protocol.md)
- [Discussion 0036 — Role Model, Join Token, Bootstrap Plane](../docs/discussions/DISCUSSION_0036_JOIN_TOKEN_BOOTSTRAP.md)
- `core/runtime/` — Implementation