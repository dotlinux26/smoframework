# RFC 0039 — NextAction Model

**Status:** APPROVED — Conditions resolved (removed ActionComplete, renamed ActionNextContract→ActionDispatchContract, ActionDispatch→ActionDispatchMessage)  
**Date:** 2026-07-18  
**Authors:** @dotlinux26, @D-O-T-Solutions  
**Supersedes:** —  
**Extends:** RFC 0035 (Runtime Architecture), RFC 0036 (Contract ABI Freeze)

---

## 1. Motivation

RFC 0035 defines `NextAction` as:

```cpp
// From RFC 0035 (current)
struct NextAction {
    enum class Type {
        NextContract, Retry, EmitEvent, StoreContext, AbortPlan,
        ForkPlan, MergePlan, ScheduleRetry, Compensate, Complete
    };
    Type type = Type::Complete;
    std::string contract_id;
    std::string payload;
    // ... 8 more string fields, 1 uint64_t, 1 map
};
```

This has four problems:

1. **Flat enum is not extensible** — adding `Notify`, `SpawnPlan`, `Dispatch` requires changing the core enum
2. **Too many fields** — 12 fields for 10 action types; most are unused per action
3. **No scheduling metadata** — `ScheduleRetry` has no cron/interval/count
4. **No data payload** — actions carry only strings; no typed parameters

**This RFC replaces the monolithic struct with a tagged-union pattern** using `std::variant`.

---

## 2. Design

### 2.1 Action Types (Extensible via Tagged Union)

```cpp
// ──Individual Action Data ──────────────────────────────────────────
struct ActionDispatchContract {
    std::string contract_id;
    ContractInput input;                    // full contract input
};

struct ActionDispatchMessage {
    std::string opcode;                     // opcode 0x01–0xFF
    std::vector<uint8_t> data;              // raw packet
    std::string target_node;
    uint64_t timeout_ns = 5'000'000'000;    // 5s default
};

struct ActionScheduleRetry {
    uint64_t delay_ns;
    uint64_t max_retries = 3;
    double backoff_multiplier = 2.0;        // exponential backoff
};

struct ActionEmitEvent {
    std::string event_type;
    std::string payload;
    EventPriority priority = EventPriority::Normal;
};

struct ActionStoreContext {
    std::string key;
    ContextValue value;                     // type-safe value, not string
};

struct ActionSpawnPlan {
    std::string plan_id;
    std::unordered_map<std::string, std::string> plan_params;
    ExecutionMode mode = ExecutionMode::Sequential;
};

struct ActionNotify {
    std::string target;                     // node_id / mesh_id / "admin"
    std::string message;
    std::unordered_map<std::string, std::string> metadata;
};

struct ActionCompensate {
    std::string compensation_plan_id;
    std::string reason;
};

struct ActionAbort {
    std::string reason;
    bool trigger_compensation = true;
};

// ── NextAction = Tagged Union ──────────────────────────────────────
// No ActionComplete — absent variant means "no next action, complete".
using NextAction = std::variant<
    ActionDispatchContract,
    ActionDispatchMessage,
    ActionScheduleRetry,
    ActionEmitEvent,
    ActionStoreContext,
    ActionSpawnPlan,
    ActionNotify,
    ActionCompensate,
    ActionAbort
>;
```

### 2.2 Why `std::variant` instead of enum + union?

| Approach | Pros | Cons |
|----------|------|------|
| **Enum + union** | Familiar, C-compatible | Unsafe, manual tag management, UB if wrong tag |
| **`std::variant`** | Type-safe, `visit()` pattern, no UB, extensible | Requires C++17, larger compile footprint |
| **Inheritance** | Polymorphic, OO-friendly | Heap allocation, vtable, not value-type |

**Decision:** `std::variant` — type safety without heap allocation.

### 2.3 Usage

```cpp
// Contract returns a next action
Result<ContractResult> MyContract::execute(
    const ContractInput& input, const RuntimeContext& ctx)
{
    // Dispatch to another contract
    return ContractResult::with_next(
        ActionDispatchContract{"vault.store", ContractInput::with_map("store", {{"key", "x"}})},
        "stored"
    );

    // Dispatch a raw message to another node
    return ContractResult::with_next(
        ActionDispatchMessage{Opcode::BootstrapRequest, encoded_data, "node-42"},
        ""
    );

    // Schedule retry
    return ContractResult::with_next(
        ActionScheduleRetry{1'000'000'000, 3, 2.0},  // 1s, 3 retries, x2 backoff
        "retrying"
    );

    // Emit event
    return ContractResult::with_next(
        ActionEmitEvent{"node.joined", "node-42", EventPriority::High},
        ""
    );

    // No next action = complete (ActionComplete is removed — absent action means terminal)
    return ContractResult::ok("done");
}
```

### 2.4 PlanExecutor Integration

```cpp
// In PlanExecutor::execute()
for (auto& action : step_result.actions) {
    std::visit(overloaded{
        [&](const ActionDispatchContract& a) {
            // Queue step for contract_id = a.contract_id
            plan_ctx.next_contracts.push_back(a);
        },
        [&](const ActionDispatchMessage& a) {
            // Send raw message to target_node via transport
            if (plan_ctx.transport) {
                plan_ctx.transport->send(a.target_node, a.opcode, a.data);
            }
        },
        [&](const ActionScheduleRetry& a) {
            // Mark step as WaitingRetry with backoff config
            plan_ctx.pending_retries[step_id] = a;
        },
        [&](const ActionEmitEvent& a) {
            // Publish to EventBus
            if (plan_ctx.event_bus) {
                Event e;
                e.type = event_type_from_string(a.event_type);
                e.details = a.payload;
                plan_ctx.event_bus->publish(e);
            }
        },
        [&](const ActionStoreContext& a) {
            // Store in shared context
            plan_ctx.context[a.key] = a.value.to_string();
        },
        [&](const ActionSpawnPlan& a) {
            // Resolve and execute sub-plan
        },
        [&](const ActionAbort& a) {
            // Abort plan, optionally trigger compensation
            plan_ctx.aborted = true;
            plan_ctx.abort_reason = a.reason;
        },
        [&](const ActionCompensate& a) {
            // Execute compensation plan
        },
        [&](const ActionNotify& a) {
            // Send notification
        }
    }, action);
}
```

### 2.5 `overloaded` Helper

```cpp
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;
```

### 2.6 Complete + Chaining

For simple cases, helper functions wrap the variant:

```cpp
// Shorthand factories (return NextAction directly)
NextAction dispatch_contract(std::string id, ContractInput input = {}) {
    return ActionDispatchContract{std::move(id), std::move(input)};
}

NextAction dispatch_message(std::string opcode, std::vector<uint8_t> data,
                             std::string target = "", uint64_t timeout = 5'000'000'000) {
    return ActionDispatchMessage{std::move(opcode), std::move(data),
                                  std::move(target), timeout};
}

NextAction retry(uint64_t delay_ns = 1'000'000'000,
                  uint64_t max_retries = 3,
                  double backoff = 2.0) {
    return ActionScheduleRetry{delay_ns, max_retries, backoff};
}

NextAction emit_event(std::string type, std::string payload = "") {
    return ActionEmitEvent{std::move(type), std::move(payload)};
}
```

---

## 3. ContractResult + NextAction

The `ContractResult` from RFC 0036 carries a `std::vector<NextAction>`:

```cpp
struct ContractResult {
    Status status;
    std::string data;
    std::vector<NextAction> next_actions;  // ← extensible action chain

    static ContractResult ok(std::string data = "") { ... }
    static ContractResult ok_with_next(NextAction action, std::string data = "") {
        return {Status::Success, std::move(data), {}, {std::move(action)}, {}};
    }
    // ...
};
```

The kernel processes `next_actions` after successful execution:
1. If empty → aggregate result, return
2. If actions exist → ProcessPlanExecutor chains them

---

## 4. Consequences

### Positive
- **Extensible** — add new action type by adding a struct to the variant, no core enum change
- **Type-safe** — `visit()` guarantees all types handled (or `auto` catch-all)
- **Self-documenting** — each action has its own struct with semantically-named fields
- **No unused fields** — each struct has exactly what it needs
- **Scheduler ready** — `ActionScheduleRetry` has backoff, max_retries built in
- **WASM compatible** — `std::variant` maps to tagged union in wasm

### Negative
- **`std::variant` verbosity** — `visit()` pattern is more verbose than `switch`
- **C++17 required** — already met
- **Larger binary** — variant machinery adds codegen
- **Learning curve** — team must learn `visit()` / `overloaded` pattern

---

## 5. Migration Path

1. Replace `NextAction` enum + flat struct with `std::variant<Action*...>`
2. Update all factory methods to return variant actions
3. Update `ContractResult::next_actions` to use new type
4. Update `PlanExecutor` to use `std::visit` dispatch
5. Compile all NativeContracts against new model

---

## 6. Files Affected

| File | Change |
|------|--------|
| `core/runtime/runtime_types.hpp` | Replace `NextAction` with variant + action structs |
| `core/runtime/contract_interface.hpp` | Update `ContractResult` |
| `core/runtime/plan_executor.hpp` | Use `std::visit` dispatch |
| `core/runtime/runtime_kernel.cpp` | Update action processing |
| All Native Contracts | Return new action types |

---

## References

- [RFC 0035 — Runtime Architecture](../RFC/0035-runtime-architecture.md)
- [RFC 0036 — Contract ABI Freeze](../RFC/0036-contract-abi-freeze.md)
- [RFC 0038 — Execution State Machine](../RFC/0038-execution-state-machine.md)
