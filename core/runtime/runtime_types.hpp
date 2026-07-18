#pragma once

#include "core/types.hpp"
#include "core/errors/error.hpp"
#include "core/crypto/impl.hpp"
#include "event_bus.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <chrono>
#include <cstdint>
#include <variant>
#include <atomic>
#include <optional>
#include <queue>
#include <bitset>

namespace smo::runtime {

// ── Forward declarations ─────────────────────────────────────────────
class Dispatcher;
class OutputManager;
class PolicyEngine;
class Scheduler;
struct RuntimeContext;
struct RuntimeServices;

// =====================================================================
// RuntimeError
// =====================================================================
class RuntimeError : public std::runtime_error {
public:
    enum class Category {
        Unknown, Validation, NotFound, Unauthorized, PolicyDenied,
        Timeout, Network, Resource, Internal, Contract, Compensation
    };

    Category category;
    int code;
    bool retryable;
    bool fatal;

    RuntimeError() : RuntimeError(Category::Unknown, 0, "ok", false, false) {}

    RuntimeError(Category cat, int code, const std::string& msg, bool retryable, bool fatal)
        : std::runtime_error(msg), category(cat), code(code), retryable(retryable), fatal(fatal) {}

    operator Error() const {
        ErrorCategory ecat = ErrorCategory::Runtime;
        Severity sev = fatal ? Severity::Critical : Severity::Error;
        RetryClass rc = retryable ? RetryClass::RetrySafe : RetryClass::NoRetry;
        Recovery rec = fatal ? Recovery::ManualIntervention : Recovery::RetryOperation;
        return Error(ErrorCode(ecat, static_cast<uint16_t>(code), sev, rc, rec), what());
    }

    static RuntimeError validation(const std::string& msg) { return {Category::Validation, 1000, msg, false, false}; }
    static RuntimeError not_found(const std::string& msg) { return {Category::NotFound, 1001, msg, false, false}; }
    static RuntimeError unauthorized(const std::string& msg) { return {Category::Unauthorized, 1002, msg, false, true}; }
    static RuntimeError policy_denied(const std::string& msg) { return {Category::PolicyDenied, 1003, msg, false, true}; }
    static RuntimeError timeout(const std::string& msg) { return {Category::Timeout, 1004, msg, true, false}; }
    static RuntimeError network(const std::string& msg) { return {Category::Network, 1005, msg, true, false}; }
    static RuntimeError resource(const std::string& msg) { return {Category::Resource, 1006, msg, false, false}; }
    static RuntimeError internal(const std::string& msg) { return {Category::Internal, 1007, msg, false, true}; }
    static RuntimeError contract(const std::string& msg) { return {Category::Contract, 1008, msg, false, false}; }
    static RuntimeError compensation(const std::string& msg) { return {Category::Compensation, 1009, msg, false, true}; }
};

// =====================================================================
// ContractCapability (RFC 0036 §3.5 + RFC 0037 §2.4)
// =====================================================================
enum class ContractCapability : size_t {
    None    = 0,
    Crypto  = 0,
    Vault   = 1,
    Network = 2,
    Filesystem = 3,
    Scheduler = 4,
    Governance = 5,
    Recovery = 6,
    Identity = 7,
    Storage = 8,
    Audit = 9,
    Metrics = 10,
};

using ContractCapabilities = std::bitset<64>;

// =====================================================================
// ExecutionMode / EventPriority (needed before Action structs)
// =====================================================================
enum class ExecutionMode : uint8_t { Sequential, Parallel, Pipeline };
enum class EventPriority : uint8_t { Low = 0, Normal = 1, High = 2, Critical = 3 };

// =====================================================================
// ContextValue (RFC 0036 §3.2)
// =====================================================================
class ContextValue {
public:
    using Value = std::variant<
        std::monostate, bool, int64_t, double, std::string, Bytes,
        std::unordered_map<std::string, std::string>,
        std::vector<std::string>
    >;

    ContextValue() = default;
    ContextValue(const ContextValue&) = default;
    ContextValue(ContextValue&&) = default;
    ContextValue& operator=(const ContextValue&) = default;
    ContextValue& operator=(ContextValue&&) = default;

    ContextValue(bool v) : value_(v) {}
    ContextValue(int64_t v) : value_(v) {}
    ContextValue(double v) : value_(v) {}
    ContextValue(const std::string& v) : value_(v) {}
    ContextValue(const char* v) : value_(std::string(v)) {}
    ContextValue(Bytes v) : value_(std::move(v)) {}
    ContextValue(const std::unordered_map<std::string, std::string>& v) : value_(v) {}
    ContextValue(const std::vector<std::string>& v) : value_(v) {}

    bool is_null() const { return std::holds_alternative<std::monostate>(value_); }
    bool is_bool() const { return std::holds_alternative<bool>(value_); }
    bool is_int64() const { return std::holds_alternative<int64_t>(value_); }
    bool is_double() const { return std::holds_alternative<double>(value_); }
    bool is_string() const { return std::holds_alternative<std::string>(value_); }
    bool is_bytes() const { return std::holds_alternative<Bytes>(value_); }
    bool is_map() const { return std::holds_alternative<std::unordered_map<std::string, std::string>>(value_); }
    bool is_array() const { return std::holds_alternative<std::vector<std::string>>(value_); }

    template<typename T> Result<T> get() const {
        if (std::holds_alternative<T>(value_)) return std::get<T>(value_);
        return Result<T>(static_cast<Error>(RuntimeError::internal("type mismatch")));
    }

    std::string to_string() const {
        return std::visit([](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) return "null";
            else if constexpr (std::is_same_v<T, bool>) return arg ? "true" : "false";
            else if constexpr (std::is_same_v<T, int64_t>) return std::to_string(arg);
            else if constexpr (std::is_same_v<T, double>) return std::to_string(arg);
            else if constexpr (std::is_same_v<T, std::string>) return arg;
            else if constexpr (std::is_same_v<T, Bytes>) return "[bytes:" + std::to_string(arg.size()) + "]";
            else if constexpr (std::is_same_v<T, std::unordered_map<std::string, std::string>>) {
                std::string s = "{";
                for (auto it = arg.begin(); it != arg.end(); ++it) {
                    if (it != arg.begin()) s += ", ";
                    s += "\"" + it->first + "\": " + it->second;
                }
                return s + "}";
            }
            else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                std::string s = "[";
                for (auto it = arg.begin(); it != arg.end(); ++it) {
                    if (it != arg.begin()) s += ", ";
                    s += *it;
                }
                return s + "]";
            }
            else return "unknown";
        }, value_);
    }

    Result<ContextValue> operator[](const std::string& key) const {
        if (is_map()) {
            auto it = std::get<std::unordered_map<std::string, std::string>>(value_).find(key);
            if (it != std::get<std::unordered_map<std::string, std::string>>(value_).end())
                return ContextValue(it->second);
            return Result<ContextValue>(Error(ErrorCode{ErrorCategory::Internal, 1001, Severity::Error, RetryClass::NoRetry, Recovery::None}, "key not found: " + key));
        }
        return Result<ContextValue>(Error(ErrorCode{ErrorCategory::Internal, 1002, Severity::Error, RetryClass::NoRetry, Recovery::None}, "not a map"));
    }

private:
    Value value_;
};

// =====================================================================
// ContractInput (RFC 0036 §3.2)
// =====================================================================
struct ContractInput {
    std::string method;
    ContextValue arguments;

    static ContractInput method_only(const std::string& method) {
        return {method, ContextValue()};
    }
    static ContractInput with_map(const std::string& method,
                                   const std::unordered_map<std::string, std::string>& map) {
        return {method, ContextValue(map)};
    }
    static ContractInput with_string(const std::string& method,
                                      const std::string& data) {
        return {method, ContextValue(data)};
    }
};

// =====================================================================
// ContractConfig (RFC 0036 §3.6)
// =====================================================================
struct ContractConfig {
    std::unordered_map<std::string, std::string> settings;
    ContractCapabilities granted_capabilities;
    std::string data_dir;
    uint64_t max_execution_time_ns = 30'000'000'000;
};

// =====================================================================
// ExecutionInfo (RFC 0037 §2.2)
// =====================================================================
struct ExecutionInfo {
    uint64_t execution_id = 0;
    uint64_t parent_execution_id = 0;
    std::string correlation_id;
    std::string requester;
    std::string contract_id;
    std::string node_id;
    std::string mesh_id;
    uint64_t epoch = 0;
    uint64_t deadline_ns = 0;
    uint64_t started_at_ns = 0;
    uint64_t budget_ns = 30'000'000'000;

    bool is_expired() const {
        if (deadline_ns == 0) return false;
        auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return now > deadline_ns;
    }
};

// =====================================================================
// Variables (RFC 0037 §2.3)
// =====================================================================
struct Variables {
    mutable std::unordered_map<std::string, ContextValue> store;

    void set(const std::string& key, ContextValue value) const {
        store[key] = std::move(value);
    }

    Result<ContextValue> get(const std::string& key) const {
        auto it = store.find(key);
        if (it == store.end())
            return Result<ContextValue>(static_cast<Error>(
                RuntimeError::not_found("variable not found: " + key)));
        return it->second;
    }

    template<typename T>
    Result<T> get(const std::string& key) const {
        auto v = get(key);
        if (!v) return Result<T>(v.error());
        return v.value().template get<T>();
    }

    bool has(const std::string& key) const { return store.find(key) != store.end(); }
    void erase(const std::string& key) const { store.erase(key); }
};

// =====================================================================
// NextAction — Action Types (RFC 0039)
// =====================================================================
struct ActionDispatchContract {
    std::string contract_id;
    ContractInput input;
};

struct ActionDispatchMessage {
    std::string opcode;
    std::vector<uint8_t> data;
    std::string target_node;
    uint64_t timeout_ns = 5'000'000'000;
};

struct ActionScheduleRetry {
    uint64_t delay_ns;
    uint64_t max_retries = 3;
    double backoff_multiplier = 2.0;
};

struct ActionEmitEvent {
    std::string event_type;
    std::string payload;
    EventPriority priority = EventPriority::Normal;
};

struct ActionStoreContext {
    std::string key;
    ContextValue value;
};

struct ActionSpawnPlan {
    std::string plan_id;
    std::unordered_map<std::string, std::string> plan_params;
    ExecutionMode mode = ExecutionMode::Sequential;
};

struct ActionNotify {
    std::string target;
    std::string message;
    std::unordered_map<std::string, std::string> metadata;
};

struct ActionCompensate {
    std::string compensation_plan_id;
    std::string reason;
};

struct ActionAbort {
    std::string reason;
    bool trigger_compensation = true;
};

using NextAction = std::variant<
    ActionDispatchContract,
    ActionDispatchMessage,
    ActionScheduleRetry,
    ActionEmitEvent,
    ActionStoreContext,
    ActionSpawnPlan,
    ActionNotify,
    ActionCompensate,
    ActionAbort
>;

// =====================================================================
// ContractResult (RFC 0036 §3.3)
// =====================================================================
struct ContractResult {
    enum class Status { Success, Denied, Pending, Retry, Compensated, Error };

    Status status = Status::Error;
    std::string data;
    std::vector<uint8_t> binary;
    std::vector<NextAction> next_actions;
    std::unordered_map<std::string, ContextValue> metrics;

    static ContractResult ok(std::string data = "") {
        return {Status::Success, std::move(data), {}, {}, {}};
    }
    static ContractResult denied(std::string reason = "") {
        return {Status::Denied, std::move(reason), {}, {}, {}};
    }
    static ContractResult pending(std::string execution_id = "") {
        return {Status::Pending, std::move(execution_id), {}, {}, {}};
    }
    static ContractResult retry_later(NextAction retry_action);
    static ContractResult with_next(NextAction action, std::string data = "") {
        return {Status::Success, std::move(data), {}, {std::move(action)}, {}};
    }
};

// =====================================================================
// ExecutionPlan
// =====================================================================
struct Step {
    std::string step_id;
    std::string contract_id;
    std::vector<std::string> depends_on;
    std::string input_template;
    std::string output_binding;
    std::string compensation_id;
    bool optional = false;
    uint64_t timeout_ns = 0;
    int max_retries = 0;
    uint64_t retry_delay_ns = 1'000'000'000;
};

struct ExecutionPlan {
    std::string plan_id;
    std::string plan_type;
    std::vector<Step> steps;
    ExecutionMode mode = ExecutionMode::Sequential;
    std::unordered_map<std::string, std::string> context;
    std::string rollback_policy = "AllOrNothing";
    uint64_t total_timeout_ns = 300'000'000'000;

    Result<void> validate() const {
        std::unordered_map<std::string, size_t> id_to_idx;
        for (size_t i = 0; i < steps.size(); ++i) {
            if (id_to_idx.count(steps[i].step_id))
                return Result<void>(static_cast<Error>(RuntimeError::validation("duplicate step id: " + steps[i].step_id)));
            id_to_idx[steps[i].step_id] = i;
        }
        for (const auto& step : steps) {
            for (const auto& dep : step.depends_on) {
                if (!id_to_idx.count(dep))
                    return Result<void>(static_cast<Error>(RuntimeError::validation("step '" + step.step_id + "' depends on unknown step '" + dep + "'")));
            }
        }
        std::unordered_map<std::string, int> indegree;
        std::unordered_map<std::string, std::vector<std::string>> adj;
        for (const auto& step : steps) indegree[step.step_id] = 0;
        for (const auto& step : steps) {
            for (const auto& dep : step.depends_on) {
                adj[dep].push_back(step.step_id);
                indegree[step.step_id]++;
            }
        }
        std::queue<std::string> q;
        for (const auto& [id, deg] : indegree) if (deg == 0) q.push(id);
        size_t visited = 0;
        while (!q.empty()) { auto u = q.front(); q.pop(); visited++; for (const auto& v : adj[u]) if (--indegree[v] == 0) q.push(v); }
        if (visited != steps.size()) return Result<void>(static_cast<Error>(RuntimeError::validation("cycle detected in execution plan")));
        return {};
    }

    std::vector<std::string> topological_order() const {
        std::unordered_map<std::string, int> indegree;
        std::unordered_map<std::string, std::vector<std::string>> adj;
        for (const auto& step : steps) indegree[step.step_id] = 0;
        for (const auto& step : steps) {
            for (const auto& dep : step.depends_on) {
                adj[dep].push_back(step.step_id);
                indegree[step.step_id]++;
            }
        }
        std::queue<std::string> q;
        for (const auto& [id, deg] : indegree) if (deg == 0) q.push(id);
        std::vector<std::string> order;
        while (!q.empty()) { auto u = q.front(); q.pop(); order.push_back(u); for (const auto& v : adj[u]) if (--indegree[v] == 0) q.push(v); }
        return order;
    }
};

// =====================================================================
// StepState / PlanState (RFC 0038)
// =====================================================================
enum class StepState : uint8_t {
    Pending      = 0,
    Ready        = 1,
    Running      = 2,
    WaitingRetry = 3,
    Compensating = 4,
    Completed    = 10,
    Compensated  = 11,
    Failed       = 20,
    Cancelled    = 21,
    Skipped      = 22,
    CompFault    = 23,
};

enum class PlanState : uint8_t {
    Resolving    = 0,
    Ready        = 1,
    Executing    = 2,
    Completed    = 10,
    PartiallyDone = 11,
    Failed       = 20,
    Cancelled    = 21,
    Compensating = 30,
    Compensated  = 31,
};

struct StepStatus {
    StepState state = StepState::Pending;
    std::string output;
    ContractResult contract_result;
    std::string error_message;
    uint64_t started_at_ns = 0;
    uint64_t completed_at_ns = 0;
    uint64_t last_retry_at_ns = 0;
    int attempt = 0;
    int max_retries = 3;
    uint64_t retry_delay_ns = 1'000'000'000;
    uint64_t cumulative_wait_ns = 0;

    bool is_terminal() const { return static_cast<uint8_t>(state) >= 10; }
    bool is_success() const { return state == StepState::Completed || state == StepState::Compensated; }
    bool is_retryable() const { return attempt < max_retries; }
};

// =====================================================================
// PlanContext
// =====================================================================
struct PlanContext {
    std::string request_id;
    std::string plan_id;
    std::string execution_id;
    uint64_t started_ns = 0;
    uint64_t deadline_ns = 0;
    PlanState plan_state = PlanState::Resolving;

    std::unordered_map<std::string, std::string> context;
    std::unordered_map<std::string, StepStatus> step_status;

    uint64_t steps_completed = 0;
    uint64_t steps_failed = 0;
    std::string current_step;
    bool aborted = false;
    std::string abort_reason;

    EventBus* event_bus = nullptr;
    class Dispatcher* dispatcher = nullptr;
    class OutputManager* output = nullptr;
    class PolicyEngine* policy = nullptr;
    class Scheduler* scheduler = nullptr;
    struct RuntimeServices* services = nullptr;
};

// =====================================================================
// StepTransitionEvent (RFC 0038 §3)
// =====================================================================
struct StepTransitionEvent {
    std::string execution_id;
    std::string step_id;
    StepState from;
    StepState to;
    std::string reason;
    uint64_t timestamp_ns;
};

// =====================================================================
// ContractLifecycle (RFC 0040)
// =====================================================================
enum class ContractLifecycleState : uint8_t {
    Registered   = 0,
    Loaded       = 1,
    Initialized  = 2,
    Ready        = 3,
    Idle         = 4,
    Unloaded     = 10,
    LoadFailed   = 20,
    InitFailed   = 21,
    InitTimeout  = 22,
    CrashLoop    = 23,
};

struct ContractLifecycle {
    ContractLifecycleState state = ContractLifecycleState::Registered;
    std::string error_message;
    uint64_t loaded_at_ns = 0;
    uint64_t initialized_at_ns = 0;
    uint64_t last_execution_at_ns = 0;
    uint64_t total_executions = 0;
    uint64_t failed_executions = 0;
    uint64_t crash_count = 0;
    uint64_t next_retry_at_ns = 0;
};

// =====================================================================
// RuntimeRequest / RuntimeResult
// =====================================================================
struct RuntimeRequest {
    std::string contract_id;
    std::string requester;
    ContractInput input;
    RuntimeContext* context = nullptr;
    bool async = false;
    uint64_t deadline_ns = 0;
    uint32_t flags = 0;
    std::string request_id;
    enum Flag : uint32_t { ASYNC=1, NO_AUDIT=2, DRY_RUN=4, NO_RETRY=8, PRIORITY=16 };
    bool is_async() const { return flags & ASYNC; }
    bool no_audit() const { return flags & NO_AUDIT; }
    bool dry_run() const { return flags & DRY_RUN; }
    bool no_retry() const { return flags & NO_RETRY; }
};

struct RuntimeResult {
    enum class Status { Success, Denied, Timeout, Error, Pending, Compensated };
    Status status = Status::Error;
    std::string request_id;
    std::string execution_id;
    std::optional<ContractResult> output;
    std::vector<NextAction> next_actions;
    RuntimeError error;
    std::unordered_map<std::string, ContextValue> metrics;
    std::vector<std::string> events_emitted;
    uint64_t elapsed_ns = 0;

    bool is_success() const { return status == Status::Success; }
    bool is_retryable() const { return error.retryable; }

    RuntimeResult() noexcept = default;
    explicit RuntimeResult(Status s) noexcept : status(s) {}
};

// =====================================================================
// Middleware
// =====================================================================
enum class MiddlewareStage { BeforeResolve, BeforeDispatch, AfterDispatch, AfterCommit };

struct MiddlewareContext {
    RuntimeRequest* request;
    RuntimeContext* plan_context;
    std::string current_stage;
    std::string current_step_id;
    bool cancelled = false;
    std::string cancel_reason;
};

class ExecutionMiddleware {
public:
    virtual ~ExecutionMiddleware() = default;
    virtual Result<void> on_stage(MiddlewareStage stage, MiddlewareContext& ctx) = 0;
    virtual int priority() const = 0;
    virtual std::string name() const = 0;
    virtual bool applies_to(const std::string& contract_id) const = 0;
};

// =====================================================================
// PlanResolver
// =====================================================================
class PlanResolver {
public:
    using PlanProvider = std::function<ExecutionPlan(const std::string&)>;
    void register_provider(const std::string& prefix, PlanProvider provider) {
        providers_[prefix] = std::move(provider);
    }
    Result<ExecutionPlan> resolve(const std::string& plan_id) const {
        for (const auto& [prefix, prov] : providers_) {
            if (plan_id.rfind(prefix, 0) == 0) return prov(plan_id);
        }
        ExecutionPlan plan;
        plan.plan_id = plan_id;
        plan.mode = ExecutionMode::Sequential;
        Step s;
        s.step_id = "step-1";
        s.contract_id = plan_id;
        s.input_template = "{{payload}}";
        s.output_binding = "result";
        plan.steps.push_back(s);
        return plan;
    }
private:
    std::unordered_map<std::string, PlanProvider> providers_;
};

// =====================================================================
// overloaded helper (RFC 0039 §2.5)
// =====================================================================
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

// =====================================================================
// NextAction shorthand factories (RFC 0039 §2.6)
// =====================================================================
inline NextAction dispatch_contract(std::string id, ContractInput input = {}) {
    return ActionDispatchContract{std::move(id), std::move(input)};
}
inline NextAction dispatch_message(std::string opcode, std::vector<uint8_t> data,
                                    std::string target = "",
                                    uint64_t timeout = 5'000'000'000) {
    return ActionDispatchMessage{std::move(opcode), std::move(data),
                                  std::move(target), timeout};
}
inline NextAction schedule_retry(uint64_t delay_ns = 1'000'000'000,
                                  uint64_t max_retries = 3, double backoff = 2.0) {
    return ActionScheduleRetry{delay_ns, max_retries, backoff};
}
inline NextAction emit_event(std::string type, std::string payload = "",
                              EventPriority prio = EventPriority::Normal) {
    return ActionEmitEvent{std::move(type), std::move(payload), prio};
}
inline NextAction store_context(std::string key, ContextValue value) {
    return ActionStoreContext{std::move(key), std::move(value)};
}
inline NextAction spawn_plan(std::string plan_id,
                              std::unordered_map<std::string, std::string> params = {},
                              ExecutionMode mode = ExecutionMode::Sequential) {
    return ActionSpawnPlan{std::move(plan_id), std::move(params), mode};
}
inline NextAction notify_action(std::string target, std::string message,
                          std::unordered_map<std::string, std::string> meta = {}) {
    return ActionNotify{std::move(target), std::move(message), std::move(meta)};
}
inline NextAction abort_plan(std::string reason = "", bool trigger_comp = true) {
    return ActionAbort{std::move(reason), trigger_comp};
}
inline NextAction compensate_plan(std::string plan_id, std::string reason = "") {
    return ActionCompensate{std::move(plan_id), std::move(reason)};
}

// ── ContractResult::retry_later ─────────────────────────────────────
inline ContractResult ContractResult::retry_later(NextAction retry_action) {
    return {Status::Retry, "", {}, {std::move(retry_action)}, {}};
}

} // namespace smo::runtime
