// SPDX-License-Identifier: Apache-2.0
//
// SMO — Secure Mesh Operation
// Core Error Model
//
// This header defines the complete error hierarchy for the SMO runtime.
// Every module returns Result<T, Error>. No exceptions are used for
// control flow within the runtime (C++ exceptions MAY be used for
// constructor failures in internal infrastructure only).
//
// Architecture invariants preserved:
//   I-04: Deterministic — errors are values, not control flow
//   I-05: Auditable — every error is loggable with category + code
//   I-19: Every FSM state has timeout and failure transition
//   Rule 3: No silent mutation — errors are never swallowed
//   Rule 9: Every state has a failure transition
//
// Design principles:
//   1. Errors are values (std::error_code pattern).
//   2. Every error has a numeric code, a category, and a human-readable message.
//   3. Errors are partitioned by module — no monolithic error enum.
//   4. Every error specifies: retryable? fatal? log level? recovery strategy?
//   5. The caller decides retry policy; the error tells the caller what is safe.

#pragma once

#include <cstdint>
#include <new>
#include <string>
#include <system_error>
#include <type_traits>

namespace smo {

// ---------------------------------------------------------------------------
// Error severity — attached to every error for filtering and escalation
// ---------------------------------------------------------------------------
enum class Severity : uint8_t {
    Debug     = 0,  // Verbose diagnostic, not actionable
    Info      = 1,  // Normal operational event (e.g., retry attempt)
    Warn      = 2,  // Unexpected but handled; operator should investigate
    Error     = 3,  // Operation failed; automatic recovery attempted
    Critical  = 4,  // Node or mesh integrity at risk; immediate action required
    Alert     = 5,  // Security event (key compromise, policy violation)
};

// ---------------------------------------------------------------------------
// Retry classification — tells the caller whether retry is safe/possible
// ---------------------------------------------------------------------------
enum class RetryClass : uint8_t {
    NoRetry       = 0,  // Retry will produce the same failure; escalate
    RetrySafe     = 1,  // Idempotent; safe to retry immediately
    RetryBackoff  = 2,  // Safe with exponential backoff (rate-limited)
    RetryNever    = 3,  // Retry is dangerous (e.g., nonce replay would trigger)
};

// ---------------------------------------------------------------------------
// Recovery strategy — what the runtime SHOULD do when this error occurs
// ---------------------------------------------------------------------------
enum class Recovery : uint8_t {
    None            = 0,  // No automatic recovery; escalate to operator
    RetryOperation  = 1,  // Retry the same operation (caller decides backoff)
    Reconnect       = 2,  // Close and re-establish session
    Reenroll        = 3,  // Obtain new certificate and rejoin mesh
    RestartFSM      = 4,  // Reset FSM to safe state and retry transition
    RebootNode      = 5,  // Node daemon restart required
    GovernanceVote  = 6,  // Multi-signature governance required
    ManualIntervention = 7, // Human operator must act
};

// ---------------------------------------------------------------------------
// Error category — mirrors module boundaries from §V
// ---------------------------------------------------------------------------
enum class ErrorCategory : uint8_t {
    Crypto      = 1,   // core/identity/ + protocol/signing/ + protocol/encryption/
    Identity    = 2,   // core/identity/ — keypair lifecycle
    Certificate = 3,   // core/mesh/ + core/enroll/ — cert chain, enrollment
    Transport   = 4,   // transport/tcp/, transport/udp/, transport/framing/
    Discovery   = 5,   // protocol/discovery/ + routing
    Session     = 6,   // core/session/ + protocol/control/ (SESSION_OPEN/CLOSE)
    Protocol    = 7,   // protocol/* — message serialization, opcode dispatch
    Runtime     = 8,   // runtime/* — scheduler, FSM, executor, audit, sandbox
    Governance  = 9,   // protocol/control/ (GOVERNANCE_PROPOSAL), acl/policy/
    Storage     = 10,  // storage/* — all 8 stores
    Internal    = 11,  // Unexpected invariant violation, resource exhaustion, bug
    Compiler    = 12,  // compiler/* — parse, plan, validate, DAG build
    Trust       = 13,  // trust/* + consensus/* — scoring, witness
};

// ---------------------------------------------------------------------------
// ErrorCode — discriminated union of category-specific error codes
// Used in Result<T, Error> and as std::error_code
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct ErrorCode {
    ErrorCategory category : 8;
    uint16_t       code    : 10;  // 0-1023 codes per category
    Severity       severity : 3;
    RetryClass     retry    : 2;
    Recovery       recovery : 4;

    constexpr ErrorCode() noexcept
        : category(ErrorCategory::Internal), code(0),
          severity(Severity::Error), retry(RetryClass::NoRetry),
          recovery(Recovery::None) {}

    constexpr ErrorCode(ErrorCategory cat, uint16_t c, Severity sev,
                         RetryClass r, Recovery rec) noexcept
        : category(cat), code(static_cast<uint16_t>(c & 0x3FF)), severity(sev), retry(r), recovery(rec) {}

    constexpr bool is_retryable() const noexcept {
        return retry == RetryClass::RetrySafe ||
               retry == RetryClass::RetryBackoff;
    }

    constexpr bool is_fatal() const noexcept {
        return severity >= Severity::Critical;
    }
};
#pragma pack(pop)
 
// ---------------------------------------------------------------------------
// Error — rich error value returned by every fallible function
// ---------------------------------------------------------------------------
class Error {
public:
    ErrorCode       code;
    std::string     message;       // Human-readable description
    const char*     source_file;   // __FILE__ where error originated
    int             source_line;   // __LINE__ where error originated
    uint64_t        timestamp_ns;  // Monotonic timestamp at error creation

    Error() noexcept
        : code(), message(), source_file(nullptr), source_line(0),
          timestamp_ns(monotonic_ns()) {}

    Error(ErrorCode ec, std::string msg,
          const char* file = nullptr, int line = 0) noexcept
        : code(ec), message(std::move(msg)),
          source_file(file), source_line(line),
          timestamp_ns(monotonic_ns()) {}

    static uint64_t monotonic_ns() noexcept;

    std::string to_string() const;
};

// ---------------------------------------------------------------------------
// Result<T, E> — either a value of type T or an Error
// ---------------------------------------------------------------------------
template <typename T>
class Result {
    static_assert(!std::is_same_v<T, Error>,
                  "Result<Error> is not allowed. Use Result<void> instead.");

    union { T value_; };
    Error error_;
    bool  has_value_ = false;

    void destroy() noexcept {
        if (has_value_) { value_.~T(); }
    }

public:
    Result(T val) noexcept(std::is_nothrow_move_constructible_v<T>)
        : value_(std::move(val)), has_value_(true) {}

    Result(Error err) noexcept
        : error_(std::move(err)), has_value_(false) {}

    ~Result() noexcept { destroy(); }

    Result(Result&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
        : has_value_(other.has_value_)
    {
        if (has_value_) {
            ::new (&value_) T(std::move(other.value_));
            other.value_.~T();
        } else {
            error_ = std::move(other.error_);
        }
        other.has_value_ = false;
    }

    Result& operator=(Result&&) = delete;
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;

    explicit operator bool() const noexcept { return has_value_; }

    T& value() & {
        return value_;
    }

    T&& value() && {
        return std::move(value_);
    }

    Error& error() & {
        return error_;
    }

    Error&& error() && {
        return std::move(error_);
    }

    // Unsafe accessors — caller MUST check operator bool first
    T& unsafe_value() noexcept { return value_; }
    Error& unsafe_error() noexcept { return error_; }
};

// Specialization for void — no value, only error or success
template <>
class Result<void> {
    Error error_{};
    bool  has_error_ = false;

public:
    Result() noexcept : has_error_(false) {}
    Result(Error err) noexcept : error_(std::move(err)), has_error_(true) {}

    explicit operator bool() const noexcept { return !has_error_; }
    bool has_error() const noexcept { return has_error_; }
    Error& error() & { return error_; }
    Error&& error() && { return std::move(error_); }

    // Discard result — useful when caller doesn't care about error but wants to compile
    void ignore() const noexcept {}
};

// ---------------------------------------------------------------------------
// Helper macros for creating errors with source location
// ---------------------------------------------------------------------------
#define SMO_ERR(cat, code, sev, retry, rec, msg) \
    smo::Error( \
        smo::ErrorCode( \
            smo::ErrorCategory::cat, code, \
            smo::Severity::sev, \
            smo::RetryClass::retry, \
            smo::Recovery::rec \
        ), \
        msg, __FILE__, __LINE__ \
    )

#define SMO_ERR_CRYPTO(code, sev, retry, rec, msg) \
    SMO_ERR(Crypto, code, sev, retry, rec, msg)
#define SMO_ERR_IDENTITY(code, sev, retry, rec, msg) \
    SMO_ERR(Identity, code, sev, retry, rec, msg)
#define SMO_ERR_CERT(code, sev, retry, rec, msg) \
    SMO_ERR(Certificate, code, sev, retry, rec, msg)
#define SMO_ERR_TRANSPORT(code, sev, retry, rec, msg) \
    SMO_ERR(Transport, code, sev, retry, rec, msg)
#define SMO_ERR_DISCOVERY(code, sev, retry, rec, msg) \
    SMO_ERR(Discovery, code, sev, retry, rec, msg)
#define SMO_ERR_SESSION(code, sev, retry, rec, msg) \
    SMO_ERR(Session, code, sev, retry, rec, msg)
#define SMO_ERR_PROTOCOL(code, sev, retry, rec, msg) \
    SMO_ERR(Protocol, code, sev, retry, rec, msg)
#define SMO_ERR_RUNTIME(code, sev, retry, rec, msg) \
    SMO_ERR(Runtime, code, sev, retry, rec, msg)
#define SMO_ERR_GOVERNANCE(code, sev, retry, rec, msg) \
    SMO_ERR(Governance, code, sev, retry, rec, msg)
#define SMO_ERR_STORAGE(code, sev, retry, rec, msg) \
    SMO_ERR(Storage, code, sev, retry, rec, msg)
#define SMO_ERR_INTERNAL(code, sev, retry, rec, msg) \
    SMO_ERR(Internal, code, sev, retry, rec, msg)
#define SMO_ERR_COMPILER(code, sev, retry, rec, msg) \
    SMO_ERR(Compiler, code, sev, retry, rec, msg)
#define SMO_ERR_TRUST(code, sev, retry, rec, msg) \
    SMO_ERR(Trust, code, sev, retry, rec, msg)

// ---------------------------------------------------------------------------
// SMO_TRY — early-return macro for Result<T>
//   Usage (value):  SMO_TRY_VAL(val, expr);
//   Usage (void):   SMO_TRY(expr);
// ---------------------------------------------------------------------------
#define SMO_TRY(expr)                                                      \
    do {                                                                   \
        auto _smo_r_ = (expr);                                             \
        if (!_smo_r_) { return std::move(_smo_r_).error(); }               \
    } while (false)

#define SMO_TRY_VAL(var, expr)                                             \
    auto _smo_r_ = (expr);                                                 \
    if (!_smo_r_) { return std::move(_smo_r_).error(); }                   \
    var = std::move(_smo_r_).value()

// ---------------------------------------------------------------------------
// std::error_code integration (for compatibility with std::expected in C++23)
// ---------------------------------------------------------------------------
class ErrorCategoryImpl : public std::error_category {
public:
    const char* name() const noexcept override;
    std::string message(int ev) const override;
};

} // namespace smo
