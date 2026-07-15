# RFC 0010 — Storage Backend

## Status
ACCEPTED — incorporated into SPEC.md §XV and RFC 0011 (thread ownership).

## Problem
SMO requires 8 per-node stores (session, trust, audit, dag, node, mesh, peer, governance). These stores must support key-value semantics, transactions (for multi-store atomicity), and backup/restore. The backend must be embedded (no external database daemon), crash-safe, and low-latency.

## Decisions

### 1. Choice: SQLite3 (single-file, embedded, battle-tested)
Alternatives considered and rejected:

| Backend | Rejected Because |
|---|---|
| LevelDB | No built-in transaction across 8 stores; compaction pauses cause latency spikes |
| LMDB | MVCC + mmap model conflicts with SMO's per-node ownership; high risk of corruption on unclean shutdown |
| RocksDB | C++ dependency heavy; LSM compaction tuning is deployment-specific |
| Custom binary | Would need WAL, crash recovery, B-tree implementation — reinventing SQLite |

SQLite3 is chosen because:
- Single-file per store (8 files max per node)
- Full ACID transactions with WAL mode
- `BEGIN/COMMIT/ROLLBACK` across multiple store operations
- `backup_api` for online backup
- Battle-tested in embedded environments (SQLite is in every smartphone, browser, and many appliances)
- Schema migration via `PRAGMA user_version` + migration scripts

### 2. One database file per store type
| File | Store |
|---|---|
| `node.db` | node_store (identity, keypair, config) |
| `mesh.db` | mesh_store (certificates, manifest per mesh) |
| `session.db` | session_store (active sessions) |
| `trust.db` | trust_store (scores, decay parameters) |
| `audit.db` | audit_store (execution audit log) |
| `dag.db` | dag_store (compiled DAGs) |
| `peer.db` | peer_store (membership table, peer records) |
| `governance.db` | governance_store (proposals, history) |

Stores are isolated. A node crash corrupting `session.db` does not affect `node.db` or `audit.db`.

### 3. WAL mode enabled by default
`PRAGMA journal_mode=WAL;` for all stores. This allows concurrent reads without blocking writes.

### 4. Schema managed via `PRAGMA user_version`
Each store file has a schema version number. On open, runtime checks the version and applies migration scripts if needed. No ORM — raw SQL via the C API.

### 5. Backup via SQLite online backup API
`Store::backup(path)` uses `sqlite3_backup_init`. Restore replaces the target file then reopens. No external tools needed.

## Interfaces

```cpp
class Store {
    virtual Result<void> put(std::span<const uint8_t> key,
                             std::span<const uint8_t> value) = 0;
    virtual Result<std::vector<uint8_t>> get(
        std::span<const uint8_t> key) = 0;
    virtual Result<void> del(std::span<const uint8_t> key) = 0;
    virtual Result<std::vector<std::vector<uint8_t>>> list(
        std::span<const uint8_t> prefix) = 0;

    virtual Result<Transaction> begin_transaction() = 0;
    virtual Result<void> backup(const std::filesystem::path& path) = 0;
    virtual Result<void> restore(const std::filesystem::path& path) = 0;
};

class Transaction {
    Result<void> commit();
    Result<void> rollback();
    // put/get/del within transaction context
};

// Schema migration interface
Result<void> migrate_store(const std::filesystem::path& db_path,
                           int target_version);
```

## Consequences
- Every node stores 8 `.db` files. Total size is bounded by contract history + trust data.
- SQLite WAL mode ensures no write blocks read in audit or trust queries.
- Transaction support enables atomic cross-store operations (e.g., session open = session_store.create + audit_store.record in one commit).
- Backup/restore is built-in, no external tooling required.
- Schema migration must be explicit: no silent schema changes across SMO versions.
- SQLite's single-writer limitation is acceptable for per-node stores (no concurrent writers within a single node process).
