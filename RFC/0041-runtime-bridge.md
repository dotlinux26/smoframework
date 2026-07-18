# RFC 0041 — Runtime Bridge & Protocol Mapping

**Status:** DRAFT — Proposed for Sprint 37  
**Date:** 2026-07-18  
**Extends:** RFC 0035 (Runtime Architecture), RFC 0036 (Contract ABI Freeze)

---

## 1. Motivation

Current state:

```
Packet → PacketDispatcher → handler(packet)
```

There is no bridge to RuntimeKernel. Every packet is handled by a raw handler function registered in `main.cpp`. Contracts (JoinContract, BootstrapContract, GovernanceContract, etc.) are never executed through the runtime pipeline.

**What we need:**

```
Packet → PacketDispatcher → RuntimeBridge → RuntimeRequest → RuntimeKernel → Contract → ContractResult → vector<NextAction> → ActionExecutor → Transport
```

This RFC defines the missing bridge — the mapping from network opcodes to contract invocations. The bridge is **purely a Runtime concern** — it does not know about TCP, UDP, or any transport.

---

## 2. Design

### 2.1 Architecture

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│ TCP/UDP      │────▶│ Packet       │────▶│ RuntimeBridge│
│ Transport    │     │ Dispatcher   │     │              │
└──────────────┘     └──────────────┘     │ route lookup │
                                          │ packet → req │
                                          │ execute      │
                                          └──────┬───────┘
                                                 │
                                          ┌──────▼───────┐
                                          │ RuntimeKernel│
                                          │ → Dispatcher │
                                          │ → Contract   │
                                          └──────┬───────┘
                                                 │
                                          ┌──────▼───────┐
                                          │ ActionExecutor│
                                          │ ┌──────────┐ │
                                          │ │ TCP send │ │
                                          │ │ UDP send │ │
                                          │ │ Timer    │ │
                                          │ │ Disk     │ │
                                          │ │ Spawn    │ │
                                          │ └──────────┘ │
                                          └──────────────┘
```

The RuntimeBridge is a **thin orchestration layer**:
1. Receives a `Packet` from `PacketDispatcher`
2. Looks up opcode → (contract_id, method) mapping
3. Builds a `RuntimeRequest` from packet fields + payload
4. Calls `RuntimeKernel::execute()`
5. Returns `RuntimeResult` containing `vector<NextAction>`
6. Upper layer (PacketDispatcher or ActionExecutor) handles dispatch

**Bridge does NOT know Transport.** It produces `NextAction` values. An `ActionExecutor` reads those and dispatches to the appropriate output (TCP, UDP, timer, file, spawn).

### 2.2 OpcodeRoute — Mapping Table

```cpp
struct OpcodeRoute {
    std::string contract_id;  // e.g. "system.join"
    std::string method;       // e.g. "join_request"
};
```

### 2.3 RuntimeBridge

```cpp
class RuntimeBridge {
public:
    RuntimeBridge(RuntimeKernel& kernel);

    // Register opcode → contract mapping
    void register_route(Opcode opcode, OpcodeRoute route);
    void register_route(uint32_t opcode_id, std::string contract_id, std::string method);

    // Unregister
    void unregister_route(Opcode opcode);

    // Resolve opcode → route
    Result<OpcodeRoute> resolve(Opcode opcode) const;
    Result<OpcodeRoute> resolve(uint32_t opcode_id) const;

    // Bridge: packet → kernel → RuntimeResult (pure Runtime, no Transport)
    Result<RuntimeResult> bridge(Packet&& pkt);

private:
    RuntimeKernel* kernel_;
    std::unordered_map<uint32_t, OpcodeRoute> routes_;
};
```

**Register all system routes at startup:**

```cpp
bridge.register_route(Opcode::BOOTSTRAP_REQUEST,  "system.bootstrap", "bootstrap_request");
bridge.register_route(Opcode::BOOTSTRAP_RESPONSE, "system.bootstrap", "bootstrap_response");
bridge.register_route(Opcode::GOV_PROPOSE,        "system.governance", "propose");
bridge.register_route(Opcode::GOV_VOTE,           "system.governance", "vote");
bridge.register_route(Opcode::GOV_COMMIT,         "system.governance", "commit");
bridge.register_route(Opcode::RECOVERY_SESSION,   "system.recovery", "start");
bridge.register_route(Opcode::CRL_SYNC,           "system.recovery", "crl_sync");
bridge.register_route(Opcode::REVOKE_CERT,        "system.recovery", "crl_revoke");
bridge.register_route(Opcode::LS,                 "system.file", "list");
bridge.register_route(Opcode::PUT,                "system.file", "write");
bridge.register_route(Opcode::EXEC,               "system.process", "exec");
```

**Design note:** `register_route` is the plugin extension point. To add a new contract, you register its opcodes → no dispatcher modification needed. This is a pure plugin architecture.

### 2.4 bridge() — The Core Flow

```cpp
Result<RuntimeResult> RuntimeBridge::bridge(Packet&& pkt) {
    // 1. Resolve opcode → route
    auto route = resolve(pkt.opcode_id);
    if (!route) {
        return RuntimeError::not_found("unknown opcode: " + std::to_string(pkt.opcode_id));
    }

    // 2. Build RuntimeRequest from packet (serialization per RFC 0043)
    auto req = RuntimeRequestBuilder::from_packet(route.value(), pkt);
    if (!req) return std::move(req.error());

    // 3. Execute via RuntimeKernel (create_context is internal to execute)
    return kernel_.execute(req.value());
}
```

**Design note:** `RuntimeKernel::execute()` creates a `RuntimeContext` internally and returns `RuntimeResult`. The bridge does not touch context — it is a network→runtime adapter only.

Note: `bridge()` returns `RuntimeResult` with `next_actions`. It does NOT send anything. The caller (PacketDispatcher) passes the `NextAction` list to `ActionExecutor`.

### 2.5 ActionExecutor

Separate component responsible for executing `NextAction` values:

```cpp
class ActionExecutor {
public:
    using ActionHandler = std::function<Result<void>(const NextAction&)>;

    void register_handler(NextAction::Type type, ActionHandler handler);

    Result<void> execute(const std::vector<NextAction>& actions);

    // Built-in handlers:
    static ActionHandler tcp_send(hl::Transport& transport, const std::string& remote);
    static ActionHandler udp_send(hl::Transport& transport, const std::string& remote);
    static ActionHandler timer(Scheduler& scheduler);
    static ActionHandler disk_write(const std::string& path);
    static ActionHandler spawn_process();

private:
    std::unordered_map<NextAction::Type, ActionHandler> handlers_;
};
```

**PacketDispatcher integration:**

```cpp
// In PacketDispatcher::dispatch_session():
auto result = bridge.bridge(std::move(pkt));
if (!result) { /* send error */ return; }

ActionExecutor executor;
executor.register_handler(
    NextAction::Type::DispatchMessage,
    ActionExecutor::tcp_send(transport_adapter, remote)
);
executor.execute(result.value().next_actions);
```

This keeps RuntimeBridge pure — no transport dependency. New action handlers (disk, process, timer) are added by registering new handlers, not by modifying the bridge.

### 2.6 Packet ↔ RuntimeRequest Transformation

```cpp
struct RuntimeRequestBuilder {
    static Result<RuntimeRequest> from_packet(
        const OpcodeRoute& route, const Packet& pkt)
    {
        RuntimeRequest req;
        req.request_id = generate_request_id();
        req.contract_id = route.contract_id;
        req.opcode = std::to_string(pkt.opcode_id);
        req.requester = to_hex(pkt.session_id);

        // Deserialize payload → ContextValue (RFC 0043)
        auto cv = ContextValue::from_cbor(pkt.payload);
        if (!cv) {
            return RuntimeError::validation("payload deserialize failed");
        }

        ContractInput input;
        input.method = route.method;
        input.arguments = std::move(cv.value());
        req.input = std::move(input);

        return req;
    }
};
```

### 2.7 Response Routing & Correlation

Every request-response pair is correlated by `intent_id` + `session_id`:

```cpp
struct ResponseRouter {
    std::unordered_map<uint64_t, PendingResponse> pending_;

    void register_pending(const Packet& pkt, PendingResponse handler);
    Result<PendingResponse> resolve(const Packet& resp);
};
```

**Correlation fields:**
- `session_id` — identifies the connection
- `intent_id` — identifies the request within that session
- Response packet echoes both fields from the original request

---

## 3. Error Handling

| Error | Cause | Response Code |
|-------|-------|---------------|
| Unknown opcode | No route registered | 0xFF01 |
| Payload parse failed | CBOR decode error | 0xFF02 |
| Contract not found | Dispatcher has no such contract | 0xFF03 |
| Contract execution error | Contract returned Error status | 0xFF04 |
| Policy denied | Middleware rejected | 0xFF05 |
| Timeout | Kernel deadline exceeded | 0xFF06 |

Error responses are packets with `opcode_id = pkt.opcode_id | 0x8000` (error flag).

---

## 4. Consequences

### Positive
- **Single entry point** — all network traffic goes through one bridge
- **Full runtime pipeline** — policy, audit, middleware apply to network requests
- **Contract reuse** — same contract handles both CLI and network requests
- **Clean opcode → contract mapping** — extensible without modifying handlers
- **Bridge is pure Runtime** — no transport dependency (SRP)
- **ActionExecutor is extensible** — new output types (disk, process, timer) add handlers, not modify bridge
- **Correlation built-in** — response routing without global state

### Negative
- **Bridge is synchronous** — kernel executes, then returns; async requires Scheduler (RFC 0044)
- **Payload deserialization overhead** — CBOR decode per packet
- **ActionExecutor requires registration** — must register handlers before use

---

## 5. Migration Path

1. Create `RuntimeBridge` class in `core/runtime/runtime_bridge.hpp/.cpp`
2. Create `ActionExecutor` in `core/runtime/action_executor.hpp/.cpp`
3. Register all system opcode routes at `main.cpp` startup
4. Modify `PacketDispatcher::dispatch_session()` to call `bridge.bridge()` + `executor.execute()`
5. Remove individual opcode handlers from `main.cpp`
6. Implement response opcode resolution table

---

## 6. Files Affected

| File | Change |
|------|--------|
| `core/runtime/runtime_bridge.hpp` | New — RuntimeBridge class |
| `core/runtime/runtime_bridge.cpp` | New — Implementation |
| `core/runtime/action_executor.hpp` | New — ActionExecutor class |
| `core/runtime/action_executor.cpp` | New — Implementation |
| `core/network/packet_dispatcher.cpp` | Add bridge + executor dispatch path |
| `core/runtime/runtime_kernel.hpp` | Add `create_context()` helper |
| `cmd/smo-node/main.cpp` | Register routes, remove raw handlers |

---

## References

- [RFC 0035 — Runtime Architecture](../RFC/0035-runtime-architecture.md)
- [RFC 0036 — Contract ABI Freeze](../RFC/0036-contract-abi-freeze.md)
- [RFC 0039 — NextAction Model](../RFC/0039-nextaction-model.md)
- [RFC 0043 — Serialization Layer](../RFC/0043-serialization-layer.md)
- [RFC 0044 — Runtime Scheduler](../RFC/0044-runtime-scheduler.md)
- [Discussion 0037 — Wiring Bridge](../docs/discussions/DISCUSSION_0037_WIRING_BRIDGE.md)
