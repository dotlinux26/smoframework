# RFC 0033 — Mesh Genesis & Governance (Phase 1)

**Status:** DRAFT  
**Date:** 2026-07-17  
**Authors:** @dotlinux26, @D-O-T-Solutions  
**Supersedes:** RFC 0016 (Governance), RFC 0018 (Mesh Manifest) — partial  

---

## 1. Motivation

Phase 1 replaces the ephemeral-Root-key model with a proper **Root-as-Node** architecture. The mesh lifecycle becomes a first-class state machine from Draft to Archived, with two-tier governance and recovery semantics.

---

## 2. Mesh State Machine

```
Draft ──[StartGenesis]──→ Genesis ──[BootstrapReady]──→ Bootstrap
                                                             │
                                              [AllSlotsFulfilled]│  [TriggerRecovery]
                                                             ▼     │
                                                           Online ←─┘
                                                             │
                                               [EnterMaintenance]│
                                                             ▼
                                                        Maintenance
                                                             │
                                               [ExitMaintenance]│
                                                             ▼
                                                           Online
                                                             │
                                               [TriggerRecovery]│
                                                             ▼
                                                         Recovery
                                                             │
                                               [RecoveryComplete]│  [Archive]
                                                             ▼     │
                                                           Online  Archived
```

**Timeout fallbacks:**
- Genesis 5 min → Draft
- Bootstrap 30 min → Recovery
- Maintenance 24 h → Recovery
- Recovery 72 h → Archived

---

## 3. Role Model (Layer 1 — Identity)

| Role | Value | Description |
|------|-------|-------------|
| Root | 0 | Bootstrap + recovery only. No voting power. Self-signed cert. |
| Authority | 1 | Voting member. Signs proposals by governance. |
| Contributor | 2 | Can deploy contracts, no governance vote. |
| Reader | 3 (deprecated) | Use Member instead. |
| Observer | 4 | Read-only access. |
| Member | 5 | Reader replacement. Same permissions. |
| Recovery | 6 | Special cert issued during recovery only. |

**Rules:**
- Reader deserialized → Member with deprecation warning (`role_deprecate_reader()`).
- Profile (desktop/server/embedded/gateway) lives in `node.json`, not in certificate.

---

## 4. Bootstrap Protocol (2-Stage Genesis)

### Stage 0 — Root Bootstrap
```
smo genesis create Company --profile enterprise --authorities 5
  → Root Node sinh ra (role=ROOT, cert self-signed)
  → genesis_manifest::GenesisManifest (mesh.json)
  → recovery_package::RecoveryPackage (recovery.pkg, encrypted)
  → bootstrap_slot::SlotRing (N waiting slots)
  → root_session::RootSession (24h TTL)
  → In ra join codes: SMO-BOOT-Company-000 ... SMO-BOOT-Company-004
```

### Stage 1 — Authority Bootstrap
```
Trên máy authority:
  smo join SMO-BOOT-XXXX...
  → Máy tự sinh ML-DSA keypair (private key KHÔNG rời máy)
  → Máy sinh CSR
  → Gửi CSR + Slot Token tới Root

Root:
  → Xác thực Slot Token (slot còn Vacant/Claimed+Expired)
  → Root ký CSR → Certificate với role=Authority
  → Slot: Claimed → Fulfilled

Khi tất cả slots Fulfilled:
  Mesh State: Bootstrap → Online
  Root → Dormant
```

**GenesisWizard API (`core/genesis/genesis.hpp`):**
```cpp
GenesisWizard(GenesisCryptoProvider crypto);

// Stage 0
Result<GenesisResult> run_stage_0(
    mesh_id, root_node_id, root_public_key,
    profile, authority_count, recovery_passphrase, now_ns);

// Stage 1
Result<Bytes> run_stage_1_claim_slot(result, role, node_pubkey, join_token_id, now_ns);
Result<void> run_stage_1_fulfill(result, slot_index, signed_csr, now_ns);
```

---

## 5. Bootstrap Slot

```cpp
struct BootstrapSlot {
    uint32_t index;
    SlotStatus status;   // Vacant | Claimed | Fulfilled | Expired | Revoked
    string role;
    string node_public_key;
    string join_token_id;
    uint64_t claimed_at, expires_at;
    string signed_csr;
    uint64_t fulfilled_at;
};

struct SlotRing {
    vector<BootstrapSlot> slots;
    BootstrapSlotConfig config;  // count, ttl_ns (default 72h)
    Result<uint32_t> claim_slot(role, node_public_key, join_token_id, now_ns);
    Result<void> fulfill_slot(index, signed_csr, now_ns);
    Result<void> expire_slot(index);
    Result<void> revoke_slot(index);
};
```

---

## 6. Genesis Manifest

```json
{
  "schema_version": 1,
  "mesh_id": "...",
  "root_public_key": "ml-dsa-87:...",
  "manifest_version": 1,
  "epoch": 1,
  "state": "genesis|bootstrap|online",
  "profile": "enterprise|homelab|personal|startup|air-gapped|custom",
  "authorities": { "minimum": 1, "preferred": 5, "maximum": 15 },
  "quorum": {
    "authority_create": "2/3", "authority_revoke": "3/4",
    "policy_update": "2/3", "emergency_lockdown": "1/3",
    "epoch_rotate": "3/4", "recovery": "1/2"
  },
  "fault_tolerance": { "max_authority_failures": 2, "max_compromised": 1 },
  "created_at": 0, "wizard_version": 1
}
```

**Profile defaults:**

| Profile | Min | Preferred | Max | Fault Tolerance |
|---------|-----|-----------|-----|-----------------|
| Personal | 1 | 1 | 3 | 0 failures |
| Homelab | 1 | 3 | 5 | 1 failure |
| Startup | 1 | 3 | 5 | 1 failure |
| Enterprise | 1 | 5 | 15 | 2 failures, 1 compromised |
| Air-Gapped | 1 | 3 | 5 | 1 failure |

---

## 7. Two-Level Governance

### Level A — Membership (registry only)
| Action | Default Quorum |
|--------|---------------|
| AddAuthority | ceil(2N/3) |
| RemoveAuthority | ceil(2N/3) |
| SuspendAuthority | ceil(2N/3) |
| ResumeAuthority | ceil(2N/3) |

No `manifest_version++`, no `epoch++`.

### Level B — Constitution (manifest_version++ + epoch++ + broadcast)
| Action | Default Quorum |
|--------|---------------|
| ChangeMaximum | ceil(3N/4) |
| ChangeMinimum | ceil(3N/4) |
| ChangeQuorum | ceil(3N/4) |
| ChangePolicy | ceil(3N/4) |
| UpdateManifest | ceil(3N/4) |
| UpgradeRuntime | ceil(3N/4) |
| EpochIncrement | ceil(3N/4) |

### Unanimous
| Action | Quorum |
|--------|--------|
| ChangeCipherSuite | N/N |
| ChangeGovernanceRules | N/N |
| DestroyMesh | N/N |
| ChangeRecovery | N/N |

### GovernanceTier mapping

```cpp
GovernanceTier action_to_tier(GovernanceAction a);
// Membership   → ceil(2N/3)
// Constitution → ceil(3N/4)
// Unanimous    → N
```

---

## 8. Mesh Health

| Condition | Level |
|-----------|-------|
| online >= preferred | Healthy |
| min <= online < preferred | Warning |
| online = min | Critical |
| online = 0 | Recovery |

Output:
```
State:       Operational / Degraded
Health:      Healthy / Warning / Critical / Recovery
Authorities: 5/5 online (min=1, preferred=5, max=15)
Offline:     0
Quorum:      2/3 (need 3 to operate)
Fault tolerance: can tolerate 2 more failure(s)
```

---

## 9. Recovery

### Soft Recovery (quorum tồn tại)
1. User mở recovery package (passphrase)
2. Root Session mở (task-based, 1h TTL)
3. Governance vote (recovery quorum riêng: `1/2 + Recovery Package`)
4. Root ký recovery certificate
5. Epoch++ → invalidate certs của authority bị thay thế

### Hard Recovery (mất quorum)
1. User mở recovery package
2. Force reset: epoch++ → invalidate ALL certificates
3. Clean slate: tất cả authority slots mới
4. Bootstrap lại như lần đầu (Stage 0 + Stage 1)

### RecoveryEngine API (`core/recovery/recovery_engine.hpp`)
```cpp
RecoveryMode assess_mode(total, online, quorum_threshold);
// → None | Soft | Hard

Result<RecoverySession> start_soft(mesh_id, root_node_id, current_epoch, passphrase, now);
Result<RecoverySession> start_hard(mesh_id, root_node_id, current_epoch, passphrase, now);
Result<void> add_signature(session, sig, now);
Result<void> execute(session, now);
Result<void> cancel(session);
```

---

## 10. CRL & Revocation

- `CRL` class with `revoke()`, `is_revoked()`, `is_epoch_invalid()`, `entries_since()`
- Epoch check: cert với epoch < current epoch → implicitly revoked
- Protocol messages: `RevokeCertMsg`, `RevokeAckMsg`
- Opcodes: `REVOKE_CERT (0x20)`, `EPOCH_INCREMENT (0x21)`, `RECOVERY_SESSION (0x22)`, `CRL_SYNC (0x23)`

---

## 11. Error Codes (new)

| Category | Range | Codes |
|----------|-------|-------|
| Genesis | 1400-1499 | 10 codes (GENESIS_FAILED, INVALID_MANIFEST, SLOT_EXHAUSTED, ...) |
| Recovery | 1500-1599 | 6 codes (RECOVERY_NOT_NEEDED, ...RECOVERY_EPOCH_ROLLBACK) |
| Governance (new) | 812-813 | PROPOSAL_CONFLICT, ACTION_NOT_ALLOWED |

---

## 12. File Layout

```
core/
├── genesis/
│   ├── genesis_manifest.hpp/.cpp   — GenesisManifest, DeploymentProfile, AuthorityRange, QuorumConfig
│   ├── bootstrap_slot.hpp/.cpp     — BootstrapSlot, SlotRing, SlotStatus
│   ├── recovery_package.hpp/.cpp   — RecoveryPackage, EmergencyRecoveryToken
│   ├── root_session.hpp/.cpp       — RootSession, RootSessionManager
│   ├── genesis.hpp/.cpp            — GenesisWizard (Stage 0 + Stage 1)
│   └── CMakeLists.txt
├── mesh/
│   ├── mesh_state.hpp              — MeshState enum + transition validation
│   └── mesh_fsm.hpp/.cpp           — MeshFsm (wrapper quanh FsmInstance)
├── governance/
│   └── governance.hpp/.cpp         — 2-tier, GovernanceAction (16), MeshHealth
├── recovery/
│   ├── recovery_engine.hpp/.cpp    — RecoveryEngine, RecoverySession
│   └── crl.hpp                     — CRL, CRLEntry, RevokeCertMsg
├── opcode/
│   └── opcode.h                    — +4 opcodes (REVOKE_CERT, etc.)
├── certificate/
│   └── certificate.hpp             — +Recovery role, deprecate Reader→Member
├── authority/
│   └── authority.hpp               — +sign_bootstrap_csr, [[deprecated]] create_mesh_keys
└── errors/
    ├── error.hpp                   — +Genesis(15), Recovery(16) categories
    └── error_codes.md              — +Genesis(10), Recovery(6), Governance(2) codes
```

---

## 13. Future Work (Phase 2 — Sprint 36+)

- Join Token implementation (role + profile + bootstrap endpoints + HMAC)
- Bootstrap protocol over wire (slot claim + CSR exchange)
- Governance persistence (SQLite proposals)
- Epoch broadcast on constitution change
- Mesh health online/offline detection via heartbeat
- Conflict resolution (1 proposal/resource)
- `smo mesh health` with real online counts from registry
