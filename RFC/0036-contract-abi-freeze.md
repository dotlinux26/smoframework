# RFC 0036 — Contract ABI Freeze

**Status:** APPROVED — Conditions resolved (ContextValue arguments, partial-const RuntimeContext, strong metrics type)  
**Date:** 2026-07-18  
**Authors:** @dotlinux26, @D-O-T-Solutions  
**Supersedes:** RFC 0026 (Contract ABI Specification)  
**Extends:** RFC 0035 (Runtime Architecture)

---

## 1. Motivation

RFC 0035 froze the Runtime Architecture and Execution Model, but the **Contract ABI** — the interface between Dispatcher and Contract — was left as a placeholder:

```cpp
// From RFC 0035 (current, unfrozen)
class ContractInterface {
    virtual Result<ExecutionOutput> execute(const ExecutionInput& input,
                                             RuntimeContext& ctx) = 0;
};
```

This is too loose. Multiple Native Contracts (Join, Bootstrap, Governance, Recovery) are about to be implemented — without a frozen ABI, each contract will evolve `execute()` in incompatible directions (different parameter structures, mutable vs immutable results, side-channel data passing through `RuntimeContext`).

**This RFC freezes the Contract ABI** so that all present and future contracts share a single, immutable interface.

---

## 2. Design Principles

| Principle | Rationale |
|-----------|-----------|
| **Immutable Input** | Contract must never modify the input; copy-on-write if needed |
| **Immutable Result** | Result is a value, not a mutable reference; contract returns, kernel reads |
| **Partial Mutability** | `RuntimeContext` is `const` at the top level, but `ctx.vars` is mutable (key-value store). `ExecutionInfo` is immutable, `RuntimeServices` is const pointer. This is enforced by C++ type system — see RFC 0037. |
| **Structured Input** | `ContractInput` = method + typed params, not raw payload string |
| **Structured Output** | `ContractResult` = status + data + next_actions, not raw output string |
| **ABI Versioned** | `api_version` in metadata ensures forward compatibility |
| **No Hidden Dependencies** | Contract declares all capabilities explicitly in metadata |

---

## 3. Contract ABI (FROZEN)

### 3.1 ContractInterface

```cpp
class ContractInterface {
public:
    virtual ~ContractInterface() = default;

    // ── Identity ─────────────────────────────────────────────────────
    virtual std::string id() const = 0;
    virtual const ContractMetadata& metadata() const = 0;
    virtual ContractCapabilities required_capabilities() const = 0;

    // ── Lifecycle ────────────────────────────────────────────────────
    virtual Result<void> initialize(const ContractConfig& config);
    virtual Result<void> shutdown();

    // ── Core Execution ───────────────────────────────────────────────
    virtual Result<ContractResult> execute(
        const ContractInput& input,
        const RuntimeContext& ctx
    ) = 0;

    // ── Validation ───────────────────────────────────────────────────
    virtual Result<void> validate(const ContractInput& input) const;
};
```

**Key differences from RFC 0035:**
- `execute()` takes `const ContractInput&` and `const RuntimeContext&` (immutable)
- Returns `ContractResult` (immutable value, not `ExecutionOutput`)
- Added `initialize()` / `shutdown()` lifecycle hooks
- Added `validate()` as a separate method (not inline)
- Added `id()` for registry lookup
- `RuntimeContext` is `const` — contract cannot mutate it

### 3.2 ContractInput

```cpp
struct ContractInput {
    std::string method;                    // e.g. "join", "leave", "propose", "vote"
    ContextValue arguments;                // type-safe: map, array, string, binary, number

    static ContractInput method_only(const std::string& method) {
        return {method, ContextValue()};
    }
    static ContractInput with_map(const std::string& method,
                                   const std::unordered_map<std::string, std::string>& map) {
        return {method, ContextValue(map)};
    }
    static ContractInput with_string(const std::string& method,
                                      const std::string& data) {
        return {method, ContextValue(data)};
    }
};
```

**Design rationale:** Replaces the old `params` + `payload` + `binary` (three overlapping fields) with a single `ContextValue`. `ContextValue` already supports `map`, `array`, `string`, `Bytes` — no duplication needed. Contract reads what it needs:

```cpp
// JoinContract reads arguments
auto mesh_id = input.arguments.get<std::string>("mesh_id");    // if stored as map
auto cert    = input.arguments.get<Bytes>("certificate");       // binary data
```

**Note:** No `headers` — headers belong to `RuntimeRequest`, not `ContractInput`. Contracts receive only execution-relevant data.

### 3.3 ContractResult

```cpp
struct ContractResult {
    enum class Status {
        Success,
        Denied,
        Pending,
        Retry,
        Compensated,
        Error
    };

    Status status = Status::Error;
    std::string data;                              // result data (JSON/CBOR)
    std::vector<uint8_t> binary;                   // binary result
    std::vector<NextAction> next_actions;          // chain/schedule/compensate
    std::unordered_map<std::string, ContextValue> metrics;  // contract-level metrics (int64, double, counter, histogram)

    // Factories — immutable by convention
    static ContractResult ok(std::string data = "") {
        return {Status::Success, std::move(data), {}, {}, {}};
    }
    static ContractResult denied(std::string reason = "") {
        return {Status::Denied, std::move(reason), {}, {}, {}};
    }
    static ContractResult pending(std::string execution_id = "") {
        return {Status::Pending, std::move(execution_id), {}, {}, {}};
    }
    static ContractResult retry_later(NextAction retry_action) {
        return {Status::Retry, "", {}, {std::move(retry_action)}, {}};
    }
    static ContractResult with_next(NextAction action, std::string data = "") {
        return {Status::Success, std::move(data), {}, {std::move(action)}, {}};
    }
};
```

**Key properties:**
- All fields are public but **const-by-convention** — kernel never mutates after creation
- `status` determines kernel behavior (Success → aggregate, Retry → scheduler, Denied → fail)
- `next_actions` replaces the old pattern of contracts calling Dispatcher directly

### 3.4 ContractMetadata

```cpp
struct ContractMetadata {
    std::string id;                        // unique contract ID
    std::string name;                      // human-readable name
    std::string version;                   // semver
    uint32_t api_version = 1;              // ABI version this contract targets
    std::string author;
    std::string description;
    ContractCapabilities capabilities;     // declared capabilities (bitfield)
    std::vector<std::string> dependencies; // dependent contract IDs
};
```

### 3.5 ContractCapabilities

```cpp
enum class ContractCapability : uint64_t {
    None        = 0,
    Crypto      = 1ULL << 0,
    Vault       = 1ULL << 1,
    Network     = 1ULL << 2,
    Filesystem  = 1ULL << 3,
    Scheduler   = 1ULL << 4,
    Governance  = 1ULL << 5,
    Recovery    = 1ULL << 6,
    Identity    = 1ULL << 7,
    Storage     = 1ULL << 8,
    Audit       = 1ULL << 9,
    Metrics     = 1ULL << 10,
    All         = ~0ULL
};

using ContractCapabilities = std::bitset<64>;
// Bit 0 = Crypto, Bit 1 = Vault, ...
```

### 3.6 ContractConfig

```cpp
struct ContractConfig {
    std::unordered_map<std::string, std::string> settings;
    ContractCapabilities granted_capabilities;  // what the kernel actually granted
    std::string data_dir;                        // contract-specific storage path
    uint64_t max_execution_time_ns = 30'000'000'000;  // 30s default
};
```

---

## 4. Migration: from old ABI to new ABI

### Old (RFC 0035)

```cpp
class ContractInterface {
    virtual Result<ExecutionOutput> execute(const ExecutionInput& input,
                                             RuntimeContext& ctx) = 0;
};
```

### New (This RFC)

```cpp
class ContractInterface {
    virtual Result<ContractResult> execute(
        const ContractInput& input,
        const RuntimeContext& ctx
    ) = 0;
};
```

### Migration Path

1. Rename `ExecutionInput` → `ContractInput`, `ExecutionOutput` → `ContractResult`
2. Change `RuntimeContext&` → `const RuntimeContext&`
3. Add `id()`, `initialize()`, `shutdown()`, `validate()`
4. Add `ContractCapabilities` bitfield
5. Update `ContractMetadata` with `api_version`, `capabilities`
6. Compile all existing NativeContracts against new ABI

---

## 5. Backward Compatibility

- Existing code using `ExecutionInput`/`ExecutionOutput` gets a deprecation `typedef` in `runtime_types.hpp`
- The old `ExecutionOutput` is kept as an alias: `using ExecutionOutput = ContractResult;` (only if fields align)
- Dispatcher wraps old-style contracts if needed via adapter

---

## 6. Consequences

### Positive
- Immutable ABI prevents contract-interface drift
- `const RuntimeContext&` guarantees no side-channel mutation
- `ContractCapabilities` enables least-privilege injection
- `initialize()` / `shutdown()` enable proper resource management
- `validate()` separates concerns from `execute()`

### Negative
- Requires updating all existing contract code
- `const RuntimeContext&` breaks existing code that mutates context (which was wrong anyway)
- Slightly more boilerplate for simple contracts

---

## 7. Files Affected

| File | Change |
|------|--------|
| `core/runtime/contract_interface.hpp` | Rewrite ABI (this RFC) |
| `core/runtime/runtime_types.hpp` | Remove old `ExecutionInput`/`ExecutionOutput`; add `ContractInput`/`ContractResult` |
| `core/runtime/dispatcher.hpp` | Update `execute()` signature |
| `core/runtime/dispatcher.cpp` | Update implementation |
| `core/runtime/runtime_kernel.cpp` | Update pipeline to use new types |
| `core/runtime/plan_executor.hpp` | Update step execution |
| All Native Contracts | Update to new interface |

---

## References

- [RFC 0026 — Contract ABI Specification](../RFC/0026-contract-abi.md) (superseded)
- [RFC 0035 — Runtime Architecture](../RFC/0035-runtime-architecture.md)
- [RFC 0037 — Runtime Service Injection](../RFC/0037-runtime-service-injection.md)
- [RFC 0040 — Contract Lifecycle + Metadata + Capabilities](../RFC/0040-contract-lifecycle-metadata-capabilities.md)
