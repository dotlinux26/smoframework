#pragma once

#include "../errors/error.hpp"
#include "../types.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace smo {

// ---------------------------------------------------------------------------
// FsmState / FsmEvent — type-erased values for the generic FSM
// ---------------------------------------------------------------------------
using FsmState = int64_t;
using FsmEvent = int64_t;

// ---------------------------------------------------------------------------
// TransitionRule — defines a legal state transition
// ---------------------------------------------------------------------------
struct TransitionRule {
    FsmState from_state;
    FsmEvent event;
    FsmState to_state;
};

// ---------------------------------------------------------------------------
// StateTimeout — dwell limit and fallback for a state
// ---------------------------------------------------------------------------
struct StateTimeout {
    FsmState state;
    uint64_t max_dwell_ns;   // 0 = unlimited
    FsmState fallback_state;
};

// ---------------------------------------------------------------------------
// TransitionRecord — an audited transition (I-05, I-06)
// ---------------------------------------------------------------------------
struct TransitionRecord {
    FsmState from_state;
    FsmEvent event;
    FsmState to_state;
    uint64_t elapsed_ns;
    Bytes    state_hash;      // Blake3(serialized state after transition)
    int64_t  timestamp_ns;    // Monotonic timestamp
};

// ---------------------------------------------------------------------------
// FsmInstance — a generic, runtime-configurable finite state machine
//
// Design:
//  - Event-driven: all transitions via on_event()
//  - Every state has max_dwell_ns + fallback for timeout
//  - Every transition is recorded for audit/replay (I-05, I-06)
//  - Serialization for crash recovery (I-07)
//  - Pure: produces no side effects, caller handles actions
// ---------------------------------------------------------------------------
class FsmInstance {
public:
    FsmInstance() = default;

    // Configure the transition table. Must be called before use.
    void set_transitions(const TransitionRule* rules, size_t count);
    void set_transitions(std::vector<TransitionRule> rules);

    // Configure per-state timeouts and fallbacks.
    void set_timeouts(const StateTimeout* timeouts, size_t count);
    void set_timeouts(std::vector<StateTimeout> timeouts);

    // Reset to the initial state (clears history).
    void reset(FsmState initial_state);

    // ── Core API ───────────────────────────────────────────────────

    // Process an event. Returns error if transition is not allowed.
    // On success, records the transition and updates current state.
    Result<void> on_event(FsmEvent event);

    // Called when the current state's max dwell time has elapsed.
    // Transitions to the configured fallback state.
    Result<void> on_timeout();

    // ── Accessors ──────────────────────────────────────────────────

    FsmState current_state() const noexcept { return current_state_; }

    // Maximum dwell time for the current state (0 = unlimited).
    uint64_t max_dwell_ns() const noexcept;

    // Fallback state for the current state (0 = none).
    FsmState fallback_state() const noexcept;

    // The full audit history (I-05).
    const std::vector<TransitionRecord>& history() const noexcept { return history_; }

    // Number of transitions processed so far.
    size_t transition_count() const noexcept { return history_.size(); }

    // ── Serialization (I-07) ───────────────────────────────────────

    // Serialize current state + history to bytes.
    Result<Bytes> serialize() const;

    // Deserialize and reconfigure. The caller must provide the same
    // transition/timeout tables that were active at serialization time.
    static Result<FsmInstance> deserialize(
        BytesView data,
        const TransitionRule* rules, size_t rule_count,
        const StateTimeout* timeouts, size_t timeout_count);

private:
    FsmState current_state_ = 0;

    // Transition table: from_state → (event → to_state)
    std::unordered_map<FsmState, std::unordered_map<FsmEvent, FsmState>> transitions_;
    // Timeout config: state → StateTimeout
    std::unordered_map<FsmState, StateTimeout> timeouts_;

    std::vector<TransitionRecord> history_;

    // Monotonic clock helper
    static uint64_t monotonic_ns();
};

} // namespace smo
