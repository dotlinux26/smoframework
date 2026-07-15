# RFC 0023 — Contract Architecture (v2)

## Status
DRAFT — pending review.

## Problem
The original Contract Model (RFC 0001) treats contracts as JSON/YAML intent objects exchanged between three parties (Requester, Responder, Witness). This model conflates user intent with contract definition, has no formal lifecycle, no registry, no compiler boundary, and no separation between system-builtin operations and user-defined workflows. As SMO grows from MVP to a programmable mesh, we need a contract architecture that:

1. Separates **Intent** (what a user wants) from **Contract** (how it is fulfilled).
2. Defines a **Contract Registry** for publishing, discovering, and verifying contract definitions.
3. Defines a **Compiler** that transforms a Contract + Parameters into an executable DAG.
4. Defines an **Opcode Registry** for discovering available operations across the mesh.
5. Keeps compilation **node-local** so heterogeneous nodes (different plugins, architectures, OS) produce valid DAGs.
6. Makes the registry **immutable and append-only** — no overwrite, no deletion, no silent update.

## Decisions

### 1. Layered Model: User → Intent → Contract → Compiler → DAG → Executor

```
                        ┌──────────────────┐
                        │   User (CLI/GUI) │
                        └────────┬─────────┘
                                 │ expresses goal
                                 ▼
                        ┌──────────────────┐
                        │     Intent       │  (what: opcode + targets + params)
                        └────────┬─────────┘
                                 │ resolved by
                                 ▼
                        ┌──────────────────┐
                        │ Contract Factory │  (maps Intent → ContractID)
                        └────────┬─────────┘
                                 │
                        ┌────────▼─────────┐
                        │    Contract      │  (canonical definition loaded from Registry)
                        └────────┬─────────┘
                                 │ compiled by
                                 ▼
                        ┌──────────────────┐
                        │    Compiler      │  (node-local: Contract + Env → DAG)
                        └────────┬─────────┘
                                 │
                        ┌────────▼─────────┐
                        │       DAG        │  (immutable, cached locally)
                        └────────┬─────────┘
                                 │ executed by
                                 ▼
                        ┌──────────────────┐
                        │    Executor      │  (DAG-aware scheduler)
                        └──────────────────┘
```

**Key boundary:** The user never touches a Contract directly. All entry points (CLI, REST, GUI, SDK) produce an **Intent**. The Contract Factory resolves the Intent to a ContractID. The Compiler loads the Contract definition from the local Registry cache, compiles it into a DAG, and hands the DAG to the Executor.

### 2. Three Contract Categories

| Category | Nature | Storage | Lifecycle | Examples |
|---|---|---|---|---|
| **Native** | Runtime-builtin template | In code (`contract/registry/native.cpp`), registered at startup | Fixed for SMO version; updated only via runtime upgrade | `ls`, `put`, `get`, `exec`, `quarantine` |
| **User-defined** (previously "Mesh") | Published by a mesh participant | Contract Registry (local DB), replicated between nodes | Draft → Publish → Registry → Node Compile → Cache DAG → Execute → Deprecate | Incident playbooks, compliance checks, custom workflows |
| **Internal** (previously "Private") | Runtime-only, never user-invokable | Not in Registry; instantiated ad-hoc by system components | Created and destroyed within a single session | Session teardown, trust recalculation, heartbeat response |

**Naming rationale:** "Native" is clearer than "System" (implies built into the runtime). "User-defined" is clearer than "Mesh" (emphasizes who creates it). "Internal" replaces "Private" (avoids confusion with private-key/private-data).

### 3. Contract Definition Schema

Every Contract (all three categories) is represented as a **canonical JSON object**:

```json
{
  "contract_version": "1.0",
  "category": "native",
  "opcode": "ls",
  "name": "List Directory",
  "description": "List files and directories at the specified path",
  "publisher": "00000000-0000-0000-0000-000000000000",
  "semver": "1.0.0",
  "parameters": {
    "path": {
      "type": "string",
      "required": true,
      "description": "Absolute path to list"
    },
    "recursive": {
      "type": "boolean",
      "required": false,
      "default": false,
      "description": "List recursively"
    }
  },
  "capabilities_required": {
    "filesystem_read": 1
  },
  "compiler_hints": {
    "max_parallelism": 1,
    "timeout_sec": 30,
    "idempotent": true
  },
  "signature": null
}
```

**ContractID** = `Blake3(utf8(canonical_json))`, encoded as lowercase hex (64 hex chars).

#### Canonical JSON rules
1. Keys are sorted lexicographically.
2. No whitespace beyond what JSON requires.
3. No trailing newline.
4. UTF-8 without BOM.

#### Field definitions

| Field | Type | Required | Description |
|---|---|---|---|
| `contract_version` | string (semver) | yes | Schema version of the contract definition format |
| `category` | string | yes | One of: `native`, `user_defined`, `internal` |
| `opcode` | string | yes | Primary opcode this contract implements |
| `name` | string | yes | Human-readable name (max 128 chars) |
| `description` | string | no | Human-readable description (max 2048 chars) |
| `publisher` | string (UUID) | yes | NodeID (UUID) of the publisher; zero-UUID for native |
| `semver` | string (semver) | yes | Version of this contract definition |
| `parameters` | object | yes | Parameter schema (JSON Schema subset) |
| `capabilities_required` | object | yes | Capability → level map required to execute |
| `compiler_hints` | object | yes | Hints for the compiler/executor |
| `signature` | string (Base64) | conditional | Publisher's signature over the canonical JSON; null for native |

#### Signature scheme (user-defined contracts only)
- Signer computes `Blake3(utf8(canonical_json))` to get `ContractID`.
- Signer signs `ContractID` with their Ed25519 private key.
- `signature` field is the Base64-encoded signature.
- Verification: compute `ContractID` from canonical JSON, verify signature against publisher's public key.

### 4. ContractID

```
ContractID = Blake3(utf8(canonical_json))
```

**Properties:**
- **Content-addressed:** Same canonical definition always produces the same ContractID, regardless of node/publisher/network. This enables deduplication and trustless verification.
- **Immutable:** Changing any field (even a description typo) produces a different ContractID. There is no "update in place" — only publish a new version with a new ContractID.
- **64 hex chars** (256 bits), matching SMO's Blake3-256 hash usage throughout the protocol.
- **Used as the primary key** in the Contract Registry, DAG cache, and Intent resolution.

### 5. Opcode Registry

The Opcode Registry is a **syscall-table-like registry** mapping OpcodeID → handler metadata. It lives in `core/opcode/` and is populated at startup from two sources:

1. **Builtin opcodes**: hardcoded in `opcode.h` (LS=0x01, PUT=0x02, etc.)
2. **Plugin opcodes**: registered by loaded plugins via `register_opcode()` at init time

Each entry:

```cpp
struct OpcodeEntry {
    Opcode       id;           // 0x01–0xEF builtin, 0xF0–0xFE plugin, 0xFF custom
    std::string  name;         // "ls", "put", "get", ...
    std::string  semver;       // opcode version
    uint32_t     capability_mask;
    bool         idempotent;
    std::string  contract_id;  // ContractID of the handler contract (native or user-defined)
    std::string  plugin_id;    // empty for builtin, plugin UUID for plugin opcodes
    std::vector<std::string> supported_arches; // e.g. {"x86_64", "aarch64"}
};
```

**Opcodes and Contracts are 1:N.** A single opcode (e.g. `ls`) can have multiple contract implementations (native `ls` v1.0.0, user-defined `ls-with-acl` v2.1.0). The Contract Factory selects which ContractID to use based on:
- The Intent's opcode
- The Intent's `contract_hint` (optional, user can specify preferred ContractID)
- The node's local Registry state (latest compatible version)

**Opcode range allocation:**
| Range | Owner | Registration |
|---|---|---|
| 0x01–0xEF | Builtin SMO opcodes | Hardcoded in `opcode.h` |
| 0xF0–0xFA | Reserved for future builtin | — |
| 0xFB–0xFE | Plugin opcodes | `register_opcode()` at plugin load |
| 0xFF | Custom/user-defined | Reserved for dynamic dispatch |

### 6. Contract Registry

The Contract Registry is **Git-like, not Docker-like**. It is:

- **Immutable:** Once a ContractID is written, it cannot be deleted or overwritten.
- **Append-only:** New contracts are appended; old ones remain for audit and replay.
- **Blake3-addressed:** The key is always `ContractID`.
- **Local-first:** Each node maintains its own Registry DB. There is no global "mesh registry" — nodes sync contract definitions via existing transport (pull-based, not broadcast).
- **Verifiable:** Every user-defined contract carries a publisher signature. Nodes verify before storing.

#### Registry storage schema

The Registry is stored in the existing `SqliteStore` under a dedicated table:

```sql
CREATE TABLE IF NOT EXISTS contract_registry (
    contract_id     TEXT PRIMARY KEY,  -- 64-char hex Blake3 hash
    canonical_json  TEXT NOT NULL,      -- canonical JSON of the contract definition
    category        TEXT NOT NULL,      -- 'native', 'user_defined', 'internal'
    opcode          TEXT NOT NULL,
    name            TEXT NOT NULL,
    publisher       TEXT NOT NULL,      -- UUID; zero-UUID for native
    semver          TEXT NOT NULL,
    parameters      TEXT NOT NULL,      -- JSON schema
    capabilities    TEXT NOT NULL,      -- JSON object
    compiler_hints  TEXT NOT NULL,      -- JSON object
    signature       TEXT,               -- Base64 signature; NULL for native/internal
    published_at    INTEGER NOT NULL,   -- Unix nanoseconds
    status          TEXT NOT NULL DEFAULT 'active',  -- 'active', 'deprecated', 'revoked'
    deprecation_note TEXT
);

CREATE INDEX idx_registry_opcode ON contract_registry(opcode);
CREATE INDEX idx_registry_publisher ON contract_registry(publisher);
```

#### Registry operations

```
Publish(definition, signature) → ContractID
  - Verifies canonical JSON
  - Computes ContractID = Blake3(definition)
  - Verifies signature matches publisher
  - Checks ContractID does not already exist (immutable — reject duplicate)
  - Inserts into registry
  - Returns ContractID

Resolve(opcode, version_constraint) → ContractID
  - Queries registry for matching opcode + semver constraint
  - Returns the latest compatible active contract
  - Falls back to native contract if no user-defined match

Get(contract_id) → ContractDefinition
  - Loads canonical JSON by ContractID
  - Returns parsed ContractDefinition struct

Deprecate(contract_id, reason) → void
  - Sets status = 'deprecated'
  - Requires publisher signature or governance Level 2+ proposal

Sync(peer_endpoint, since_timestamp) → [ContractEntry]
  - Pulls new contract definitions from peer since given timestamp
  - Each entry is independently verified before local insert
```

#### Sync strategy

Contract definitions are **not broadcast**. Sync is:
1. **Pull-based:** Node A requests new contracts from Node B since a given timestamp.
2. **Verification:** Each received entry is verified (signature check, canonical JSON validation) before inserting.
3. **No DAG sync:** Only the canonical definition is synced. DAGs are never transferred between nodes.
4. **Trust:** A node only syncs from peers it trusts (trust score ≥ configured threshold).

### 7. Contract Lifecycle

```
        ┌──────────┐
        │  Draft   │  (local, not published)
        └────┬─────┘
             │ publish
             ▼
        ┌──────────┐
        │ Published│  (in Registry, immutable)
        └────┬─────┘
             │ first node fetches
             ▼
        ┌──────────┐
        │  Synced  │  (verified, stored locally)
        └────┬─────┘
             │ compile (node-local)
             ▼
        ┌──────────┐
        │  Cached  │  (DAG stored in dag_store)
        └────┬─────┘
             │ execute
             ▼
        ┌──────────┐
        │ Executed │  (run by Executor)
        └──────────┘

Post-execution paths:
        ┌──────────┐
        │ Deprecated│← publisher or governance
        └──────────┘

        ┌──────────┐
        │  Revoked │← emergency governance (Level 3+)
        └──────────┘
```

**Transitions:**
| From | To | Trigger |
|---|---|---|
| Draft | Published | Publisher signs and calls `Publish()` |
| Published | Synced | Remote node calls `Sync()` |
| Synced | Cached | Compiler produces DAG; stored in `dag_store` |
| Published | Deprecated | Publisher signs deprecation, or governance proposal |
| Deprecated | Revoked | Emergency governance (Level 3+ threshold) |

### 8. Compiler Boundary

**Compilation is node-local.** This is a hard architectural invariant.

```
                    Contract Definition (canonical JSON)
                               │
                               ▼
                    ┌─────────────────────┐
                    │    Node Compiler     │
                    │                     │
                    │  Inputs:            │
                    │  • Contract def      │
                    │  • Intent params     │
                    │  • Node environment: │
                    │    - Plugin versions  │
                    │    - OS/arch          │
                    │    - Available ops    │
                    │    - Policy constraints │
                    │                     │
                    │  Output:            │
                    │  • DAG (immutable)   │
                    └─────────────────────┘
                               │
                               ▼
                    ┌─────────────────────┐
                    │   DAG Cache          │
                    │   (key: ContractID + │
                    │    env fingerprint)  │
                    └─────────────────────┘
```

**Why node-local?**
- Nodes have different plugin sets (one node may have `ml-detect` v1.2, another v2.0).
- Nodes run different architectures (x86_64 vs aarch64) — compiled steps differ.
- Nodes have different policy constraints (one mesh allows `exec`, another restricts it).
- DAGs are derived from environment; a DAG valid on node A may be invalid on node B.
- The compiler is deterministic given the same inputs, but inputs vary per node.

**Compiler interface:**

```cpp
struct CompileInput {
    ContractDefinition  contract;
    std::string         parameters_json;   // from Intent
    NodeEnvironment     environment;       // os, arch, plugin versions, policy
};

struct CompileOutput {
    DAG                 dag;
    std::string         env_fingerprint;   // Blake3 hash of environment snapshot
    uint64_t            compiled_at;       // Unix nanoseconds
};

Result<CompileOutput> compile(const CompileInput& input);
```

### 9. DAG Cache Strategy

The DAG cache maps `(ContractID, env_fingerprint) → DAG`.

```
Cache key format: Blake3(ContractID + "|" + env_fingerprint)
```

**Rules:**
1. **Cache hit:** If a compiled DAG exists for the exact `(ContractID, env_fingerprint)` pair, return it without recompiling.
2. **Cache miss:** Compile, store DAG in `dag_store`, return.
3. **Cache invalidation:** When node environment changes (plugin installed/removed, policy updated), increment a local `env_epoch`. The `env_fingerprint` incorporates `env_epoch`, so old cached DAGs are naturally bypassed.
4. **Eviction:** LRU eviction when the DAG cache exceeds a configured limit (default 1000 DAGs). The canonical definition remains in the Registry forever; only the compiled DAG is evictable.
5. **No DAG sync:** DAGs are never transferred between nodes. Each node compiles independently.

#### DAG Cache storage (in `dag_store`)

```sql
CREATE TABLE IF NOT EXISTS dag_cache (
    cache_key       TEXT PRIMARY KEY,  -- Blake3(ContractID + env_fingerprint)
    contract_id     TEXT NOT NULL,
    env_fingerprint TEXT NOT NULL,
    dag_json        TEXT NOT NULL,     -- serialized DAG
    compiled_at     INTEGER NOT NULL,
    last_accessed   INTEGER NOT NULL
);

CREATE INDEX idx_dag_cache_contract ON dag_cache(contract_id);
```

### 10. Intent → Contract Resolution Flow

```
User runs: smo exec ls --path /tmp

1. CLI produces Intent:
   {
     "opcode": "ls",
     "targets": ["node-abc"],
     "parameters": {"path": "/tmp"},
     "scope": "single",
     "trust_min": 0.5
   }

2. Contract Factory resolves:
   a. Look up ContractID for opcode "ls":
      - Check if user specified contract_hint (they didn't)
      - Query registry for latest active "ls" contract
      - Fall back to native contract if no user-defined match
   b. Return ContractID = Blake3(native_ls_canonical_json)

3. Compiler loads Contract definition by ContractID:
   a. Check local dag_cache for (ContractID, env_fingerprint)
   b. Cache miss → compile Contract + parameters → DAG
   c. Store DAG in dag_cache

4. Executor runs DAG:
   a. Scheduler traverses DAG respecting dependencies
   b. Each task invokes the corresponding opcode handler
   c. Results collected and returned to user
```

### 11. Native Contract Registration

Native contracts are **Contract Templates** registered at startup:

```cpp
// In contract/registry/native.cpp (called at SMO startup)
void register_native_contracts(ContractRegistry& registry) {
    registry.register_native({
        .opcode = "ls",
        .name = "List Directory",
        .semver = "1.0.0",
        .parameters = R"({...})"_json,
        .capabilities = {{"filesystem_read", 1}},
        .compiler_hints = {.max_parallelism = 1, .timeout_sec = 30, .idempotent = true},
        .handler = &native_ls_handler,  // function pointer for executor
    });
    // ... more native contracts
}
```

A native contract:
- Has a `handler` function pointer that the Executor calls directly (no compilation needed — the DAG is a single-node trivial DAG).
- Still has a ContractID (computed from its canonical JSON).
- Is registered in the Registry at startup with `category = "native"`.
- Cannot be published, deprecated, or revoked by users.
- Is updated only by upgrading the SMO runtime.

**Native contracts are visible in the Registry just like user-defined contracts.** The only differences:
- `publisher` = zero UUID
- `signature` = null
- `handler` field exists in the in-memory representation (not in canonical JSON)
- Cannot be synced (every node has them built-in)

## Interfaces

```cpp
// ─── Contract Definition ────────────────────────────────────────

struct ContractParameter {
    std::string name;
    std::string type;           // "string", "boolean", "integer", "number", "array", "object"
    bool        required;
    std::optional<std::string> default_value;
    std::string description;
};

struct CompilerHints {
    uint32_t    max_parallelism{1};
    uint32_t    timeout_sec{30};
    bool        idempotent{false};
};

struct ContractDefinition {
    std::string                         contract_version;
    std::string                         category;          // "native", "user_defined", "internal"
    std::string                         opcode;
    std::string                         name;
    std::string                         description;
    std::string                         publisher;         // UUID
    std::string                         semver;
    std::vector<ContractParameter>      parameters;
    std::map<std::string, uint32_t>     capabilities_required;
    CompilerHints                       compiler_hints;
    std::string                         signature;         // Base64 or empty
    ContractID                          contract_id;       // computed, not in canonical JSON

    static Result<ContractDefinition> from_canonical_json(std::string_view json);
    std::string to_canonical_json() const;
    ContractID compute_id() const;   // Blake3(to_canonical_json())
};

// ─── ContractID ─────────────────────────────────────────────────

struct ContractID {
    std::string hex;                 // 64 hex chars

    bool operator==(const ContractID& other) const;
    bool operator<(const ContractID& other) const;
    static Result<ContractID> from_hex(std::string_view hex);
    static ContractID compute(std::string_view canonical_json);
};

// ─── Intent ─────────────────────────────────────────────────────

// Note: Intent already exists in core/intent/intent.h.
// Extending it with contract_hint:
struct Intent {
    IntentId          id;
    Opcode            opcode;
    std::string       requester;
    std::string       responder;
    std::string       witness;
    std::string       scope;            // "single", "mesh"
    std::vector<std::string> targets;
    double            trust_min{0.0};
    int32_t           parallelism{1};
    int64_t           created_at{0};
    std::string       parameters_json;  // opcode-specific JSON blob

    // NEW: Optional contract hint — user can specify preferred ContractID
    std::string       contract_hint;    // 64-char hex ContractID, or empty for "best match"
};

// ─── Contract Factory ───────────────────────────────────────────

class ContractFactory {
public:
    ContractFactory(ContractRegistry& registry, OpcodeRegistry& opcodes);

    // Resolve Intent → ContractDefinition
    Result<ContractDefinition> resolve(const Intent& intent);

    // Resolve by explicit ContractID
    Result<ContractDefinition> resolve_by_id(const ContractID& id);

private:
    ContractRegistry& registry_;
    OpcodeRegistry&   opcodes_;
};

// ─── Opcode Registry ────────────────────────────────────────────

struct OpcodeEntry {
    Opcode       id;
    std::string  name;
    std::string  semver;
    uint32_t     capability_mask;
    bool         idempotent;
    std::string  contract_id;     // default ContractID for this opcode
    std::string  plugin_id;
    std::vector<std::string> supported_arches;
};

class OpcodeRegistry {
public:
    OpcodeRegistry();

    // Builtin registration
    void register_builtin(Opcode code, std::string_view name,
                          uint32_t capability_mask, bool idempotent);

    // Plugin registration
    Result<void> register_opcode(OpcodeEntry entry);

    // Lookup
    Result<OpcodeEntry> resolve(Opcode code) const;
    Result<OpcodeEntry> resolve_by_name(std::string_view name) const;

    // Iteration
    std::vector<OpcodeEntry> all() const;

private:
    std::unordered_map<Opcode, OpcodeEntry> entries_;
};

// ─── Contract Registry ──────────────────────────────────────────

class ContractRegistry {
public:
    explicit ContractRegistry(StorageBackend& storage);

    // Publish a user-defined contract
    Result<ContractID> publish(const std::string& canonical_json,
                               const std::string& signature,
                               const std::string& publisher_id);

    // Register a native contract at startup
    Result<ContractID> register_native(std::string_view canonical_json,
                                       OpcodeHandler handler);

    // Resolve best contract for opcode + optional version constraint
    Result<ContractDefinition> resolve(std::string_view opcode,
                                       std::string_view version_constraint = "");

    // Get by ContractID
    Result<ContractDefinition> get(const ContractID& id);

    // Get by ContractID (hex string)
    Result<ContractDefinition> get(std::string_view contract_id_hex);

    // Deprecate
    Result<void> deprecate(const ContractID& id, std::string_view reason,
                           std::string_view publisher_signature);

    // Sync from peer
    Result<std::vector<ContractID>> sync(NodeID peer, uint64_t since_timestamp);

    // List contracts matching criteria
    Result<std::vector<ContractDefinition>> list(std::string_view opcode = "",
                                                  std::string_view category = "",
                                                  std::string_view status = "active");

private:
    StorageBackend& storage_;
};

// ─── Compiler ───────────────────────────────────────────────────

struct NodeEnvironment {
    std::string os;
    std::string arch;
    uint64_t    env_epoch;
    std::map<std::string, std::string> plugin_versions;
    std::vector<std::string> enabled_policies;

    std::string fingerprint() const;  // Blake3 hash of all fields
};

struct CompileInput {
    ContractDefinition  contract;
    std::string         parameters_json;
    NodeEnvironment     environment;
};

struct CompileOutput {
    DAG                 dag;
    std::string         env_fingerprint;
    uint64_t            compiled_at;
};

class Compiler {
public:
    explicit Compiler(DagStore& dag_store);

    Result<CompileOutput> compile(const CompileInput& input);
    Result<std::optional<DAG>> cache_get(const ContractID& id,
                                         const std::string& env_fingerprint);

private:
    DagStore& dag_store_;
    Result<DAG> compile_internal(const CompileInput& input);
};

// ─── Native Contract Handler ────────────────────────────────────

using OpcodeHandler = Result<ExecutionResult>(const ExecutionContext& ctx);

struct NativeContractRegistration {
    std::string_view   opcode;
    std::string_view   name;
    std::string_view   semver;
    json               parameters;
    CapabilityMap      capabilities;
    CompilerHints      hints;
    OpcodeHandler*     handler;
};
```

## Consequences

### Positive
- **Clear separation of concerns:** Intent, Contract, Compiler, Executor are independent modules with defined interfaces.
- **Local compilation** enables heterogeneous nodes without mesh-wide coordination.
- **Immutable registry** guarantees auditability — contracts cannot be silently modified.
- **Content-addressed** ContractID enables deduplication and trustless verification.
- **Opcode Registry** provides a single source of truth for available operations.
- **Native contracts as templates** simplifies the model — all contracts share the same schema, just with different registration paths.
- **DAG cache** avoids recompilation on repeated executions with the same environment.
- **Pull-based sync** avoids broadcast overhead and lets nodes control their trust exposure.

### Negative
- **No "update in place":** Fixing a typo in a contract description requires publishing a new version. Users must update their Intent to reference the new ContractID.
- **Local compilation cost:** Each node compiles independently. For complex contracts with many dependencies, compile time could be significant. Mitigated by DAG cache.
- **Registry sync latency:** A new contract is not immediately available on all nodes. Nodes must discover and pull it. For time-sensitive operations, the publisher must ensure target nodes have synced first.
- **Storage overhead:** Both the canonical JSON (in Registry) and the compiled DAG (in cache) are stored. For large contracts, this doubles storage. Mitigated by DAG cache eviction.

### Migration from RFC 0001
- RFC 0001's "contract is JSON/YAML intent" is superseded: Intent is now separate from Contract.
- RFC 0001's three-party model (Requester, Responder, Witness) remains valid at the Intent/Execution layer but is not part of the Contract definition itself.
- RFC 0005's DAG model remains valid: the Compiler produces DAGs as defined in RFC 0005.
- Existing code in `core/intent/` needs extension (add `contract_hint`).
- `core/opcode/opcode.h` needs expansion into a full OpcodeRegistry class.
- New directories: `contract/`, `contract/registry/`, `contract/compiler/`.

### Open Questions
1. Should the Registry support version constraints (e.g. `^1.2.0`) during `resolve()`, or just "latest compatible"?
2. Should the Intent carry an explicit `contract_hint` field (as proposed) or should resolution be entirely opaque to the user?
3. Should the Compiler support plugins (e.g. WASM-based compilers for user-defined DSLs) at launch or post-MVP?
4. What is the exact `env_epoch` invalidation trigger? On plugin install only, or on policy change too?
5. Should `sync()` be automatic (background polling every N seconds) or explicit (user-invoked before execution)?
