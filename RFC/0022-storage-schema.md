# RFC 0022 — Storage Schema

## Status
DRAFT — pending review.

## Problem
SMO defines 8 per-node stores (session, trust, audit, dag, node, mesh, peer, governance). Each must have a concrete SQLite schema, version numbering, migration strategy, and backup/restore procedure. Without a frozen schema, storage becomes the #1 source of irrecoverable data corruption across version upgrades.

## Decisions

### 1. SQLite3 C API directly — no ORM, no wrapper
Runtime calls `sqlite3_prepare_v2`, `sqlite3_bind_*`, `sqlite3_step`, `sqlite3_finalize` directly. No SOCI, no sqlitecpp, no ORM. Rationale:
- SQLite3 C API is the most stable ABI in computing (20+ years of backward compatibility).
- No dependency chain to update or audit.
- Full control over WAL mode, page size, mmap, and busy timeout.
- Every query is visible and reviewable.

### 2. One database file per store (8 files max per mesh)

| Store | File | Schema Version | Content |
|---|---|---|---|
| node | `node.db` | 1 | Keypair (encrypted), config |
| mesh | `mesh.db` | 1 | Mesh manifest, certificates |
| session | `session.db` | 1 | Active sessions, capabilities |
| trust | `trust.db` | 1 | Trust scores, event windows, decay params |
| audit | `audit.db` | 1 | Transition records, contract audit trails |
| dag | `dag.db` | 1 | Compiled DAGs, execution plans |
| peer | `peer.db` | 1 | Peer records, membership table |
| governance | `governance.db` | 1 | Proposals, signatures, history |

For multi-tenant: `~/.smo/meshes/<mesh-name>/<store>.db`.

### 3. Every table has standard metadata columns
```sql
-- Present in EVERY table across all stores
_row_created_at INTEGER NOT NULL DEFAULT (unixepoch('now')),
_row_updated_at INTEGER NOT NULL DEFAULT (unixepoch('now')),
_row_version    INTEGER NOT NULL DEFAULT 1
```

### 4. Per-store schema (frozen tables)

**node.db:**
```sql
CREATE TABLE keypair (
    suite_id    INTEGER PRIMARY KEY,
    encrypted   BLOB NOT NULL,       -- AES-256-GCM encrypted key material
    nonce       BLOB NOT NULL,       -- AEAD nonce
    created_at  INTEGER NOT NULL
);

CREATE TABLE config (
    key   TEXT PRIMARY KEY,
    value BLOB NOT NULL
);
```

**mesh.db:**
```sql
CREATE TABLE manifest (
    mesh_id     TEXT PRIMARY KEY,
    yaml_text   TEXT NOT NULL,
    imported_at INTEGER NOT NULL,
    verified    INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE certificates (
    cert_hash       BLOB PRIMARY KEY,    -- Blake3(der_bytes)
    der_bytes       BLOB NOT NULL,
    mesh_id         TEXT NOT NULL,
    node_pubkey     BLOB NOT NULL,
    role            INTEGER NOT NULL,
    epoch           INTEGER NOT NULL,
    issued_by       BLOB NOT NULL,
    not_before      INTEGER NOT NULL,
    not_after       INTEGER NOT NULL,
    status          INTEGER NOT NULL DEFAULT 1  -- 1=active, 0=revoked, -1=suspended
);
CREATE INDEX idx_cert_mesh ON certificates(mesh_id);
```

**session.db:**
```sql
CREATE TABLE sessions (
    session_id      BLOB PRIMARY KEY,
    peer_id         BLOB NOT NULL,
    mesh_id         TEXT NOT NULL,
    state           INTEGER NOT NULL,       -- SessionState enum
    capabilities    BLOB NOT NULL,          -- serialized capability bitset
    cert_hash       BLOB NOT NULL,
    encryption_key  BLOB,                   -- optional, NULL if transport-level
    created_at      INTEGER NOT NULL,
    expires_at      INTEGER NOT NULL,
    last_activity   INTEGER NOT NULL
);
CREATE INDEX idx_session_expiry ON sessions(expires_at);
```

**trust.db:**
```sql
CREATE TABLE scores (
    node_id         BLOB PRIMARY KEY,
    citizen         REAL NOT NULL DEFAULT 0.5,
    execution       REAL NOT NULL DEFAULT 0.5,
    witness         REAL NOT NULL DEFAULT 0.5,
    consistency     REAL NOT NULL DEFAULT 0.5,
    composite       REAL NOT NULL DEFAULT 0.5,
    last_updated    INTEGER NOT NULL
);

CREATE TABLE events (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    node_id     BLOB NOT NULL,
    event_type  INTEGER NOT NULL,           -- heartbeat|contract|witness|consistency
    outcome     INTEGER NOT NULL,           -- success|failure
    timestamp   INTEGER NOT NULL,
    detail      BLOB
);
CREATE INDEX idx_events_node ON events(node_id);
```

**audit.db:**
```sql
CREATE TABLE transitions (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    contract_id     BLOB,
    node_fsm_state  INTEGER NOT NULL,
    contract_state  INTEGER,
    event_type      INTEGER NOT NULL,
    from_state      INTEGER NOT NULL,
    to_state        INTEGER NOT NULL,
    elapsed_ns      INTEGER NOT NULL,
    state_hash      BLOB NOT NULL,
    timestamp       INTEGER NOT NULL,
    extra           BLOB                    -- JSON or CBOR context
);
CREATE INDEX idx_audit_contract ON transitions(contract_id);
CREATE INDEX idx_audit_time ON transitions(timestamp);
```

**dag.db:**
```sql
CREATE TABLE dags (
    dag_hash    BLOB PRIMARY KEY,
    contract_id BLOB NOT NULL,
    json_text   TEXT NOT NULL,
    compiled_at INTEGER NOT NULL,
    status      INTEGER NOT NULL DEFAULT 0  -- 0=pending, 1=running, 2=done, 3=failed
);
```

**peer.db:**
```sql
CREATE TABLE peers (
    node_id         BLOB PRIMARY KEY,
    addrs_json      TEXT NOT NULL,           -- JSON array of endpoints
    pubkey_fp       BLOB NOT NULL,
    protocol_ver    INTEGER NOT NULL,
    cap_digest      BLOB,
    trust_digest    REAL,
    state           INTEGER NOT NULL,        -- PeerState enum
    last_seen       INTEGER NOT NULL,
    first_seen      INTEGER NOT NULL
);
```

**governance.db:**
```sql
CREATE TABLE proposals (
    proposal_id     BLOB PRIMARY KEY,
    level           INTEGER NOT NULL,
    action          TEXT NOT NULL,
    payload         BLOB,
    created_at      INTEGER NOT NULL,
    expires_at      INTEGER NOT NULL,
    state           INTEGER NOT NULL         -- 0=draft, 1=signing, 2=committed, 3=rejected, 4=expired
);

CREATE TABLE signatures (
    proposal_id     BLOB NOT NULL,
    authority_id    BLOB NOT NULL,
    signature       BLOB NOT NULL,
    signed_at       INTEGER NOT NULL,
    PRIMARY KEY (proposal_id, authority_id)
);

CREATE TABLE history (
    epoch       INTEGER PRIMARY KEY,
    state_hash  BLOB NOT NULL,              -- Blake3 of governance state at this epoch
    prev_hash   BLOB NOT NULL               -- Blake3 of previous epoch's state_hash
);
```

### 5. Schema versioning and migration
```sql
PRAGMA user_version = 1;
```
On database open, runtime reads `user_version`. If lower than expected, migration scripts run sequentially. Migration scripts are idempotent and tested. There is no automated rollback — downgrade requires restoring from backup.

### 6. WAL mode + busy timeout
```sql
PRAGMA journal_mode = WAL;
PRAGMA busy_timeout = 5000;    -- 5 second busy wait
PRAGMA synchronous = NORMAL;   -- WAL mode: safe with NORMAL
```

### 7. Backup via SQLite online backup API
`Store::backup(path)` uses `sqlite3_backup_init` to create a consistent snapshot without stopping the runtime. Restore closes the current database, replaces the file, and reopens.

## Consequences
- Schema is frozen per-store. Adding a column or table requires a migration script and a `user_version` bump.
- 8 files per mesh, isolated by mesh directory. No cross-mesh data leakage.
- Direct C API eliminates ORM dependency and version lock.
- WAL mode allows concurrent reads (audit queries) without blocking writes (session updates).
- Backup/restore is built-in — no external `sqlite3` CLI needed.
- Schema version is checked at every open. Mismatch = controlled shutdown, not silent corruption.
