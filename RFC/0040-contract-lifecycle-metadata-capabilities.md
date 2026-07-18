# RFC 0040 — Contract Lifecycle + Metadata + Capabilities

**Status:** APPROVED — Conditions resolved (Registry/Manager split, removed Executing lifecycle, removed duplicate capability fields)  
**Date:** 2026-07-18  
**Authors:** @dotlinux26, @D-O-T-Solutions  
**Supersedes:** RFC 0025 (Contract Runtime), RFC 0030 (Native Contracts)  
**Extends:** RFC 0035 (Runtime Architecture), RFC 0036 (Contract ABI)

---

## 1. Motivation

RFC 0036 froze the **Contract ABI** (the `execute()` signature), but two pieces remain unspecified:

1. **Lifecycle** — When is `initialize()` called? When is `shutdown()` called? What about reload?
2. **Metadata + Capabilities** — How does the Registry discover contracts? How does PolicyEngine know what a contract needs?

Without these, Sprint 36D will produce contracts with inconsistent lifecycle handling and ad-hoc capability checks.

**This RFC freezes:**
- Contract lifecycle states and transitions
- Metadata schema (what every `ContractMetadata` must contain)
- Capability model (how contracts declare + how kernel enforces)
- Registry (how contracts are discovered, loaded, and versioned)

---

## 2. Contract Lifecycle

### 2.1 Contract Lifecycle (ContractManager)

Lifecycle is managed by **ContractManager**, not Registry.  
Registry handles only metadata/lookup/discovery.

```
                    ┌────────────────────┐
                    │    Registered      │  ← discovered by Registry
                    └────────┬───────────┘
                             │ load()
                             ▼
                    ┌────────────────────┐
                    │      Loaded        │  ← binary loaded, metadata read
                    └────────┬───────────┘
                             │ initialize()
                             ▼
                    ┌────────────────────┐
                    │   Initialized      │  ← resources allocated, ready to execute
                    └────────┬───────────┘
                             │ (accepting execute() calls)
                             ▼
                    ┌────────────────────┐
                    │      Ready         │  ← accepting executions
                    └────────┬───────────┘
                             │ shutdown()
                             ▼
                    ┌────────────────────┐
                    │       Idle         │  ← resources released, can be garbage collected
                    └────────┬───────────┘
                             │ unload()
                             ▼
                    ┌────────────────────┐
                    │     Unloaded       │  ← removed from registry
                    └────────────────────┘
```

**Note:** `Executing` is NOT a lifecycle state. Lifecycle is about the **contract** (is it loaded? initialized? ready?). Execution is about the **invocation** (a single `execute()` call). A contract in `Ready` state can handle 100 concurrent executions — lifecycle state should not change per invocation.

### 2.2 States

| State | Description | Transitions |
|-------|-------------|-------------|
| `Registered` | Metadata known in Registry, binary not loaded | → Loaded |
| `Loaded` | Binary loaded into memory, `metadata()` readable | → Initialized, → Unloaded |
| `Initialized` | `initialize()` called, resources allocated, not yet accepting calls | → Ready, → Idle |
| `Ready` | Accepting `execute()` calls (may be concurrent) | → Idle |
| `Idle` | `shutdown()` called, resources freed, can be GC'd or reloaded | → Loaded, → Unloaded |
| `Unloaded` | Removed from Registry, binary unloaded | *(terminal)* |

### 2.3 Errors / Faults

| State | Meaning |
|-------|---------|
| `LoadFailed` | Binary failed to load (invalid WASM, missing symbol) |
| `InitFailed` | `initialize()` returned error |
| `InitTimeout` | `initialize()` exceeded budget |
| `CrashLoop` | Contract crashed on consecutive `execute()` calls |

Contracts in fault states are quarantined by the Registry and may be reloaded after a backoff.

### 2.4 C++ Definition

```cpp
enum class ContractLifecycleState : uint8_t {
    // Normal states
    Registered   = 0,
    Loaded       = 1,
    Initialized  = 2,
    Ready        = 3,
    Idle         = 4,

    // Terminal
    Unloaded     = 10,

    // Fault
    LoadFailed   = 20,
    InitFailed   = 21,
    InitTimeout  = 22,
    CrashLoop    = 23,
};

struct ContractLifecycle {
    ContractLifecycleState state = ContractLifecycleState::Registered;
    std::string error_message;
    uint64_t loaded_at_ns = 0;
    uint64_t initialized_at_ns = 0;
    uint64_t last_execution_at_ns = 0;
    uint64_t total_executions = 0;
    uint64_t failed_executions = 0;
    uint64_t crash_count = 0;               // consecutive crashes
    uint64_t next_retry_at_ns = 0;           // for crash loop backoff
};
```

---

## 3. ContractMetadata (Frozen Schema)

```cpp
struct ContractMetadata {
    // ── Identity ─────────────────────────────────────────────────────
    std::string id;                             // unique, e.g. "system.join"
    std::string name;                           // human-readable
    std::string version;                        // semver, e.g. "1.2.3"
    uint32_t api_version = 1;                   // ABI version (RFC 0036)

    // ── Authorship ───────────────────────────────────────────────────
    std::string author;
    std::string description;
    std::string repository;                     // optional: source repo URL
    std::string documentation;                  // optional: docs URL

    // ── Dependencies ─────────────────────────────────────────────────
    std::vector<std::string> dependencies;       // contract IDs this contract depends on
    std::vector<std::string> optional_deps;      // soft dependencies

    // ── Capabilities ─────────────────────────────────────────────────
    ContractCapabilities required_capabilities;  // what this contract needs
    ContractCapabilities optional_capabilities;  // nice-to-have

    // ── Execution Limits ─────────────────────────────────────────────
    uint64_t max_execution_time_ns = 30'000'000'000;  // 30s default
    uint64_t max_memory_bytes = 64 * 1024 * 1024;     // 64MB default

    // ── Registration ────────────────────────────────────────────────
    std::vector<std::string> tags;               // e.g. ["system", "governance"]
    std::vector<std::string> provides;           // e.g. ["join", "leave"]
    std::string entry_point;                     // contract_id for dispatcher

    // ── Lifecycle Hooks ──────────────────────────────────────────────
    bool has_initialize = false;                 // contract implements initialize()
    bool has_shutdown = false;                   // contract implements shutdown()
    bool has_validate = false;                   // contract implements validate()

    // ── Health ───────────────────────────────────────────────────────
    ContractLifecycle lifecycle;
};
```

---

## 4. ContractRegistry + ContractManager

**Separation of concerns:**
- `ContractRegistry` — metadata, lookup, discovery ONLY
- `ContractManager` — lifecycle (load/init/shutdown/unload), fault handling

### 4.1 ContractRegistry (Metadata + Lookup + Discovery)

```cpp
class ContractRegistry {
public:
    virtual ~ContractRegistry() = default;

    // ── Registration ─────────────────────────────────────────────────
    virtual Result<void> register_contract(
        std::unique_ptr<ContractInterface> contract) = 0;
    virtual Result<void> unregister_contract(const std::string& id) = 0;

    // ── Lookup ───────────────────────────────────────────────────────
    virtual ContractInterface* get_contract(const std::string& id) = 0;
    virtual const ContractMetadata* get_metadata(const std::string& id) const = 0;
    virtual bool has_contract(const std::string& id) const = 0;
    virtual std::vector<std::string> list_contracts() const = 0;

    // ── Discovery ────────────────────────────────────────────────────
    virtual std::vector<ContractMetadata> discover(
        const std::string& tag = "") const = 0;
    virtual std::vector<ContractMetadata> discover_by_capability(
        ContractCapability cap) const = 0;
};
```

### 4.2 ContractManager (Lifecycle + Fault Handling)

```cpp
class ContractManager {
public:
    virtual ~ContractManager() = default;

    // ── Lifecycle ────────────────────────────────────────────────────
    virtual Result<void> load(const std::string& id) = 0;
    virtual Result<void> initialize(const std::string& id,
                                     const ContractConfig& config) = 0;
    virtual Result<void> shutdown(const std::string& id) = 0;
    virtual Result<void> unload(const std::string& id) = 0;

    // ── State ────────────────────────────────────────────────────────
    virtual ContractLifecycleState get_state(const std::string& id) const = 0;
    virtual bool is_ready(const std::string& id) const = 0;
    virtual bool is_faulted(const std::string& id) const = 0;

    // ── Health ───────────────────────────────────────────────────────
    virtual std::vector<std::string> get_faulted_contracts() const = 0;
    virtual Result<void> recover(const std::string& id) = 0;  // retry load/init
};
```

### 4.3 Integration

```cpp
class RuntimeKernel {
    ContractRegistry& registry_;   // lookup + discovery
    ContractManager&  manager_;    // lifecycle management
    Dispatcher&       dispatcher_; // execution routing

    Result<void> ensure_contract_ready(const std::string& id) {
        if (!registry_.has_contract(id))
            return static_cast<Error>(RuntimeError::not_found("contract: " + id));

        auto state = manager_.get_state(id);
        if (state == ContractLifecycleState::Ready)
            return {};

        // Auto-load if registered but not loaded
        if (state == ContractLifecycleState::Registered) {
            SMO_TRY(manager_.load(id));
            SMO_TRY(manager_.initialize(id, default_config_for(id)));
        }

        if (manager_.is_faulted(id))
            return static_cast<Error>(RuntimeError::internal("contract faulted: " + id));

        return {};  // still initializing — caller should retry
    }
};
```

---

## 5. Capability Enforcement

### 5.1 Declaration (by Contract)

```cpp
// In JoinContract metadata
ContractMetadata JoinContract::metadata() const {
    return {
        .id = "system.join",
        .name = "Join Contract",
        .version = "1.0.0",
        .api_version = 1,
        .required_capabilities = ContractCapabilities{}.set(Capability::Crypto)
                                                       .set(Capability::Network),
        .max_execution_time_ns = 10'000'000_000,  // 10s
        .provides = {"join", "leave"},
        .has_initialize = false,
        // ...
    };
}
```

### 5.2 Enforcement (by Kernel)

```cpp
class PolicyEngine {
public:
    // Called before Dispatcher::execute()
    Result<void> check_execution_allowed(
        const std::string& requester,
        const ContractMetadata& meta,
        RuntimeContext& ctx)
    {
        // 1. Does requester have the capabilities this contract needs?
        auto requester_caps = get_requester_capabilities(requester);
        auto missing = meta.required_capabilities & ~requester_caps;
        if (missing.any()) {
            return Result<void>(static_cast<Error>(
                RuntimeError::policy_denied(
                    "missing capabilities: " + caps_to_string(missing))));
        }

        // 2. Does execution context provide the required services?
        auto& granted = ctx.services->granted_caps;
        auto missing_services = meta.required_capabilities & ~granted;
        if (missing_services.any()) {
            return Result<void>(static_cast<Error>(
                RuntimeError::internal(
                    "runtime missing services for: " + caps_to_string(missing_services))));
        }

        // 3. Rate limiting / quota checks
        // ...

        return {};
    }
};
```

### 5.3 Capability-to-Service Mapping

```cpp
// Kernel: grant services based on capabilities
void inject_services(RuntimeServices& svc, const ContractCapabilities& caps) {
    if (caps.test(Capability::Crypto))     svc.crypto    = &global_crypto_service();
    if (caps.test(Capability::Vault))      svc.vault     = &global_vault_service();
    if (caps.test(Capability::Network))    svc.network   = &global_network_service();
    if (caps.test(Capability::Filesystem)) svc.fs        = &global_file_service();
    if (caps.test(Capability::Storage))    svc.storage   = &global_storage_service();
    if (caps.test(Capability::Scheduler))  svc.scheduler = &global_scheduler_service();
    if (caps.test(Capability::Audit))      svc.audit     = &global_audit_service();
    if (caps.test(Capability::Metrics))    svc.metrics   = &global_metrics_service();
    svc.granted_caps = caps;
}
```

---

## 6. Examples

### JoinContract Lifecycle

```
1. Registry discovers JoinContract → Registered
2. Kernel: load("system.join") → Loaded
3. Kernel: initialize("system.join", config) → Initialized
   → allocates crypto handles, opens network connection
4. Kernel: state == Ready
5. CLI submits RuntimeRequest("system.join", "join", ...)
6. Dispatcher → JoinContract::execute(input, ctx)
   → ctx.services->crypto is non-null (Crypto capability granted)
   → ctx.services->vault is null (not granted)
7. JoinContract returns ContractResult::ok(...)
8. Kernel aggregates, audits, returns
9. Kernel: shutdown("system.join") → Idle
   → releases crypto handles, closes network connection
10. Kernel: unload("system.join") → Unloaded
```

### RecoveryContract Capabilities

```cpp
// RecoveryContract needs more than Join
ContractMetadata RecoveryContract::metadata() const {
    return {
        .id = "system.recovery",
        .required_capabilities = ContractCapabilities{}
            .set(Capability::Crypto)
            .set(Capability::Vault)       // seed/mnemonic access
            .set(Capability::Governance), // mesh state modification
        // ...
    };
}
```

```cpp
// Kernel will grant Crypto + Vault + Governance
// RecoveryContract CAN access:
//   ctx.services->crypto    ✅
//   ctx.services->vault     ✅
// RecoveryContract CANNOT access:
//   ctx.services->fs        ❌ (Filesystem not granted)
//   ctx.services->storage   ❌ (Storage not granted)
//   ctx.services->scheduler ❌ (Scheduler not granted)
```

---

## 7. Consequences

### Positive
- **Clear lifecycle** — every contract follows same init → ready → shutdown → unload path
- **Least privilege** — contracts get exactly the capabilities they declare
- **Registry single source of truth** — one place to find, load, and manage contracts
- **Metadata drives policy** — PolicyEngine reads metadata, not hardcoded lists
- **Fault isolation** — CrashLoop detection prevents broken contracts from consuming CPU
- **WASM-ready** — lifecycle + registry same for native and WASM

### Negative
- **Boilerplate** — every contract must implement `metadata()` with full schema
- **Registry complexity** — load/initialize/shutdown/unload state machine
- **Capability audit burden** — every new capability needs PolicyEngine update
- **Runtime startup cost** — loading all contracts at boot vs lazy-load tradeoff

---

## 8. Files Affected

| File | Change |
|------|--------|
| `core/runtime/contract_interface.hpp` | Add `initialize()` / `shutdown()` hooks |
| `core/runtime/runtime_types.hpp` | Add `ContractLifecycle`, `ContractLifecycleState` |
| `core/runtime/contract_metadata.hpp` | New file: full `ContractMetadata` schema |
| `core/runtime/contract_registry.hpp` | New file: `ContractRegistry` (metadata/lookup/discovery) |
| `core/runtime/contract_registry.cpp` | Implementation |
| `core/runtime/contract_manager.hpp` | New file: `ContractManager` (lifecycle: load/init/shutdown/unload) |
| `core/runtime/contract_manager.cpp` | Implementation |
| `core/runtime/dispatcher.hpp` | Remove `register_contract` → Registry owns this |
| `core/runtime/runtime_kernel.cpp` | Use Registry for contract lookup |
| `core/runtime/policy_engine.hpp` | Add capability check method |
| `core/runtime/services/` | Service injection (RFC 0037) |
| All Native Contracts | Implement `metadata()` with full schema |

---

## References

- [RFC 0025 — Contract Runtime Architecture](../RFC/0025-contract-runtime.md) (superseded)
- [RFC 0030 — Native Contracts](../RFC/0030-native-contracts.md) (superseded)
- [RFC 0035 — Runtime Architecture](../RFC/0035-runtime-architecture.md)
- [RFC 0036 — Contract ABI Freeze](../RFC/0036-contract-abi-freeze.md)
- [RFC 0037 — Runtime Service Injection](../RFC/0037-runtime-service-injection.md)
