# RFC 0021 — FSM Rules

## Status
DRAFT — pending review.

## Problem
FSM (Finite State Machine) is the backbone of SMO runtime: it governs node lifecycle, contract execution, mesh coordination, and failure recovery. Without a frozen FSM architecture, every implementation will invent different transition patterns, making the runtime non-deterministic and unreplayable.

## Decisions

### 1. Every FSM state transition MUST satisfy 4 invariants (I-04 through I-07)

| Invariant | Rule |
|---|---|
| I-04 | **Deterministic** — same input + same prior state = same output |
| I-05 | **Auditable** — every transition recorded in audit log |
| I-06 | **Replayable** — audit log reconstructs exact execution |
| I-07 | **Serializable** — state can be stored, transmitted, verified |

These four invariants are non-negotiable. Any FSM implementation that violates one of them is a spec violation.

### 2. Every FSM state MUST have a timeout and a failure transition (Rule 9)

No state may block indefinitely. Each state defines:
- `max_dwell_time`: maximum time before timeout fires
- `fallback_state`: the safe state to transition to on timeout
- `on_timeout()`: optional handler (log, cleanup, notify)

Default fallback is REJECTED (for contract states) or OFFLINE (for node states).

### 3. Two independent FSM instances per node

| FSM | Scope | States | Example Transitions |
|---|---|---|---|
| **Node FSM** | Per-node lifecycle | UNINITIALIZED → KEY_GENERATION → ENROLLING → DOMAIN_JOIN → ACTIVE → (DEGRADED | QUARANTINED | OFFLINE) | ACTIVE → SUSPENDED → ACTIVE; ACTIVE → QUARANTINED |
| **Contract FSM** | Per-contract execution | PENDING → VALIDATE_* → CONSULT_WITNESS → LOCAL_DECISION → EXECUTE → RECORD_RESULT → FINALIZED | EXECUTING → FAILED; EXECUTING → TIMEOUT; VALIDATING → REJECTED |

These FSMs are independent. Contract FSM runs within the context of Node FSM's ACTIVE state. A node in QUARANTINED state does not start new Contract FSMs.

### 4. FSM state is the single source of truth for node/contract status

No other component stores a parallel "node status" or "contract status." Every status query must go through the FSM. This prevents divergence between "what the runtime thinks" and "what the FSM says."

### 5. FSM transitions are event-driven, not polled

Events that trigger transitions:
- External: network messages (CONTRACT_PROPOSAL, EXEC_CANCEL, REVOKE_CERT)
- Internal: timer expiry (timeout, heartbeat miss), execution completion, worker result
- Administrative: CLI commands (suspend, quarantine, shutdown)

There is no polling loop inside the FSM. Events are pushed via `Fsm::on_event(Event)`.

### 6. FSM is serializable for crash recovery

`Fsm::serialize()` produces a byte array that is stored in `audit_store` or `dag_store`. On restart, `Fsm::deserialize(bytes)` reconstructs the exact FSM state. If the state was EXECUTING, the recovery module checks the actual execution outcome.

### 7. FSM does not know about transport, storage, or crypto

The FSM receives abstract `Event` objects and produces abstract `Action` objects. Side effects (sending a message, writing to storage, signing with a keypair) are handled by the runtime layer that owns the FSM. This ensures the FSM remains pure and testable.

## Interfaces

```cpp
template<typename State, typename Event>
class Fsm {
    State current_state() const;
    Result<void> on_event(const Event& event);
    Result<void> on_timeout();          // triggered by timer thread
    Duration max_dwell_time() const;    // per current state

    // Serialization (I-07)
    Result<std::vector<uint8_t>> serialize() const;
    static Result<Fsm> deserialize(std::span<const uint8_t> data);

    // Audit (I-05, I-06)
    const std::vector<TransitionRecord>& history() const;
};

struct TransitionRecord {
    State      from;
    Event      event;
    State      to;
    TimePoint  timestamp;
    uint64_t   elapsed_ns;       // dwell time in previous state
    Hash256    state_hash;       // hash of serialized state after transition
};

// Concrete states — Node FSM
enum class NodeState : uint8_t {
    Uninitialized, KeyGeneration, Enrolling, DomainJoin,
    CapabilityNegotiation, Active,
    ContractPending, ContractValidating, ContractAccepted,
    ContractRejected, Executing, Completed, Failed,
    Auditing, Recording,
    Degraded, Quarantined, Offline, Suspended, Retired
};

// Concrete states — Contract FSM
enum class ContractState : uint8_t {
    Pending,
    ValidatePolicy, ValidateCertificate, ValidateCapability,
    ValidateSignature, VerifySession, ConsultWitness,
    LocalDecision,
    Execute,
    RecordResult, NotifyParticipants,
    Finalized,
    Rejected, Failed, TimedOut, Cancelled
};
```

## Consequences
- FSM is the heart of runtime determinism. All state changes go through `Fsm::on_event()`.
- Serialization enables crash recovery without external state reconstruction.
- Independence from transport/storage/crypto means FSM can be unit-tested with mock events.
- Dual FSM (Node + Contract) provides clean separation of concerns.
- Event-driven design eliminates polling overhead and race conditions in state checks.
- Every transition is auditable by design — no silent state mutations.
