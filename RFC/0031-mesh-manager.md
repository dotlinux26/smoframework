# RFC 0031 — Mesh Manager

**Status:** ACCEPTED  
**Date:** 2026-07-16  
**Authors:** dotlinux26, D-O-T-Solutions

---

## Summary

This RFC defines the **Mesh Manager** — the component responsible for managing multiple logical meshes within a single SMO Runtime instance. Each mesh is a completely isolated execution environment with its own certificates, peer tables, audit logs, and contracts.

---

## Motivation

A single physical node may participate in multiple logical meshes simultaneously:

```
Laptop Admin
┌─────────────────────────────────────┐
│ SMO Runtime                         │
│  ┌─────────────┐ ┌─────────────┐    │
│  │ Mesh: SOC   │ │ Mesh: Prod  │    │
│  │ cert, peers │ │ cert, peers │    │
│  │ audit.db    │ │ audit.db    │    │
│  │ contracts   │ │ contracts   │    │
│  └─────────────┘ └─────────────┘    │
└─────────────────────────────────────┘
```

Each mesh is **completely isolated** — no shared state between meshes.

---

## Architecture

```
Runtime
│
├── MeshManager
│   ├── Mesh: SOC
│   │     ├── Cert: mesh_soc.smoc
│   │     ├── PeerStore: peers.db
│   │     ├── Audit: audit.db
│   │     ├── Contracts: contracts/
│   │     └── Config: mesh.json
│   │
│   ├── Mesh: Production
│   │     ├── Cert: mesh_prod.smoc
│   │     ├── PeerStore: peers.db
│   │     ├── Audit: audit.db
│   │     └── ...
│   │
│   └── Mesh: Lab
│         └── ...
│
├── MeshManager
│   ├── create_mesh()
│   ├── switch_mesh()
│   ├── join_mesh()
│   ├── leave_mesh()
│   └── list_meshes()
│
└── Services (per-mesh)
    ├── MeshContext
    │     ├── PeerStore
    │     ├── AuditStore
    │     ├── ContractRegistry
    │     ├── PolicyEngine
    │     ├── DiscoveryEngine
    │     ├── HeartbeatService
    │     ├── GossipEngine
    │     ├── ExecutionEngine
    │     ├── Scheduler
    │     ├── Selector
    │     └── HistoryService
```

---

## Mesh Identity

### MeshID

```
MeshID = BLAKE3(RootPublicKey || CreatedAtMs || Random32)
```

- **Globally unique** — collision resistant
- **Immutable** — cannot be changed after creation
- **Used as** directory name, DB prefix, network identifier

### Mesh Name

```
Display Name: "SOC-Production"
MeshID: "a1b2c3d4e5f6..."
```

- Human-readable label
- Changeable via governance
- Used in CLI prompts, UI

---

## Mesh Lifecycle

### Creation

```bash
smo mesh create --name soc --display-name "SOC Production"
```

1. Generate Mesh Root Keypair (Ed25519)
2. Generate First Authority Keypair
3. Root signs Authority Certificate
4. Compute MeshID = BLAKE3(RootPK || time || random32)
5. Export Recovery Package (AES-256-GCM, password-protected)
6. Store Authority Cert in `~/.smo/meshes/<mesh_id>/cert.smoc`
7. Create mesh directories:
   ```
   ~/.smo/meshes/<mesh_id>/
   ├── mesh.json
   ├── cert.smoc
   ├── peers.db
   ├── audit.db
   ├── contract.db
   ├── contracts/
   ├── cache/
   ├── dumps/
   └── workflow/
   ```

### Join

```bash
smo mesh join --mesh <mesh_id> --seed <seed_node_address>
```

1. Connect to seed node (TCP)
2. Send HELLO (NodeID, MeshID, capabilities)
3. Receive WELCOME (MeshID, AuthorityCert, PeerTable)
4. Validate MeshID matches expected
5. Verify AuthorityCert chain to Root
6. Persist PeerStore
7. Start Heartbeat + Gossip

### Leave

```bash
smo mesh leave --mesh <mesh_id>
```

1. Send OFFLINE message to peers
2. Close all connections
3. Stop Heartbeat/Gossip
4. Persist final state

### Switch

```bash
smo mesh use production
```

Switches active mesh context for subsequent commands.

---

## Mesh Configuration

```json
{
  "mesh_id": "a1b2c3d4e5f6...",
  "display_name": "SOC Production",
  "authority_pubkey": "ed25519_pubkey_base64",
  "root_pubkey": "ed25519_pubkey_base64",
  "epoch": 1,
  "created_at": 1700000000000,
  "config": {
    "heartbeat_interval_ms": 5000,
    "heartbeat_timeout_ms": 3000,
    "max_misses": 3,
    "gossip_interval_ms": 5000,
    "gossip_fanout": 3,
    "bootstrap_seeds": [
      "seed1.example.com:7777",
      "seed2.example.com:7777"
    ],
    "default_policy": "enterprise-standard",
    "cert_validity_days": 365,
    "epoch_increment_policy": "manual"
  }
}
```

---

## Filesystem Layout

```
~/.smo/
├── runtime.toml                 # Global runtime config
├── meshes.db                    # SQLite: mesh catalog
└── meshes/
    ├── a1b2c3d4/                # MeshID directory
    │   ├── mesh.json            # Mesh config (above)
    │   ├── cert.smoc            # Authority certificate
    │   ├── peers.db             # PeerStore (SQLite)
    │   ├── audit.db             # AuditStore (SQLite)
    │   ├── contract.db          # ContractRegistry
    │   ├── contracts/           # Contract definitions
    │   ├── cache/               # DAG cache, compilation artifacts
    │   ├── dumps/               # Crash dumps, forensic
    │   └── workflow/            # Workflow definitions
    │
    ├── e5f6g7h8/                # Another mesh
    │   └── ...
```

---

## Mesh Context

Each mesh gets a fully isolated `MeshContext`:

```cpp
struct MeshConfig {
    std::string mesh_id;           // Blake3-derived, immutable
    std::string display_name;      // Human-readable label
    std::string authority_pubkey;
    std::string root_pubkey;
    int64_t epoch = 1;
    int64_t created_at = 0;
};

struct MeshPaths {
    std::string mesh_dir;          // ~/.smo/meshes/<display_name>/ (name-based, not hash)
    std::string mesh_json;
    std::string cert_path;
    std::string identity_json;
    std::string peers_db;
    std::string audit_db;
    std::string contract_db;
    std::string policy_dir;
    std::string contracts_dir;
    std::string cache_dir;
    std::string dumps_dir;
    std::string workflow_dir;
};

struct MeshContext {
    MeshConfig config;
    std::string display_name;
    MeshPaths paths;               // All per-mesh paths resolved
};
```

**Per-mesh directory layout:**

```
~/.smo/meshes/<mesh_id>/
├── mesh.json          # MeshConfig metadata
├── cert.smoc          # Membership certificate
├── identity.json      # Node identity for this mesh
├── peers.db           # PeerStore (SQLite)
├── audit.db           # AuditStore (SQLite)
├── contract.db        # ContractRegistry (SQLite)
├── policies/          # Policy definition files
├── contracts/         # Contract definition files
├── cache/             # DAG cache, compilation artifacts
├── dumps/             # Crash dumps, forensic
└── workflows/         # Workflow definitions
```

**No shared state between meshes** — each has its own SQLite databases, contract registry, peer tables, audit logs, and certificate store.

---

## Mesh Manager API

```cpp
// core/mesh/mesh_manager.hpp

class MeshManager {
public:
    struct Config {
        std::string base_data_dir;       // ~/.smo/
        std::string default_mesh_name = "default";
    };

    explicit MeshManager(const Config& config = {});
    ~MeshManager();

    // Cannot copy
    MeshManager(const MeshManager&) = delete;
    MeshManager& operator=(const MeshManager&) = delete;

    // Lifecycle
    Result<void> initialize();           // Opens catalog.db, creates dirs
    Result<void> create_mesh(const MeshConfig& config, const std::string& name);
    Result<void> delete_mesh(const std::string& mesh_id);
    Result<void> join_mesh(const std::string& mesh_id, const std::string& seed_address);
    Result<void> leave_mesh(const std::string& mesh_id);
    Result<void> switch_mesh(const std::string& mesh_id_or_name);

    // Access
    Result<std::shared_ptr<MeshContext>> get_mesh(const std::string& mesh_id);
    Result<std::shared_ptr<MeshContext>> get_current_mesh() const;
    std::vector<std::string> list_meshes() const;
    std::string get_current_mesh_id() const;
    std::string get_current_mesh_name() const;

    // Open a mesh (returns handle with context)
    struct MeshHandle {
        std::shared_ptr<MeshContext> context;
        ~MeshHandle();
    };
    Result<MeshHandle> open_mesh(const std::string& mesh_id);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
```

### Catalog Database (`catalog.db`)

The Manager maintains a global SQLite catalog at `~/.smo/catalog.db`:

```sql
CREATE TABLE IF NOT EXISTS meshes (
    mesh_id TEXT PRIMARY KEY,
    display_name TEXT NOT NULL,
    authority_pubkey TEXT NOT NULL,
    root_pubkey TEXT NOT NULL,
    epoch INTEGER NOT NULL DEFAULT 1,
    created_at INTEGER NOT NULL,
    config_json TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_meshes_name ON meshes(display_name);
```

### Implementation Details (`core/mesh/mesh_manager.cpp`)

```
mesh_id = BLAKE3(root_pubkey || created_at || random)
        → 64-char hex string used as directory name
```

- `ensure_dirs()` creates all per-mesh directories on `create_mesh()`
- `make_mesh_paths()` resolves all `MeshPaths` from `base_data_dir + mesh_id`
- `generate_mesh_id()` produces deterministic, collision-resistant IDs
- On `initialize()`, creates `catalog.db` with WAL mode and creates a `default` mesh if none exists

---

## Multi-Mesh CLI

```bash
# List meshes
smo mesh list

# Create mesh
smo mesh create --name production --display-name "Production Mesh"

# Switch mesh
smo mesh use production

# Current mesh
smo mesh current
# → production (a1b2c3d4)

# Join mesh
smo mesh join --seed seed.example.com:7777

# Leave mesh
smo mesh leave

# Remove mesh
smo mesh remove lab
```

---

## Isolation Guarantees

| Resource | Isolation |
|----------|-----------|
| Certificates | Per-mesh `.smoc` files |
| PeerStore | Per-mesh `peers.db` |
| Audit Logs | Per-mesh `audit.db` |
| Contracts | Per-mesh `contract.db` + `contracts/` |
| Policies | Per-mesh policy directory |
| Gossip | Per-mesh gossip domain |
| Heartbeat | Per-mesh heartbeat interval |
| Contract Registry | Per-mesh registry |

**No cross-mesh contamination possible.**

---

## Migration & Upgrade

### Mesh Upgrade

```bash
smo mesh upgrade --mesh production --version 2
```

1. Increment epoch
2. Issue new Authority certificates
3. Distribute via Gossip
2. Nodes re-verify chain
3. Old certs rejected

### Mesh Merge

Not supported in MVP. Future: Mesh Federation protocol.

---

## Recovery

### Mesh Recovery Package

```bash
smo mesh recover --mesh production --package recovery.pkg --password ***
```

1. Decrypt package (AES-256-GCM)
2. Extract Root Private Key
3. Reconstruct Mesh state
4. Rejoin mesh

### Shamir Secret Sharing

```bash
smo mesh create --threshold 3 --shares 5
```

Creates 5 recovery shares, any 3 can reconstruct Root Key.

---

## References

- [RFC 0006] Mesh Identity and Certificate Model
- [RFC 0015] Discovery Engine
- [RFC 0027] Network Layer
- [RFC 0028] Contract Runtime

---

**End of RFC 0031**