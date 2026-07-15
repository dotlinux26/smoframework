# RFC 0026 — Contract ABI Specification

| Field | Value |
|---|---|
| Status | Draft |
| Author | SMO Architecture |
| Date | 2026-07-16 |
| Supersedes | — |

## Summary

This RFC defines the **Contract ABI** (Application Binary Interface) — a
canonical format for describing a contract's inputs, outputs, capabilities,
dependencies, and version bounds. The ABI carries a content-addressed **ABI
Hash** that enables runtime verification without re-parsing the contract body.

## Motivation

SMO currently has `ContractDefinition` with fields, but no formal ABI layer.
Without it:

- The compiler cannot validate whether an intent's parameters match what the
  contract expects
- The executor cannot verify that a contract's capability requirements are
   satisfied without parsing the full JSON
- The registry cannot detect incompatible contract updates
- SDK generators (Python, Rust, CLI) have no schema to generate bindings from

## Contract ABI Format

### Top-Level Structure

```json
{
    "abi_version": 1,
    "contract_id": "abc123...",
    "contract_name": "ping",
    "category": "kernel",
    "input": { ... },
    "output": { ... },
    "capability_mask": "0000000f",
    "opcode_dependencies": ["LS", "GET"],
    "abi_hash": "def456...",
    "semantic_hash": "789abc...",
    "min_runtime_version": "3.0.0",
    "max_runtime_version": "3.255.255"
}
```

### Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `abi_version` | uint8 | yes | ABI format version. Current = 1 |
| `contract_id` | string | yes | 64-char hex ContractID |
| `contract_name` | string | yes | Canonical name, e.g., "ping", "ls" |
| `category` | enum | yes | "kernel", "native", "mesh", "private" |
| `input` | Schema | yes | Input parameter schema |
| `output` | Schema | yes | Output result schema |
| `capability_mask` | string | yes | Hex-encoded 64-bit capability bitmask |
| `opcode_dependencies` | string[] | optional | Opcodes this contract invokes |
| `abi_hash` | string | yes | BLAKE3(canonical_json of ABI excluding abi_hash and semantic_hash) |
| `semantic_hash` | string | yes | BLAKE3(abi_hash + canonical_json of contract body) |
| `min_runtime_version` | semver | yes | Minimum SMO runtime version required |
| `max_runtime_version` | semver | yes | Maximum SMO runtime version allowed |

### Schema Format

```json
{
    "type": "object",
    "properties": {
        "path": {
            "type": "string",
            "description": "Target file path",
            "required": true
        },
        "recursive": {
            "type": "boolean",
            "description": "Recurse into subdirectories",
            "required": false,
            "default": false
        }
    }
}
```

Supported types: `string`, `boolean`, `integer`, `number`, `array`, `object`,
`bytes` (hex-encoded), `contract_id` (64-char hex), `node_id` (fingerprint),
`socket_addr` (ip:port).

### Hashing

```
ABI Hash = BLAKE3(canonical_json(abi_version + contract_name + category + input + output + capability_mask + opcode_dependencies + min_runtime_version + max_runtime_version))
Semantic Hash = BLAKE3(abi_hash || canonical_json(contract_body))
ContractID = BLAKE3(canonical_json(full_contract))
```

Where `canonical_json` means:
- Sorted keys
- No whitespace
- No trailing newline

### Hash Linkage

```
ContractID
    ↕  (verifies contract body integrity)
Semantic Hash
    ↕  (verifies ABI + body consistency)
ABI Hash
    ↕  (verifies interface compatibility)
Runtime check: ABI Hash match → no re-parse needed
```

If the ABI Hash matches, the runtime can safely use the cached compiled DAG
without re-validating parameters.

## ABI Registry

The runtime maintains an in-memory ABI registry:

```cpp
class AbiRegistry {
public:
    void register_abi(const ContractABI& abi);
    std::optional<ContractABI> get_abi(const ContractID& id) const;
    bool verify_compatibility(const ContractID& id, const ExecutionContext& ctx) const;
    Hash256 compute_abi_hash(const ContractABI& abi) const;
};
```

The ABI registry is populated:
1. At registration time (kernel/native contracts)
2. At publish time (mesh/private contracts)
3. On registry sync (from remote peers)

## Version Compatibility

| Runtime ABI | Contract ABI | Result |
|-------------|--------------|--------|
| 1 | 1 | Compatible |
| 1 | 2 | Incompatible — reject |
| 2 | 1 | Compatible (backward compatible) |

`min_runtime_version` and `max_runtime_version` are checked at dispatch time:

```cpp
if (runtime_version < abi.min_runtime_version) reject;
if (runtime_version > abi.max_runtime_version) reject;
```

## SDK Code Generation

The ABI serves as the schema for SDK bindings:

```
Contract ABI
    ↓
Code Generator
    ↓
    ├── C++ SDK: struct PingInput { std::string path; bool recursive; };
    ├── Python SDK: class PingInput(TypedDict): ...
    ├── CLI: smoctl ping --path <path> [--recursive]
    └── Rust SDK: struct PingInput { path: String, recursive: Option<bool> };
```

## Files Changed

| File | Change |
|------|--------|
| `core/contract/contract_abi.hpp` | New — `ContractABI`, `Schema`, `AbiRegistry` |
| `core/contract/contract_abi.cpp` | New — `compute_abi_hash()`, `compute_semantic_hash()`, JSON ABI serialization |
| `core/contract/CMakeLists.txt` | Add contract_abi.cpp |
| `SPEC.md` | Add ABI section |

## Migration

No breaking changes. Existing contracts that lack an ABI are assigned a
default ABI at registration time with `abi_version=1` and empty input/output
schemas. All new contracts MUST carry an explicit ABI.
