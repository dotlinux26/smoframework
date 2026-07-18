#pragma once

#include "core/types.hpp"
#include "core/errors/error.hpp"

#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <map>

namespace smo::runtime {

// ── Event Types ───────────────────────────────────────────────────────
enum class EventType : uint8_t {
    // Lifecycle
    RuntimeStarted,
    RuntimeStopped,
    ContractRegistered,
    ContractUnregistered,

    // Execution
    ExecutionStarted,
    ExecutionCompleted,
    ExecutionFailed,
    ExecutionTimeout,
    ExecutionCancelled,

    // Policy
    PolicyEvaluated,
    PolicyDenied,

    // Contract
    ContractInvoked,
    ContractReturned,
    ContractError,

    // Join / Bootstrap
    JoinStarted,
    JoinCompleted,
    JoinFailed,
    BootstrapRequested,
    BootstrapCompleted,
    BootstrapFailed,

    // Governance
    ProposalCreated,
    ProposalVoted,
    ProposalCommitted,
    ProposalRejected,
    ProposalConflicted,

    // Recovery
    RecoveryStarted,
    RecoveryCompleted,
    RecoveryFailed,
    RecoveryProposalCreated,   // RecoveryContract emits proposal for governance
    RecoveryApproved,          // Governance approved → triggers CRL revoke + session invalidate

    // File / Process / Vault
    FileOperation,
    ProcessExecution,
    VaultOperation,

    // Network
    NodeConnected,
    NodeDisconnected,
    GossipReceived,
    GossipSent,

    // Audit
    AuditLogged,
    SecurityAlert,
};

// ── Event ─────────────────────────────────────────────────────────────
struct Event {
    EventType type = EventType::ExecutionStarted;
    uint64_t timestamp_ns = 0;           // nanoseconds since epoch
    std::string source_id;               // node_id or "runtime"
    std::string correlation_id;          // for tracing related events
    std::string execution_id;            // if applicable
    std::string contract_id;             // if applicable
    std::string details;                 // JSON or human-readable
    std::map<std::string, std::string> tags; // key-value tags for filtering
};

// ── Subscription ──────────────────────────────────────────────────────
using EventHandler = std::function<void(const Event&)>;
using SubscriptionId = uint64_t;

struct Subscription {
    SubscriptionId id;
    EventHandler handler;
};

// ── EventBus ──────────────────────────────────────────────────────────
class EventBus {
public:
    EventBus() = default;
    ~EventBus() = default;

    // Publish event to all subscribers of this type (thread-safe)
    void publish(const Event& event);

    // Subscribe to a specific event type
    SubscriptionId subscribe(EventType type, EventHandler handler);

    // Unsubscribe by ID (removes from all event types)
    void unsubscribe(SubscriptionId id);

    // Unsubscribe from a specific event type
    void unsubscribe(EventType type, SubscriptionId id);

    // Get subscriber count for a type
    size_t subscriber_count(EventType type) const;

protected:
    mutable std::mutex mutex_;
    std::map<EventType, std::vector<Subscription>> subscribers_;
    std::atomic<SubscriptionId> next_sub_id_{1};
};

// ── Event Helper Functions ────────────────────────────────────────────
inline Event make_event(EventType type,
                        const std::string& source_id,
                        const std::string& correlation_id,
                        const std::string& execution_id,
                        const std::string& contract_id,
                        const std::string& details) {
    Event ev;
    ev.type = type;
    ev.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    ev.source_id = source_id;
    ev.correlation_id = correlation_id;
    ev.execution_id = execution_id;
    ev.contract_id = contract_id;
    ev.details = details;
    return ev;
}

inline Event make_event(EventType type,
                        const std::string& source_id,
                        const std::string& correlation_id,
                        const std::string& details) {
    return make_event(type, source_id, correlation_id, "", "", details);
}

// Convert string to EventType
inline EventType event_type_from_string(const std::string& name) {
    if (name == "RuntimeStarted") return EventType::RuntimeStarted;
    if (name == "RuntimeStopped") return EventType::RuntimeStopped;
    if (name == "ContractRegistered") return EventType::ContractRegistered;
    if (name == "ContractUnregistered") return EventType::ContractUnregistered;
    if (name == "ExecutionStarted") return EventType::ExecutionStarted;
    if (name == "ExecutionCompleted") return EventType::ExecutionCompleted;
    if (name == "ExecutionFailed") return EventType::ExecutionFailed;
    if (name == "ExecutionTimeout") return EventType::ExecutionTimeout;
    if (name == "ExecutionCancelled") return EventType::ExecutionCancelled;
    if (name == "PolicyEvaluated") return EventType::PolicyEvaluated;
    if (name == "PolicyDenied") return EventType::PolicyDenied;
    if (name == "ContractInvoked") return EventType::ContractInvoked;
    if (name == "ContractReturned") return EventType::ContractReturned;
    if (name == "ContractError") return EventType::ContractError;
    if (name == "JoinStarted") return EventType::JoinStarted;
    if (name == "JoinCompleted") return EventType::JoinCompleted;
    if (name == "JoinFailed") return EventType::JoinFailed;
    if (name == "BootstrapRequested") return EventType::BootstrapRequested;
    if (name == "BootstrapCompleted") return EventType::BootstrapCompleted;
    if (name == "BootstrapFailed") return EventType::BootstrapFailed;
    if (name == "ProposalCreated") return EventType::ProposalCreated;
    if (name == "ProposalVoted") return EventType::ProposalVoted;
    if (name == "ProposalCommitted") return EventType::ProposalCommitted;
    if (name == "ProposalRejected") return EventType::ProposalRejected;
    if (name == "ProposalConflicted") return EventType::ProposalConflicted;
    if (name == "RecoveryStarted") return EventType::RecoveryStarted;
    if (name == "RecoveryCompleted") return EventType::RecoveryCompleted;
    if (name == "RecoveryFailed") return EventType::RecoveryFailed;
    if (name == "RecoveryProposalCreated") return EventType::RecoveryProposalCreated;
    if (name == "RecoveryApproved") return EventType::RecoveryApproved;
    if (name == "FileOperation") return EventType::FileOperation;
    if (name == "ProcessExecution") return EventType::ProcessExecution;
    if (name == "VaultOperation") return EventType::VaultOperation;
    if (name == "NodeConnected") return EventType::NodeConnected;
    if (name == "NodeDisconnected") return EventType::NodeDisconnected;
    if (name == "GossipReceived") return EventType::GossipReceived;
    if (name == "GossipSent") return EventType::GossipSent;
    if (name == "AuditLogged") return EventType::AuditLogged;
    if (name == "SecurityAlert") return EventType::SecurityAlert;
    return EventType::ExecutionCompleted; // default
}

} // namespace smo::runtime