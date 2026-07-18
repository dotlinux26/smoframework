# RFC 0043 — Runtime Serialization Layer

**Status:** DRAFT — Proposed for Sprint 37  
**Date:** 2026-07-18  
**Extends:** RFC 0036 (Contract ABI Freeze), RFC 0039 (NextAction Model)

---

## 1. Motivation

Current serialization is ad-hoc:

- `Packet.payload` is `std::vector<uint8_t>` — raw bytes, no schema
- `ContractInput.arguments` is `ContextValue` — type-safe but no wire format defined
- `RuntimeRequest.payload` is `std::string` — JSON/CBOR ambiguity
- Each contract parses its own input format independently

**What we need:**

A unified serialization pipeline:

```
Wire (Packet.payload)
    ↓ CBOR decode
ContextValue
    ↓ type-check
ContractInput
    ↓ contract execute
ContractResult
    ↓ CBOR encode
Wire (response Packet.payload)
```

This RFC freezes the serialization contract — all contracts, all packets, all requests use the same pipeline.

---

## 2. Design

### 2.1 Serialization Pipeline

```
┌─────────────┐     ┌──────────────┐     ┌──────────────┐
│ Packet      │────▶│              │────▶│ ContextValue │
│ .payload    │     │ CBOR Decode  │     │ (recursive)  │
│ (bytes)     │     │              │     │              │
└─────────────┘     └──────────────┘     └──────┬───────┘
                                                │
                                          ┌─────▼──────┐
                                          │ Schema     │
                                          │ Validation │
                                          │ (new)      │
                                          └─────┬──────┘
                                                │
                                          ┌─────▼──────┐
                                          │ Contract   │
                                          │ Input      │
                                          │ Builder    │
                                          └─────┬──────┘
                                                │
                                          ┌─────▼──────┐
                                          │ Contract   │
                                          │ .execute() │
                                          └─────┬──────┘
                                                │
                                          ┌─────▼──────┐
                                          │ Contract   │
                                          │ Result     │
                                          └─────┬──────┘
                                                │
                                          ┌─────▼──────┐
                                          │ Context    │
                                          │ Value      │
                                          └─────┬──────┘
                                                │
                                          ┌─────▼──────┐
                                          │ CBOR       │
                                          │ Encode     │
                                          └─────┬──────┘
                                                │
                                          ┌─────▼──────┐
                                          │ Packet     │
                                          │ .payload   │
                                          └────────────┘
```

### 2.2 Canonical Wire Format: CBOR

All packet payloads use **CBOR (RFC 7049)** as the canonical wire format.

**Rationale:**
- Compact binary (smaller than JSON)
- Schema-less (no IDL required)
- Native support for binary data (unlike JSON)
- Deterministic encoding (key ordering, no whitespace)

**Decisions:**
- All `Packet.payload` is CBOR-encoded
- All `RuntimeRequest.payload` is CBOR-encoded
- All `ContractInput.arguments` originates from CBOR
- All `ContractResult.data` is CBOR-encoded for network transmission
- JSON is allowed for CLI display only (converted internally)

### 2.3 ContextValue — Recursive Value Type

ContextValue uses a **recursive variant** (similar to `serde_json::Value`, `nlohmann::json`):

```cpp
class ContextValue {
public:
    // Forward declaration for recursive variant
    struct Map;
    struct Array;

    using Value = std::variant<
        std::monostate,     // null
        bool,
        int64_t,
        double,
        std::string,
        Bytes,              // byte string
        Map,                // nested map: string → Value
        Array               // nested array: Value[]
    >;

    struct Map {
        std::unordered_map<std::string, ContextValue> entries;
    };

    struct Array {
        std::vector<ContextValue> items;
    };

    // ── Constructors ──────────────────────────────────────
    ContextValue() = default;
    ContextValue(nullptr_t) : value_(std::monostate{}) {}
    ContextValue(bool v) : value_(v) {}
    ContextValue(int64_t v) : value_(v) {}
    ContextValue(double v) : value_(v) {}
    ContextValue(std::string v) : value_(std::move(v)) {}
    ContextValue(Bytes v) : value_(std::move(v)) {}
    ContextValue(Map v) : value_(std::move(v)) {}
    ContextValue(Array v) : value_(std::move(v)) {}

    // ── Type-safe access ──────────────────────────────────
    template<typename T>
    const T* get() const { return std::get_if<T>(&value_); }

    template<typename T>
    T* get() { return std::get_if<T>(&value_); }

    bool is_null() const { return std::holds_alternative<std::monostate>(value_); }
    bool is_bool() const { return std::holds_alternative<bool>(value_); }
    bool is_int() const { return std::holds_alternative<int64_t>(value_); }
    bool is_double() const { return std::holds_alternative<double>(value_); }
    bool is_string() const { return std::holds_alternative<std::string>(value_); }
    bool is_bytes() const { return std::holds_alternative<Bytes>(value_); }
    bool is_map() const { return std::holds_alternative<Map>(value_); }
    bool is_array() const { return std::holds_alternative<Array>(value_); }

    // ── Serialization ─────────────────────────────────────
    Bytes to_cbor() const;
    static Result<ContextValue> from_cbor(BytesView data);

    // JSON bridge (for CLI/debug):
    std::string to_json() const;
    static Result<ContextValue> from_json(const std::string& json);

private:
    Value value_;
};
```

**CBOR mapping:**

| ContextValue | CBOR Major Type |
|---|---|
| `null` | 7 (value 22) |
| `bool` | 7 (value 20/21) |
| `int64_t` | 0/1 (unsigned/negative) |
| `double` | 7 (float) |
| `string` | 3 (text) |
| `Bytes` | 2 (byte string) |
| `Map` | 5 (map) — recursive, all values are ContextValue |
| `Array` | 4 (array) — recursive, all items are ContextValue |

This matches CBOR's native type system. No flattening, no shared_ptr overhead.

### 2.4 ContractInput Serialization

```cpp
struct ContractInputBuilder {
    static Result<ContractInput> from_cbor(
        const std::string& method,
        BytesView cbor_data)
    {
        auto cv = ContextValue::from_cbor(cbor_data);
        if (!cv) return std::move(cv.error());
        return ContractInput{method, std::move(cv.value())};
    }

    static Result<ContractInput> from_json(
        const std::string& method,
        const std::string& json_data)
    {
        auto cv = ContextValue::from_json(json_data);
        if (!cv) return std::move(cv.error());
        return ContractInput{method, std::move(cv.value())};
    }
};
```

### 2.5 ContractResult Serialization

```cpp
struct ContractResultSerializer {
    static Bytes to_cbor(const ContractResult& result);
    static Result<ContractResult> from_cbor(BytesView data);
};
```

**NextAction CBOR mapping:**

| NextAction | CBOR Tag |
|---|---|
| `ActionDispatchContract` | `{ 1: contract_id, 2: input_cbor }` |
| `ActionDispatchMessage` | `{ 3: opcode, 4: data_binary, 5: target }` |
| `ActionScheduleRetry` | `{ 6: delay_ns, 7: max_retries, 8: backoff }` |
| `ActionEmitEvent` | `{ 9: event_type, 10: payload }` |
| `ActionStoreContext` | `{ 11: key, 12: value_cbor }` |
| `ActionSpawnPlan` | `{ 13: plan_id, 14: params_map }` |
| `ActionNotify` | `{ 15: target, 16: message, 17: metadata }` |
| `ActionCompensate` | `{ 18: plan_id, 19: reason }` |
| `ActionAbort` | `{ 20: reason, 21: trigger_compensation }` |

### 2.6 Schema Validation Layer

Contracts should not self-parse or self-validate their input. Instead, each contract registers a schema, and the runtime validates before dispatch:

```cpp
// Per-contract schema: maps argument name → expected type
struct ContractSchema {
    std::string contract_id;
    std::unordered_map<std::string, ContextValueType> expected_args;
    std::vector<std::string> required_args;

    Result<void> validate(const ContextValue& input) const;
};

// At registration:
kernel.register_contract("system.bootstrap", BootstrapContract{});
kernel.register_schema("system.bootstrap", {
    .expected_args = {
        {"node_id", ContextValueType::String},
        {"public_key", ContextValueType::Bytes},
        {"listen_port", ContextValueType::Int},
    },
    .required_args = {"node_id", "public_key"}
});

// In pipeline (before ContractInputBuilder):
auto schema = registry_.find(contract_id);
if (schema) {
    auto valid = schema->validate(cv);
    if (!valid) return ContractError::validation_failed(valid.error());
}
```

Benefits:
- Contracts never crash on unexpected input — rejected at the validation layer
- Error messages are uniform: "expected int for 'port', got string"
- Adding a new field to a contract is a one-line schema change
- The validation layer can be bypassed for internal (trusted) calls via a flag: `RuntimeRequest::SKIP_VALIDATION`

### 2.7 Packet ↔ RuntimeRequest Pipeline

```cpp
// Network → Runtime direction
struct PacketToRequest {
    static Result<RuntimeRequest> convert(const Packet& pkt, const OpcodeRoute& route)
    {
        RuntimeRequest req;
        req.request_id = generate_uuid();
        req.contract_id = route.contract_id;
        req.opcode = to_hex_string(pkt.opcode_id);
        req.requester = bytes_to_hex(pkt.session_id);
        req.timestamp = pkt.timestamp;

        auto input = ContractInputBuilder::from_cbor(route.method, pkt.payload);
        if (!input) return std::move(input.error());

        req.input = std::move(input.value());
        req.headers["session_id"] = bytes_to_hex(pkt.session_id);
        req.headers["intent_id"] = bytes_to_hex(pkt.intent_id);

        return req;
    }
};

// Runtime → Network direction
struct RequestToPacket {
    static Packet convert(
        const RuntimeResult& result,
        const Packet& original_pkt,
        const std::vector<NextAction>& actions)
    {
        Packet resp;
        resp.opcode_id = derive_response_opcode(original_pkt.opcode_id);
        resp.session_id = original_pkt.session_id;
        resp.intent_id = original_pkt.intent_id;
        resp.timestamp = get_timestamp_ms();

        for (const auto& action : actions) {
            if (auto* msg = std::get_if<ActionDispatchMessage>(&action)) {
                resp.payload = msg->data;
                break;
            }
        }

        if (resp.payload.empty()) {
            resp.payload = ContractResultSerializer::to_cbor(result);
        }

        return resp;
    }
};
```

### 2.7 Content Negotiation

```
Wire: [content_type_byte | cbor_data]
```

| Content Type | Byte | Description |
|---|---|---|
| CBOR (default) | 0x00 | Canonical format |
| JSON | 0x01 | CLI/debug interop |
| MsgPack | 0x02 | Future |
| FlatBuffers | 0x03 | Future |

Sprint 37: only CBOR (0x00) required.

---

## 3. Serialization at Each Boundary

| Boundary | Serialization | Format |
|----------|--------------|--------|
| Wire → Packet | `packet_from_buffer()` | Binary framing |
| Packet → ContextValue | `ContextValue::from_cbor()` | CBOR |
| ContextValue → Validated Input | `ContractSchema::validate()` | Schema check |
| Validated Input → ContractInput | `ContractInputBuilder` | In-memory |
| ContractInput → Contract | Direct struct | In-memory |
| Contract → ContractResult | Direct struct | In-memory |
| ContractResult → CBOR | `ContractResultSerializer::to_cbor()` | CBOR |
| CLI display | `ContextValue::to_json()` | JSON |

---

## 4. Consequences

### Positive
- **Single serialization path** — no ad-hoc parsing per contract
- **CBOR is compact** — smaller than JSON over the wire
- **Binary native** — CBOR handles `Bytes` without base64
- **Deterministic encoding** — canonical CBOR ensures consistent IDs
- **Recursive variant** — no shared_ptr overhead, value semantics
- **Contract agnostic** — contracts never touch CBOR; they see `ContextValue` directly
- **Schema validation** — contracts never crash on malformed input; validation is centralized
- **Same model as serde_json / nlohmann** — familiar pattern

### Negative
- **CBOR library required** — must add or implement
- **Conversion overhead** — decode/encode per packet
- **Schema registry** — must maintain schema definitions per contract
- **Recursive variant requires wrapper struct** — C++ limitation

---

## 5. Migration Path

1. Rewrite `ContextValue` with recursive variant (Map/Array wrappers)
2. Implement `to_cbor()` / `from_cbor()` using a CBOR library
3. Add `to_json()` / `from_json()` bridge
4. Add `ContractInputBuilder` and `ContractResultSerializer`
5. Add `ContractSchema` type and `SchemaRegistry` in RuntimeKernel
6. Register schemas for all existing contracts (Join, Bootstrap, Governance, Recovery, File, Process)
7. Wire schema validation into `PacketToRequest` pipeline (before ContractInputBuilder)
8. Add `PacketToRequest` and `RequestToPacket` converters
9. Update `RuntimeBridge::bridge()` to use serialization pipeline
10. Update CLI to use `to_json()` for display

---

## 6. Files Affected

| File | Change |
|------|--------|
| `core/runtime/runtime_types.hpp` | Rewrite ContextValue with recursive variant, add CBOR/JSON methods |
| `core/runtime/runtime_types.cpp` | Implement to_cbor / from_cbor / to_json / from_json |
| `core/runtime/runtime_kernel.hpp` | Add SchemaRegistry + register_schema() |
| `core/runtime/runtime_kernel.cpp` | Wire schema validation into dispatch |
| `core/runtime/runtime_bridge.hpp` | Use PacketToRequest / RequestToPacket |
| `core/runtime/runtime_bridge.cpp` | Wire serialization + validation pipeline |
| `protocol/packet/packet.cpp` | Add content-type byte support (optional) |

---

## References

- [RFC 0036 — Contract ABI Freeze](../RFC/0036-contract-abi-freeze.md)
- [RFC 0039 — NextAction Model](../RFC/0039-nextaction-model.md)
- [RFC 0041 — Runtime Bridge](../RFC/0041-runtime-bridge.md)
- [CBOR RFC 7049](https://tools.ietf.org/html/rfc7049)
