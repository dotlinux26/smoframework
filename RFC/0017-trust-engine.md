# RFC 0017 — Trust Engine (Engineering)

## Status
ACCEPTED — engineering supplement to RFC 0004. Full engine deferred to Stage 5 (binary trust for MVP).

## Problem
RFC 0004 defines the conceptual trust model (4 components, composite formula, local computation). This RFC defines the concrete interfaces for score computation, decay, penalty application, digest gossip, witness selection, and the execution decision formula.

## Decisions

### 1. TrustEngine is the single entry point
All trust queries go through `TrustEngine::evaluate(requester_id) → Score`. The engine internally computes all four component scores, applies decay, blends in any gossiped trust digest from known peers, and returns a composite. No module calls into sub-components directly.

### 2. Component scores are stored as rolling window aggregates
Each component tracks a fixed window of events (e.g., last 1000 heartbeats for Citizen, last 100 contracts for Execution). Scores are computed from window data, not from cumulative counters. This prevents unbounded score growth (I-13).

| Component | Window | Data Source |
|---|---|---|
| Citizen | Last 1000 heartbeat intervals | HealthMonitor ping success/failure |
| Execution | Last 100 contracts | AuditStore (local + witness attestations) |
| Witness | Last 50 witness assignments | Consensus/Witness records |
| Consistency | Last 100 multi-node results | Cross-node result comparison |

### 3. Decay function: exponential decay over sliding window
```
score(t) = score(t0) * exp(-λ * Δt)
```
Where λ is configurable per component (default: 0.001 per hour). After 1000 hours (~42 days) of inactivity, a component score decays to approximately 37% of its original value. Scores below 0.1 are treated as "unknown node."

### 4. Penalty severity scale
| Offense | Citizen | Execution | Witness | Consistency |
|---|---|---|---|---|
| Failed PING (1 miss) | -0.01 | — | — | — |
| Contract rejected (policy) | — | -0.05 | — | — |
| Contract rejected (capability) | — | -0.10 | — | — |
| Contract failed (execution) | — | -0.15 | — | — |
| Bad witness attestation | — | — | -0.20 | — |
| Witness timeout (frequent) | — | — | -0.05 | — |
| Result divergence | — | — | — | -0.10 |

### 5. Decision formula
```
can_execute = capability_valid
           && policy_matches
           && signature_valid
           && session_valid
           && (trust_score >= local_threshold
               || authority_override)
```
Trust alone never authorizes execution. It is always combined with capability, policy, signature, and session checks.

### 6. Trust digest is a hint, not an authoritative value
Gossiped trust digests are blended into the local score using:
```
blended = local_score * (1 - sender_trust * blend_factor)
        + remote_hint * (sender_trust * blend_factor)
```
`blend_factor` defaults to 0.3. A highly-trusted sender's hint has more influence. A low-trust sender's hint is mostly ignored. Local observations always dominate.

## Interfaces

```cpp
using Score = float;  // 0.0 (distrusted) to 1.0 (fully trusted)

struct ComponentScores {
    Score citizen;
    Score execution;
    Score witness;
    Score consistency;
};

class TrustEngine {
    Result<Score> evaluate(const NodeID& requester_id);
    Result<ComponentScores> components(const NodeID& node_id) const;

    Result<void> record_heartbeat_result(const NodeID& id, bool success);
    Result<void> record_contract_result(const NodeID& id,
                                        ContractStatus status);
    Result<void> record_witness_result(const NodeID& id, bool attestation_valid);
    Result<void> record_consistency_result(const NodeID& id, bool agreed_with_majority);

    Result<void> penalize(const NodeID& id, const Penalty& penalty);

    Result<TrustDigest> digest(const NodeID& id) const;
    Result<void> apply_hint(const NodeID& source,
                            const NodeID& target,
                            Score hint);
    Result<void> tick();  // apply decay over time
};

struct TrustDigest {
    NodeID    node_id;
    Score     composite;
    TimePoint generated_at;
    Signature signature;  // signed by the node that computed this digest
};

class WitnessSelector {
    Result<NodeID> select(const NodeID& requester,
                          const NodeID& responder);
    Result<NodeID> fallback(const NodeID& primary);
};
```

## Consequences
- Trust engine is not needed for MVP (Stage 1-4). Use binary trust: known (0.5) vs unknown (0.0).
- Full trust engine with scoring, decay, and digest gossip arrives in Stage 5-6.
- Rolling window prevents score inflation and unbounded growth.
- Trust digest blending preserves node sovereignty: local observations always dominate.
- Decision formula ensures trust is never the sole factor in execution authorization.
- All trust operations are deterministic given the same event sequence, enabling replay testing.
