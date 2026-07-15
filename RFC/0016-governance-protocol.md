# RFC 0016 — Governance Protocol

## Status
ACCEPTED — 5-tier governance model frozen. Fail-closed conflict resolution confirmed.

## Problem
Mesh-wide decisions (capability grant/revoke, policy changes, authority addition/removal, epoch increment, emergency lockdown) require a mechanism for Authorities to propose, sign, and commit actions. Governance must be tiered by impact, configurable per mesh, and fully independent of contract execution.

## Decisions

### 1. Governance does NOT govern execution (§33.1)
Governance manages the mesh — not individual contracts. An Authority cannot use governance to force a Responder to execute a contract. Execution decisions remain local to the Responder.

### 2. Five governance levels with configurable thresholds (§33.2)

| Level | Example Actions | Default Threshold | Scope |
|---|---|---|---|
| L0 — Local | Node-local policy, plugin enable | 1-of-1 (self) | Single node |
| L1 — Authority | Issue/revoke cert, grant capability | 1-of-N Authorities | Authority action |
| L2 — Policy | Change trust thresholds, protocol config | 2-of-N Authorities | Mesh-wide |
| L3 — Critical | Root rotate, mesh destroy, emergency lockdown | M-of-N (e.g., 3-of-5) | Mesh-wide |
| L4 — Genesis | Create mesh | 1-of-1 (Root Key) | Once only |

Thresholds are defined in the Mesh Manifest (`governance.policy_threshold`, `governance.critical_threshold`). Changing a threshold requires a Level 2 (or higher) proposal.

### 3. Proposal lifecycle
```
DRAFT → SIGNING → COMMITTED (threshold met)
   ↓        ↓
EXPIRED  REJECTED (conflicting proposal reached threshold first)
```
- Any Authority creates a `GovernanceProposal`.
- Other Authorities sign it.
- When signature count reaches the threshold for the proposal's level, any observing node may `commit()`.
- The commit action appends the proposal to `governance_store` and applies the effect (e.g., revokes a certificate, increments epoch).
- Proposals that do not reach threshold within `max_ttl` (default 24 hours) transition to EXPIRED.

### 4. Conflict resolution: first-past-the-post
If two proposals conflict (e.g., Authority A grants a capability, Authority B revokes it), the first proposal to reach threshold wins. The losing proposal is REJECTED. This creates a **fail-closed** default: if split-brain occurs, conflicting proposals both remain uncommitted, and no change is applied until mesh connectivity is restored.

### 5. Compromised Authority recovery
A compromised Authority's certificates are invalidated by an epoch increment. The epoch increment proposal requires Level 3 threshold. After the increment, all certificates issued by the compromised Authority are invalid (epoch too old). Affected nodes must re-enroll.

### 6. Mesh split handling
If a mesh splits into two partitions, each partition continues operating with its own epoch counter. When connectivity is restored, the runtime detects a governance history fork (divergent `governance_store` hashes). No automatic merge. Operators must manually reconcile and issue a Level 3 proposal accepting one history as canonical.

### 7. Root Key usage is strictly limited
The Root Key is used exactly once (mesh creation). It may be used again only for:
- Emergency recovery (reconstructing M-of-N shares).
- Signing the first Authority certificate after genesis.
- Authorizing a Level 4 (Genesis-level) decision after the original Root is lost (requires M-of-N recovery).

Any attempt to use the Root Key for Level 1-3 decisions is rejected by the governance engine.

## Interfaces

```cpp
enum class GovernanceLevel : uint8_t {
    Local = 0, Authority = 1, Policy = 2, Critical = 3, Genesis = 4
};

enum class ProposalState : uint8_t {
    Draft, Signing, Committed, Rejected, Expired
};

struct GovernanceProposal {
    ProposalID       id;
    GovernanceLevel  level;
    std::string      action;      // "grant_capability", "revoke_cert", "increment_epoch", etc.
    std::vector<uint8_t> payload; // action-specific data
    TimePoint        created_at;
    TimePoint        expires_at;
    std::vector<Signature> signatures;

    static Result<GovernanceProposal> create(
        GovernanceLevel level,
        std::string_view action,
        std::span<const uint8_t> payload,
        TimePoint ttl);
    Result<void> sign(const Keypair& authority_key);
    Result<bool> threshold_met(const MeshManifest& manifest) const;
    Result<void> commit(GovernanceStore& store);
};

class GovernanceEngine {
    Result<void> submit_proposal(GovernanceProposal proposal);
    Result<void> sign_proposal(ProposalID id, const Keypair& authority_key);
    Result<void> commit_proposal(ProposalID id);
    Result<GovernanceProposal> get_proposal(ProposalID id) const;
    Result<std::vector<GovernanceProposal>> pending_proposals() const;
    Result<void> tick();  // expire stale proposals
    Result<void> detect_fork(const GovernanceStore& local,
                             const GovernanceStore& remote);  // compare history hashes
};
```

## Consequences
- Governance actions are fully auditable (append-only `governance_store`).
- Configurable thresholds let small meshes (1 Authority) and large meshes (7+ Authorities) use the same engine with different manifests.
- Fail-closed conflict resolution prevents runtime ambiguity: if Authorities disagree, the default is "no change."
- Root Key isolation limits blast radius: Root compromise requires a targeted attack on the recovery package, not a runtime exploit.
- Mesh split detection + manual merge is a deliberate tradeoff: automatic merge of governance histories is intractable when policies diverge.
