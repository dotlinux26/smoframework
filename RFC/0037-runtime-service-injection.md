# RFC 0037 — Runtime Service Injection

**Status:** APPROVED — Conditions resolved (removed reinterpret_cast, added ClockService + RandomService)  
**Date:** 2026-07-18  
**Authors:** @dotlinux26, @D-O-T-Solutions  
**Supersedes:** —  
**Extends:** RFC 0035 (Runtime Architecture), RFC 0036 (Contract ABI Freeze)

---

## 1. Motivation

In RFC 0035, `RuntimeContext` held service pointers directly:

```cpp
// From RFC 0035 (current)
struct RuntimeContext {
    CryptoProvider* crypto = nullptr;
    Storage* storage = nullptr;
    Transport* transport = nullptr;
    PolicyEngine* policy = nullptr;
    Scheduler* scheduler = nullptr;
    // ... more services as flat pointers
    std::unordered_map<std::string, ContextValue> context;  // mixed with data!
    uint64_t execution_id = 0;
    uint64_t deadline_ns = 0;
    CapabilitySet granted_caps;
};
```

This has three problems:

1. **No capability gating** — every contract sees every service pointer
2. **Flat namespace** — services and execution data mixed together
3. **No DI** — tests must construct the full `RuntimeContext` even to test a simple contract
4. **No extensibility** — adding a new service means adding a new field to the struct

**This RFC separates concerns into three layers:**

```
RuntimeContext
 ├── ExecutionInfo     (read-only execution metadata)
 ├── Variables         (mutable context key-value store)
 └── RuntimeServices   (injected services, capability-gated)
```

---

## 2. Design

### 2.1 RuntimeContext (Refactored)

```cpp
struct RuntimeContext {
    ExecutionInfo info;           // read-only
    Variables vars;               // mutable key-value store
    RuntimeServices* services;    // injected, capability-gated (may be null per capability)
};
```

`RuntimeContext` is now a **thin shell** — it delegates to three specialized sub-structs.  
Contracts access services through `ctx.services->crypto`, `ctx.services->vault`, etc.

### 2.2 ExecutionInfo (Immutable per Execution)

```cpp
struct ExecutionInfo {
    uint64_t execution_id = 0;
    uint64_t parent_execution_id = 0;
    std::string correlation_id;
    std::string requester;          // node_id / "admin" / "system"
    std::string contract_id;
    std::string node_id;
    std::string mesh_id;
    uint64_t epoch = 0;
    uint64_t deadline_ns = 0;       // absolute deadline
    uint64_t started_at_ns = 0;
    uint64_t budget_ns = 30'000'000'000;  // execution budget

    bool is_expired() const {
        if (deadline_ns == 0) return false;
        auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return now > deadline_ns;
    }
};
```

**Properties:**
- Read-only after creation
- Single source of truth for execution identity
- `is_expired()` utility for deadline enforcement

### 2.3 Variables (Mutable Context Store)

```cpp
struct Variables {
    std::unordered_map<std::string, ContextValue> store;

    void set(const std::string& key, ContextValue value) {
        store[key] = std::move(value);
    }

    Result<ContextValue> get(const std::string& key) const {
        auto it = store.find(key);
        if (it == store.end())
            return Result<ContextValue>(static_cast<Error>(
                RuntimeError::not_found("variable not found: " + key)));
        return it->second;
    }

    template<typename T>
    Result<T> get(const std::string& key) const {
        auto v = get(key);
        if (!v) return Result<T>(v.error());
        return v.value().template get<T>();
    }

    bool has(const std::string& key) const { return store.find(key) != store.end(); }
    void erase(const std::string& key) { store.erase(key); }
};
```

**Properties:**
- Only mutable part of RuntimeContext
- Type-safe via `ContextValue`
- Template `get<T>()` for strongly-typed access

### 2.4 RuntimeServices (Dependency Injection Container)

```cpp
struct RuntimeServices {
    // ── Core Services ────────────────────────────────────────────────
    CryptoService*    crypto    = nullptr;
    IdentityService*  identity  = nullptr;
    VaultService*     vault     = nullptr;

    // ── Storage ──────────────────────────────────────────────────────
    StorageService*   storage   = nullptr;
    FileService*      fs        = nullptr;

    // ── Network ──────────────────────────────────────────────────────
    NetworkService*   network   = nullptr;
    TransportService* transport = nullptr;

    // ── Runtime ──────────────────────────────────────────────────────
    SchedulerService* scheduler = nullptr;
    PolicyEngine*     policy    = nullptr;

    // ── Observability ────────────────────────────────────────────────
    AuditService*     audit     = nullptr;
    HistoryService*   history   = nullptr;
    MetricsService*   metrics   = nullptr;
    LoggerService*    logger    = nullptr;

    // ── Time & Random ────────────────────────────────────────────────
    ClockService*     clock     = nullptr;    // deterministic time for testing
    RandomService*    random    = nullptr;    // deterministic RNG for testing

    // ── Capability Gating ────────────────────────────────────────────
    ContractCapabilities granted_caps;

    bool has_capability(ContractCapability cap) const {
        return granted_caps.test(static_cast<size_t>(cap));
    }
};
```

**Properties:**
- All pointers may be `nullptr` — contract must check before use
- `granted_caps` bitfield determines which services are visible
- In WASM mode, `RuntimeServices` becomes a **host function table** — same interface, different ABI

### 2.5 Service Interfaces (Minimal, Frozen)

Each service has a minimal interface. Examples:

```cpp
// ── CryptoService ────────────────────────────────────────────────────
class CryptoService {
public:
    virtual ~CryptoService() = default;
    virtual Result<Signature> sign(const Bytes& data, const KeyID& key) = 0;
    virtual Result<bool> verify(const Bytes& data, const Signature& sig, const PublicKey& pk) = 0;
    virtual Result<Bytes> encrypt(const Bytes& plaintext, const PublicKey& pk) = 0;
    virtual Result<Bytes> decrypt(const Bytes& ciphertext, const KeyID& key) = 0;
    virtual Result<KeyID> generate_key(KeyType type) = 0;
};

// ── VaultService ────────────────────────────────────────────────────
class VaultService {
public:
    virtual ~VaultService() = default;
    virtual Result<void> store(const std::string& key, const Bytes& secret) = 0;
    virtual Result<Bytes> retrieve(const std::string& key) = 0;
    virtual Result<void> delete_key(const std::string& key) = 0;
    virtual Result<bool> exists(const std::string& key) = 0;
};

// ── AuditService ────────────────────────────────────────────────────
class AuditService {
public:
    virtual ~AuditService() = default;
    virtual void emit(const AuditEvent& event) = 0;
};

// ── LoggerService ────────────────────────────────────────────────────
class LoggerService {
public:
    virtual ~LoggerService() = default;
    virtual void info(const std::string& msg) = 0;
    virtual void warn(const std::string& msg) = 0;
    virtual void error(const std::string& msg) = 0;
    virtual void debug(const std::string& msg) = 0;
};

// ── ClockService ────────────────────────────────────────────────────
class ClockService {
public:
    virtual ~ClockService() = default;
    virtual uint64_t now_ns() = 0;              // monotonic
    virtual uint64_t wall_clock_ns() = 0;       // wall clock
    virtual void advance(uint64_t delta_ns) = 0; // for deterministic testing
};

// ── RandomService ────────────────────────────────────────────────────
class RandomService {
public:
    virtual ~RandomService() = default;
    virtual Bytes random_bytes(size_t count) = 0;
    virtual uint64_t random_u64() = 0;
    virtual void seed(const Bytes& entropy) = 0; // for deterministic testing
};
```

---

## 3. Capability Injection Flow

```
RuntimeKernel::execute(request)
    │
    ├── 1. Resolve contract → get required_capabilities()
    │
    ├── 2. PolicyEngine checks: does requester have these capabilities?
    │      ├── Yes → proceed
    │      └── No  → return Denied
    │
    ├── 3. Create RuntimeServices with granted_caps = intersection(
    │         requester_caps, contract_required_caps)
    │
    ├── 4. Inject only the services for granted capabilities:
    │      crypto → if Crypto cap granted
    │      vault  → if Vault cap granted
    │      ...
    │
    ├── 5. Create RuntimeContext { info, vars, services }
    │
    └── 6. Dispatcher → Contract::execute(input, ctx)
```

Unused services remain `nullptr`. Contract checks before use:

```cpp
Result<ContractResult> JoinContract::execute(
    const ContractInput& input, const RuntimeContext& ctx)
{
    if (!ctx.services || !ctx.services->crypto)
        return ContractResult::denied("crypto service not available");
    // ...
}
```

---

## 4. Testability

```cpp
// Test: no service mocking framework needed
TEST(JoinContract, SignJoinToken) {
    JoinContract contract;
    auto mock_crypto = MockCryptoService();  // implement CryptoService interface

    RuntimeServices services;
    services.crypto = &mock_crypto;
    services.granted_caps.set(static_cast<size_t>(ContractCapability::Crypto));

    RuntimeContext ctx;
    ctx.services = &services;
    ctx.info.execution_id = 1;

    auto res = contract.execute(join_input, ctx);
    ASSERT_TRUE(res.is_ok());
}
```

No need to construct a full RuntimeKernel — just inject what the contract needs.

---

## 5. WASM Compatibility

When contracts run in WASM:
- `RuntimeServices` becomes a **table of function pointers** exported from host
- WASM guest accesses services via host calls: `runtime_service_call(service_id, function_id, args...)`
- Same `RuntimeServices` API contracts compiled to native use — but behind a FFI boundary

---

## 6. Consequences

### Positive
- **Least privilege** — contract sees only what it needs
- **Testability** — inject mock services per test
- **WASM-ready** — same service table becomes host function table
- **Extensible** — add service pointer, update bitfield, done
- **No RuntimeContext bloat** — info/vars/services separation

### Negative
- **Null check overhead** — contracts must check `ctx.services->crypto != nullptr`
- **One more indirection** — `ctx.services->crypto` vs `ctx.crypto`
- **Service lifecycle** — kernel must ensure services outlive execution

---

## 7. Files Affected

| File | Change |
|------|--------|
| `core/runtime/runtime_context.hpp` | Refactor into info/vars/services |
| `core/runtime/runtime_types.hpp` | Add `ExecutionInfo`, `Variables`, `ContractCapabilities` |
| `core/runtime/services/` | New directory: service interfaces |
| `core/runtime/runtime_kernel.cpp` | Update pipeline for capability injection |
| All Native Contracts | Update access from `ctx.crypto` → `ctx.services->crypto` |

---

## References

- [RFC 0035 — Runtime Architecture](../RFC/0035-runtime-architecture.md)
- [RFC 0036 — Contract ABI Freeze](../RFC/0036-contract-abi-freeze.md)
- [RFC 0040 — Contract Lifecycle + Metadata + Capabilities](../RFC/0040-contract-lifecycle-metadata-capabilities.md)
