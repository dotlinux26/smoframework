# RFC 0008 — Error Model

## Status
ACCEPTED — incorporated into SPEC.md §V and core/errors/error_codes.md.

## Problem
Every SMO function must return a consistent error type. Without a unified error model, callers cannot distinguish retryable failures from fatal ones, recovery strategies become ad-hoc, and audit logs lose semantic value.

## Decisions

### 1. `Result<T, Error>` is the universal return type
Every public function returns `Result<T, Error>`. There is no exception-based error handling in the runtime core. Propagation uses `SMO_TRY(err)` macro that returns the error on failure.

### 2. `Error` carries five fields

| Field | Type | Bits | Purpose |
|---|---|---|---|
| `category` | `ErrorCategory` | 8 | Origin module (13 categories) |
| `code` | `uint16_t` | 10 | Numeric code (0-1023 per category) |
| `severity` | `Severity` | 3 | D / I / W / E / C / A |
| `retry` | `RetryClass` | 2 | NoRetry / Safe / Backoff / Never |
| `recovery` | `Recovery` | 3 | Hints: Reconnect, Enroll, FSM, Boot, Governance, Manual |

Total: 23 bits, packed into `uint32_t error_bits`.

### 3. 13 error categories (1 per SPEC §V module)
Crypto, Identity, Certificate, Transport, Discovery, Session, Protocol, Runtime, Governance, Storage, Internal, Compiler, Trust.

Each category owns codes 0-1023. First assignment: 187 codes total across 13 categories.

### 4. Severity determines node-level action
- Debug/Info: logged, no escalation
- Warn: logged + rate-limited alert
- Error: operation fails, caller notified
- Critical: node self-quarantines, rejects new contracts
- Alert: emergency broadcast to mesh (e.g., AEAD_TAG_MISMATCH)

### 5. Retry class drives caller behavior
- NoRetry: return error immediately
- RetrySafe: retry with same parameters (idempotent-safe)
- RetryBackoff: retry with exponential backoff
- RetryNever: never retry (security violations, invariant breaches)

### 6. Source location is mandatory
Every `SMO_ERR(cat, code, msg)` macro captures `__FILE__` and `__LINE__` automatically. This is not optional debug information — it is part of the contract.

## Interfaces

```cpp
enum class ErrorCategory : uint8_t {
    Crypto, Identity, Certificate, Transport,
    Discovery, Session, Protocol, Runtime,
    Governance, Storage, Internal, Compiler, Trust
};

enum class Severity : uint8_t {
    Debug, Info, Warn, Error, Critical, Alert
};

enum class RetryClass : uint8_t {
    NoRetry, RetrySafe, RetryBackoff, RetryNever
};

enum class Recovery : uint8_t {
    None, Reconnect, Enroll, FSM, Boot, Governance, Manual
};

struct ErrorCode {
    ErrorCategory category : 8;
    uint16_t        code    : 10;
    Severity        severity : 3;
    RetryClass      retry   : 2;
    Recovery        recovery : 3;
};

struct Error {
    ErrorCode     code;
    std::string   message;
    const char*   source_file;
    int           source_line;
    uint64_t      timestamp_ns;  // monotonic clock
};

template<typename T>
struct Result {
    T* value;
    Error* error;
    bool ok() const;
    T& unwrap();        // asserts ok
    Error& unwrap_err(); // asserts !ok
};

// Specialization for void
template<>
struct Result<void> {
    Error* error;
    bool ok() const;
};
```

## Consequences
- Every module now has a consistent error vocabulary. No ad-hoc `-1` returns.
- Audit logs carry structured error metadata, enabling automated post-mortem analysis.
- 13-category scheme leaves room for growth: each category has 1024 code slots.
- Source location constraint prevents error creation without context.
- `Result<T>` replaces `std::optional` + `std::error_code` patterns.
