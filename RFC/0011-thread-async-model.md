# RFC 0011 — Thread Safety & Async Model

## Status
ACCEPTED — epoll-first reactor + worker pool + SPSC queues. io_uring as optional accelerator.

## Problem
SMO must support concurrent contract execution, multiple transport connections, scheduled DAG tasks, and audit logging — all within a single process. Without an explicit thread and async model, data races, deadlocks, and non-deterministic behavior will plague the runtime.

## Decisions

### 1. Overall model: reactor + bounded thread pool
The runtime uses a single-threaded reactor (event loop) for I/O and coordination, and a bounded thread pool for CPU-bound execution (crypto operations, DAG task execution, plugin calls). This is the **reactor-per-subsystem** pattern, not full actor model.

| Thread Role | Count | Responsibility |
|---|---|---|
| Main reactor | 1 | Transport I/O, FSM transitions, session management, trust score ticks |
| Worker pool | N (configurable, default = CPU cores) | Contract execution, plugin calls, crypto operations, storage writes |
| Timer thread | 1 | Heartbeat scheduler, FSM timeout watchers, decay clock |
| Admin RPC | 1 (optional) | `smo-admin` commands over Unix socket |

### 2. Ownership rules (who owns what)
| Object | Owner Thread | Shared? | Synchronization |
|---|---|---|---|
| Transport socket | Main reactor | No | — |
| FSM instance | Main reactor | No | — |
| Session state | Main reactor | Read from worker pool | Atomic shared_ptr + lock-free state flag |
| DAG node | Worker pool | No (moved to worker) | — |
| Audit log | Worker pool | Append-only from workers | Lock-free SPSC queue → main reactor flushes |
| Trust store | Main reactor | Read from workers | `std::shared_mutex` (shared_lock for read, unique_lock for write) |
| Node store | Main reactor | No | — |
| Keypair (crypto) | Worker pool | No (copied per operation) | — |

### 3. Communication: message queues, not shared state
Cross-thread communication uses lock-free SPSC (Single Producer Single Consumer) channels:

- Worker → Main reactor: `AuditEvent`, `ExecutionResult`, `FSMEvent`
- Main reactor → Worker: `ExecuteTask`, `CryptoOperation`

This prevents accidental shared mutable state. The main reactor is the single point of truth for FSM state — workers never mutate it directly.

### 4. No std::async or std::thread::detach
Every thread is explicitly managed by the runtime. Thread creation/destruction follows the worker pool lifecycle. No fire-and-forget tasks.

### 5. Deterministic scheduling within the main reactor
The main reactor processes events in FIFO order per priority class:
1. Timer events (heartbeat, timeouts)
2. Transport I/O events (incoming packets)
3. Worker completion events (execution results)
4. Admin commands

Within each class, events are processed in submission order. This guarantees deterministic replay given the same event sequence.

### 6. Async I/O via io_uring (Linux 5.1+)
For transport I/O: `io_uring` is the primary backend. Fallback to `epoll` for older kernels. This is abstracted behind the reactor interface — no business logic knows about io_uring vs epoll.

## Interfaces

```cpp
class Reactor {
    virtual Result<void> post(Event event, Priority priority) = 0;
    virtual Result<void> run_once(Duration timeout) = 0;  // process one batch
    virtual Result<void> run_forever() = 0;
};

class WorkerPool {
    virtual Result<Handle> submit(Task task) = 0;
    virtual Result<void> cancel(Handle handle) = 0;
    virtual Result<size_t> active_count() = 0;
    virtual Result<void> resize(size_t new_size) = 0;
};

// Lock-free SPSC channel (worker → main)
template<typename T>
class OutputChannel {
    bool try_push(T&& event);
    std::optional<T> try_pop();
};

// Thread ownership documentation marker
// Every class declares its owner thread in a comment:
//   // Owner: main_reactor
//   // Owner: worker_pool
//   // Thread-safe (immutable)
//   // Thread-safe (mutex)
```

## Consequences
- Single-threaded main reactor eliminates entire classes of concurrency bugs (race conditions on FSM state, session state, trust state).
- Worker pool isolates CPU-bound execution without blocking I/O.
- Lock-free SPSC channels avoid mutex contention for the hot path (audit logging, execution results).
- `io_uring` provides true async I/O without per-connection thread overhead.
- Deterministic event ordering in the reactor enables reliable replay testing.
- Thread ownership annotations in class headers make concurrency reasoning explicit during code review.
