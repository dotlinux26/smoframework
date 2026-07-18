# RFC 0038 — Execution State Machine

**Status:** APPROVED — Conditions resolved (added Compensating, removed TimedOut terminal state)  
**Date:** 2026-07-18  
**Authors:** @dotlinux26, @D-O-T-Solutions  
**Supersedes:** —  
**Extends:** RFC 0035 (Runtime Architecture)

---

## 1. Motivation

RFC 0035 defines a `Step` with a simple `StepStatus`:

```cpp
// From RFC 0035 (current)
enum State { Pending, Running, Completed, Failed, Compensating, Compensated, Skipped };
```

This is insufficient for:

1. **Retry** — No `WaitingRetry` state; retries are invisible in the state machine
2. **Cancellation** — No `Cancelled` state; no way to distinguish cancelled vs failed
3. **Scheduling** — No `Scheduled` state; Scheduler integration is ad-hoc
4. **Compensation** — `Compensated` is terminal; no `Compensating` progress tracking
5. **Observability** — 7 states vs 12+ states; auditing can't distinguish transient vs terminal

**This RFC replaces the flat StepStatus with a formal state machine** that covers all execution paths: normal, retry, cancel, compensation, and timeout.

---

## 2. Design

### 2.1 State Machine

```
                         ┌─────────────────────────────────────────┐
                         │                                         │
                         ▼                                         │
                    ┌─────────┐                                    │
         ┌────────►│ Pending  │────┐                               │
         │         └─────────┘    │                               │
         │              │         │                               │
         │              ▼         │                               │
         │         ┌─────────┐    │                               │
         │         │  Ready   │◄───┘                               │
         │         └─────────┘                                    │
         │              │                                         │
         │              ▼                                         │
         │         ┌──────────┐                                   │
         │         │ Running  │◄────────────┐                    │
         │         └──────────┘             │                    │
         │           │    │                 │                    │
         │     ┌─────┘    └──────┐          │                    │
         │     ▼                  ▼         │                    │
         │  ┌─────────┐    ┌────────────┐   │                    │
         │  │Completed│    │WaitingRetry│───┘                    │
         │  └─────────┘    └────────────┘                        │
         │                                                       │
         │  (failure / abort / timeout paths)                    │
         │     │                                                  │
         │     ▼                                                  │
         │  ┌──────────┐       ┌──────────────┐                  │
         │  │  Failed   │──────►│Compensating  │                  │
         │  └──────────┘       └──────────────┘                  │
         │                           │                           │
         │                           ▼                           │
         │                      ┌────────────┐                   │
         │                      │Compensated │                   │
         │                      └────────────┘                   │
         │                                                       │
         │  (explicitly cancelled)                               │
         │     │                                                  │
         │     ▼                                                  │
         │  ┌───────────┐                                         │
         │  │ Cancelled │                                         │
         │  └───────────┘                                         │
         │                                                       │
         │  (dependency failed, step is optional)                │
         │     │                                                  │
         │     ▼                                                  │
         │  ┌─────────┐                                           │
         └──┤ Skipped │                                           │
            └─────────┘                                           │
                                                                  │
         (retry exhausted → Failed) ──────────────────────────────┘
         (deadline exceeded → Failed) ────────────────────────────┘
```

### 2.2 State Transitions

| From | To | Trigger |
|------|----|---------|
| `Pending` | `Ready` | All dependencies resolved |
| `Pending` | `Cancelled` | Explicit cancel before start |
| `Ready` | `Running` | Executor picks up step |
| `Ready` | `Cancelled` | Explicit cancel before execution |
| `Running` | `Completed` | Successful execution |
| `Running` | `WaitingRetry` | Retryable failure, retries remaining |
| `Running` | `Failed` | Non-retryable failure, retries exhausted, or timeout |
| `Running` | `Compensating` | Failure + `rollback_policy != "BestEffort"` |
| `Running` | `Cancelled` | Explicit cancel during execution |
| `WaitingRetry` | `Ready` | Retry timer fires |
| `WaitingRetry` | `Failed` | Retry budget exhausted |
| `WaitingRetry` | `Cancelled` | Explicit cancel during wait |
| `Failed` | `Compensating` | `rollback_policy != "BestEffort"` |
| `Failed` | `Skipped` | Optional step, no compensation needed |
| `Failed` | `Cancelled` | Explicit cancel after failure |
| `Compensating` | `Compensated` | Compensation completes |
| `Compensating` | `CompFault` | Compensation fails |
| `Completed` | *(terminal)* | — |
| `Cancelled` | *(terminal)* | — |
| `Skipped` | *(terminal)* | — |
| `Compensated` | *(terminal)* | — |
| `CompFault` | *(terminal)* | — |

**Note:** `TimedOut` is NOT a separate state. Timeout is modeled as `Failed(reason=timeout)` with `RuntimeErrorCategory::Timeout`. This avoids duplicating all failure-handling logic for a parallel state. The error category distinguishes the root cause, not the state.

### 2.3 C++ Definition

```cpp
enum class StepState : uint8_t {
    // ── Active states ───────────────────────────────────────────────
    Pending        = 0,   // Not yet ready (waiting for dependencies)
    Ready          = 1,   // Dependencies resolved, waiting for executor
    Running        = 2,   // Currently executing
    WaitingRetry   = 3,   // Failed with retryable error, waiting backoff
    Compensating   = 4,   // Compensation in progress (distinct from Running)

    // ── Terminal success states ──────────────────────────────────────
    Completed      = 10,  // Executed successfully
    Compensated    = 11,  // Compensation completed

    // ── Terminal failure states ──────────────────────────────────────
    Failed         = 20,  // Non-retryable failure / retries exhausted / timeout
    Cancelled      = 21,  // Explicitly cancelled
    Skipped        = 22,  // Optional step, skipped due to dependency failure
    CompFault      = 23,  // Compensation itself failed
};

struct StepStatus {
    StepState state = StepState::Pending;
    std::string output;
    std::string error_message;
    uint64_t started_at_ns = 0;
    uint64_t completed_at_ns = 0;
    uint64_t last_retry_at_ns = 0;
    int attempt = 0;
    int max_retries = 3;
    uint64_t retry_delay_ns = 1'000'000'000;  // 1s base
    uint64_t cumulative_wait_ns = 0;           // total retry wait time

    bool is_terminal() const {
        return state >= StepState::Completed;
    }
    bool is_success() const {
        return state == StepState::Completed || state == StepState::Compensated;
    }
    bool is_retryable() const {
        return attempt < max_retries;
    }
};
```

### 2.4 Plan State Machine

The Plan itself also has a state machine:

```cpp
enum class PlanState : uint8_t {
    Resolving      = 0,
    Ready          = 1,
    Executing      = 2,
    Completed      = 10,
    PartiallyDone  = 11,  // some steps skipped/compensated
    Failed         = 20,
    Cancelled      = 21,
    Compensating   = 30,
    Compensated    = 31,
};
```

### 2.5 Integration with PlanExecutor

```cpp
class PlanExecutor {
    Result execute(PlanContext& ctx) {
        ctx.plan_state = PlanState::Executing;
        
        // ... DAG execution loop ...
        
        for (auto& [id, status] : ctx.step_status) {
            if (status.state == StepState::Failed && plan_.rollback_policy == "AllOrNothing") {
                trigger_compensation(ctx);
                ctx.plan_state = PlanState::Compensated;
                return Result{false, {}, RuntimeError::compensation("plan compensated"), {}};
            }
        }
        
        ctx.plan_state = PlanState::Completed;
        return Result{true, ctx.context, {}, {}};
    }
};
```

---

## 3. Observability

Every state transition emits an EventBus event:

```cpp
struct StepTransitionEvent {
    std::string execution_id;
    std::string step_id;
    StepState from;
    StepState to;
    std::string reason;
    uint64_t timestamp_ns;
};
```

Subscribers (AuditService, HistoryStore, MetricsService) consume these transitions.

---

## 4. Consequences

### Positive
- **Precise observability** — each state is semantically distinct
- **Retry chain visible** — WaitingRetry → Ready → Running → WaitingRetry is observable
- **Cancellation distinct** — Cancelled ≠ Failed, different semantics
- **Timeout distinct** — TimedOut has different recovery path
- **Compensation chain** — Compensating → Compensated vs Compensating → CompFault
- **Scheduler naturally fits** — WaitingRetry maps to Scheduler re-enqueue

### Negative
- **More states to handle** — 13 states vs 7
- **More code paths** — each transition needs validation
- **Audit explosion** — each transition is an event

---

## 5. Files Affected

| File | Change |
|------|--------|
| `core/runtime/runtime_types.hpp` | Replace `StepStatus` enum with `StepState` + `PlanState` |
| `core/runtime/plan_executor.hpp` | Handle all state transitions |
| `core/runtime/event_bus.hpp` | Add `StepTransitionEvent` type |
| `core/runtime/scheduler.hpp` | `WaitingRetry` → scheduler integration |
| Tests | Update all step-state assertions |

---

## References

- [RFC 0035 — Runtime Architecture](../RFC/0035-runtime-architecture.md)
- [RFC 0039 — NextAction Model](../RFC/0039-nextaction-model.md)
