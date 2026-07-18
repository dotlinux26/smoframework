# RFC 0044 — Runtime Scheduler

**Status:** DRAFT — Proposed for Sprint 37  
**Date:** 2026-07-18  
**Extends:** RFC 0035 (Runtime Architecture), RFC 0039 (NextAction Model)

---

## 1. Motivation

Current execution is synchronous:

```
RuntimeKernel::execute()
    ↓ contract
    ↓ return
```

There is no way to:
- Queue work for later execution
- Spawn parallel tasks from a contract
- Schedule retries with backoff
- Run periodic or cron-based contracts
- Chain contract executions asynchronously

Contracts that return `NextAction` currently rely on `PlanExecutor` to process them inline. But `PlanExecutor` only handles the current request — it does not persist or schedule future work.

**What we need:**

```
Contract → NextAction → Scheduler → WorkerPool → ActionExecutor → ...
```

The Scheduler is the central coordination point. Unlike v1, the Scheduler does **not** know about `RuntimeKernel`. It only manages job lifecycle (enqueue, cancel, pause). The `ActionExecutor` (from RFC 0041) handles dispatch to transport, kernel, or other services.

---

## 2. Architecture

```
┌─────────────────────────────────────────────────────┐
│                   RuntimeKernel                      │
│  execute() → plan → NextActions → Scheduler.submit()│
└─────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────┐
│                     Scheduler                         │
│                                                       │
│  ┌────────────────────┐    ┌───────────────────┐    │
│  │     JobQueue       │    │   CronService     │    │
│  │  ┌─ Priority Queue─┤    │  parse expression  │    │
│  │  │  (min-heap)     │    │  find next run     │    │
│  │  ├─ Delayed Queue ─┤    │  submit to queue   │    │
│  │  │  (sorted set)   │    └───────────────────┘    │
│  │  └─────────────────┘                             │
│  └────────────────────┘                             │
│              │                                       │
│              ▼                                       │
│  ┌────────────────────┐                             │
│  │    WorkerPool      │   N threads (configurable) │
│  │  worker_loop():    │                             │
│  │   pop job → execute│                             │
│  └─────────┬──────────┘                             │
└────────────┼────────────────────────────────────────┘
             │
             ▼
┌─────────────────────────────────────────────────────┐
│                  ActionExecutor                      │
│                                                      │
│  DispatchContract → RuntimeBridge → Kernel → loop   │
│  DispatchMessage → Transport.send()                  │
│  ScheduleRetry   → Scheduler.submit_delayed()       │
│  EmitEvent       → EventBus.publish()               │
│  StoreContext    → RuntimeContext.set()              │
│  SpawnPlan       → resolve plan, submit steps       │
│  Notify          → deliver notification             │
│  Compensate      → rollback with high priority      │
│  Abort           → abort with highest priority      │
└─────────────────────────────────────────────────────┘
```

---

## 3. Design

### 3.1 Scheduler (Orchestrator Only)

The Scheduler owns the queue and the worker pool. It does **not** interpret `NextAction` types and does **not** hold a reference to `RuntimeKernel`.

```cpp
class Scheduler {
public:
    Scheduler(size_t worker_count = 4);

    void start();
    void stop();

    // ── Submission ──────────────────────────────────────
    // Submit a job for immediate execution (joins priority queue)
    Result<void> submit(Job job);

    // Submit a job to run after a delay
    Result<void> submit_delayed(Job job, uint64_t delay_ns);

    // ── Lifecycle ─────────────────────────────────────
    Result<void> cancel(const std::string& job_id);

    // ── Stats ─────────────────────────────────────────
    size_t pending_count() const;
    size_t running_count() const;

private:
    JobQueue queue_;
    WorkerPool pool_;
    CronService cron_;
    bool running_ = false;
};
```

**Key principle:** `Scheduler` does not know about `RuntimeKernel`, `RuntimeBridge`, or `Transport`. It is purely a job lifecycle manager.

### 3.2 Job

A `Job` wraps any execution unit. The Scheduler treats it as opaque:

```cpp
struct Job {
    std::string id;                   // unique job ID
    std::function<Result<void>()> task;  // the actual work
    std::string source_id;            // for tracing

    // Priority (lower = higher priority)
    uint32_t priority = 100;

    // Retry policy
    uint32_t max_retries = 0;
    uint64_t base_delay_ns = 1'000'000;      // 1 ms
    double backoff_multiplier = 2.0;
    uint32_t retry_count = 0;

    // Cron (if periodic)
    std::string cron_expr;
    int64_t scheduled_at_ns = 0;     // 0 = immediate
    int64_t last_run_ns = 0;
};
```

**When a job fails**, the `WorkerPool` checks `retry_count < max_retries`, computes `delay = base_delay_ns * backoff_multiplier^retry_count`, and calls `scheduler.submit_delayed(job, delay)`.

### 3.3 JobQueue

```cpp
class JobQueue {
public:
    void push(Job job);      // priority min-heap
    void push_delayed(Job job, int64_t at_ns);  // sorted set
    Job pop();               // pop highest-priority ready job
    Job pop_delayed();       // pop jobs whose scheduled_at_ns ≤ now
    Result<void> cancel(const std::string& id);
    size_t size() const;

private:
    // priority_queue: min-heap by priority
    // delayed_queue: multiset ordered by scheduled_at_ns
    // hash_map: id → iterator for O(1) cancel
};
```

The `JobQueue` is a pure data structure — no threads, no execution logic.

### 3.4 WorkerPool

```cpp
class WorkerPool {
public:
    WorkerPool(size_t count, Scheduler* scheduler, ActionExecutor* executor);
    void start();
    void stop();

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool running_ = false;
};
```

Worker loop:

```cpp
void WorkerPool::worker_loop() {
    while (running_) {
        Job job = queue_.pop();  // blocks until job available

        auto result = executor_->execute(job.task);

        if (!result && job.retry_count < job.max_retries) {
            job.retry_count++;
            uint64_t delay = job.base_delay_ns *
                static_cast<uint64_t>(std::pow(job.backoff_multiplier, job.retry_count));
            scheduler_->submit_delayed(std::move(job), delay);
        }

        // Check cron for next run
        if (!job.cron_expr.empty()) {
            auto next_run = cron_next(job.cron_expr, now());
            if (next_run) {
                job.scheduled_at_ns = *next_run;
                scheduler_->submit_delayed(std::move(job),
                    *next_run - now());
            }
        }
    }
}
```

### 3.5 CronService

```cpp
class CronService {
public:
    // Parse standard 5-field cron expression
    // Returns next absolute timestamp (ns since epoch)
    std::optional<int64_t> next_run(const std::string& expr, int64_t now_ns);
};
```

The `CronService` is a utility — it computes the next run time. The `WorkerPool` uses it to reschedule periodic jobs.

### 3.6 ActionExecutor (from RFC 0041)

The `ActionExecutor` handles all `NextAction` dispatch. It is the only component that knows about `RuntimeBridge`, `Transport`, and `RuntimeKernel`:

```cpp
class ActionExecutor {
public:
    ActionExecutor(RuntimeBridge* bridge, Transport* transport, Scheduler* scheduler);

    // Execute a NextAction — the job's task wraps this call
    Result<void> execute(NextAction& action);

private:
    RuntimeBridge* bridge_;       // for ActionDispatchContract → Kernel → loop
    Transport* transport_;        // for ActionDispatchMessage
    Scheduler* scheduler_;        // for ActionScheduleRetry (requeue)
    // EventBus* event_bus_;      // future (RFC 0045)
};
```

**Contract chaining via ActionExecutor (no circular dependency):**

```
WorkerPool → ActionExecutor::execute(ActionDispatchContract)
                 ↓
          RuntimeBridge::bridge(RuntimeRequest)
                 ↓
          RuntimeKernel::execute()
                 ↓
          vector<NextAction>
                 ↓
          Scheduler::submit(job)  ← non-blocking, returns immediately
```

The kernel does not call the scheduler back. The scheduler receives new jobs through the normal `submit()` path.

---

## 4. Integration with RuntimeKernel

### 4.1 Kernel → Scheduler (Via ActionExecutor)

```cpp
// In RuntimeKernel::execute(), after PlanExecutor:
Result<RuntimeResult> RuntimeKernel::execute(const RuntimeRequest& req) {
    // ... plan execution ...

    // Collect NextActions — they are returned in RuntimeResult
    // The caller (RuntimeBridge) submits them to Scheduler via ActionExecutor
    return RuntimeResult{std::move(next_actions)};
}
```

The kernel itself does not call the scheduler. It returns `NextAction`s, and the `RuntimeBridge` (wired to `ActionExecutor`) submits them as jobs.

### 4.2 Contract Chaining Flow

```
Kernel returns:
  NextAction { ActionDispatchContract { "system.bootstrap", input } }

RuntimeBridge → ActionExecutor:
  ActionExecutor::on_dispatch_contract(action)
    → RuntimeRequest req { .contract_id = "system.bootstrap", .input = ... }
    → RuntimeBridge::bridge(req)   // re-enters Kernel
    → Kernel returns new RuntimeResult with next_actions
    → For each next_action:
        Scheduler::submit(Job{task = [=]{ executor.execute(next_action); }})
```

This is fully asynchronous — no blocking, no recursive call stack.

---

## 5. Scheduler in the Big Picture

```
             CLI                     Remote Node
              │                          │
              ▼                          │
      RuntimeRequest ◄───────────────────┘
              │                     (via RuntimeBridge)
              ▼
      RuntimeKernel::execute()
              │
              ▼
      PlanExecutor
              │
              ▼
      vector<NextAction>
              │  (returned to ActionExecutor via RuntimeBridge)
              ▼
      ┌────────────────┐
      │  Scheduler      │
      │  .submit(job)   │
      └───────┬────────┘
              │
              ▼
      ┌────────────────┐
      │  WorkerPool     │
      │  pop → execute  │
      └───────┬────────┘
              │
              ▼
      ┌────────────────┐
      │ ActionExecutor  │
      │ switch(action): │
      └───┬────┬────┬───┘
          │    │    │
          ▼    ▼    ▼
        TCP  Kernel EventBus
        send (via   (future)
             Bridge)
```

---

## 6. Component Responsibilities

| Component | Responsibility | Knows About |
|-----------|---------------|-------------|
| `Scheduler` | enqueue, cancel, pause/resume | Job, JobQueue, WorkerPool, CronService |
| `JobQueue` | priority queue + delayed queue | Job (opaque) |
| `WorkerPool` | thread pool, job execution loop | Job, ActionExecutor |
| `CronService` | cron expression parser | — |
| `ActionExecutor` | NextAction dispatch | RuntimeBridge, Transport, EventBus (future) |

No circular dependencies:
- `Scheduler → WorkerPool → ActionExecutor → RuntimeBridge → Kernel`
- `Kernel → RuntimeResult → (via ActionExecutor) → Scheduler`
- The direction is **linear**, never circular.

---

## 7. Consequences

### Positive
- **No circular dependency** — Scheduler does not know Kernel
- **Separation of concerns** — each component has exactly one job
- **Async by default** — contracts never block waiting for responses
- **Contract chaining** — via ActionExecutor → RuntimeBridge → Kernel → loop
- **Retry with backoff** — configurable per-job
- **Cron support** — periodic contract execution via CronService
- **Priority queue** — important actions skip the line
- **Worker pool** — parallel execution without blocking the Kernel
- **Scheduler is generic** — could schedule non-runtime jobs too

### Negative
- **Threading complexity** — mutex, condition variable, thread pool management
- **State management** — jobs must be serializable for crash recovery
- **Backpressure** — queue could grow unbounded under load
- **Cancellation** — cancelling an in-flight job is non-trivial

---

## 8. Migration Path

1. Create `Job` struct (opaque task, priority, retry policy, cron)
2. Create `JobQueue` (priority min-heap + delayed sorted set)
3. Create `WorkerPool` (N threads, pop → execute → retry logic)
4. Create `Scheduler` (orchestrates queue + pool + cron)
5. Wire `ActionExecutor::on_dispatch_contract()` → RuntimeBridge → Kernel → loop
6. Wire `RuntimeKernel::execute()` to return NextActions (does not call Scheduler directly)
7. Add cron expression parser

---

## 9. Files Affected

| File | Change |
|------|--------|
| `core/runtime/scheduler.hpp` | New — Scheduler, Job, JobQueue |
| `core/runtime/scheduler.cpp` | New — Scheduler, JobQueue implementation |
| `core/runtime/worker_pool.hpp` | New — WorkerPool |
| `core/runtime/worker_pool.cpp` | New — thread pool |
| `core/runtime/cron_service.hpp` | New — CronService (cron parser) |
| `core/runtime/cron_service.cpp` | New — implementation |
| `core/runtime/runtime_kernel.hpp` | No change to Kernel (NextActions returned, not submitted) |
| `core/runtime/action_executor.hpp` | Add contract chaining via RuntimeBridge |

---

## References

- [RFC 0035 — Runtime Architecture](../RFC/0035-runtime-architecture.md)
- [RFC 0039 — NextAction Model](../RFC/0039-nextaction-model.md)
- [RFC 0041 — Runtime Bridge](../RFC/0041-runtime-bridge.md)
