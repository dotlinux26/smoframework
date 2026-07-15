#include "fsm.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>

namespace smo {

// ---------------------------------------------------------------------------
// Monotonic clock
// ---------------------------------------------------------------------------

uint64_t FsmInstance::monotonic_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void FsmInstance::set_transitions(const TransitionRule* rules, size_t count) {
    transitions_.clear();
    for (size_t i = 0; i < count; ++i) {
        transitions_[rules[i].from_state][rules[i].event] = rules[i].to_state;
    }
}

void FsmInstance::set_transitions(std::vector<TransitionRule> rules) {
    set_transitions(rules.data(), rules.size());
}

void FsmInstance::set_timeouts(const StateTimeout* timeouts, size_t count) {
    timeouts_.clear();
    for (size_t i = 0; i < count; ++i) {
        timeouts_[timeouts[i].state] = timeouts[i];
    }
}

void FsmInstance::set_timeouts(std::vector<StateTimeout> timeouts) {
    set_timeouts(timeouts.data(), timeouts.size());
}

void FsmInstance::reset(FsmState initial_state) {
    current_state_ = initial_state;
    history_.clear();
}

// ---------------------------------------------------------------------------
// Core API
// ---------------------------------------------------------------------------

Result<void> FsmInstance::on_event(FsmEvent event) {
    auto state_it = transitions_.find(current_state_);
    if (state_it == transitions_.end()) {
        return SMO_ERR_RUNTIME(701, Error, NoRetry, RestartFSM,
                               "no transitions defined for current state");
    }

    auto event_it = state_it->second.find(event);
    if (event_it == state_it->second.end()) {
        return SMO_ERR_RUNTIME(700, Error, NoRetry, RestartFSM,
                               "invalid transition for current state");
    }

    FsmState next_state = event_it->second;
    uint64_t now = monotonic_ns();

    // Compute crude state_hash = XOR of (current, event, next, timestamp)
    Bytes hash(8, 0);
    for (size_t i = 0; i < 8; ++i) {
        hash[i] = static_cast<uint8_t>(
            (reinterpret_cast<const uint8_t*>(&current_state_)[i] ^
             reinterpret_cast<const uint8_t*>(&event)[i] ^
             reinterpret_cast<const uint8_t*>(&next_state)[i] ^
             reinterpret_cast<const uint8_t*>(&now)[i]));
    }

    uint64_t elapsed = history_.empty() ? 0 :
        now - history_.back().timestamp_ns;

    history_.push_back({
        current_state_,
        event,
        next_state,
        elapsed,
        std::move(hash),
        static_cast<int64_t>(now)
    });

    current_state_ = next_state;
    return {};
}

Result<void> FsmInstance::on_timeout() {
    auto it = timeouts_.find(current_state_);
    if (it == timeouts_.end()) {
        return SMO_ERR_RUNTIME(701, Error, NoRetry, RestartFSM,
                               "no timeout config for current state");
    }

    const auto& cfg = it->second;
    if (cfg.max_dwell_ns == 0) {
        return SMO_ERR_RUNTIME(701, Error, NoRetry, RestartFSM,
                               "current state has no timeout");
    }

    // Transition to fallback state with a special TIMEOUT event
    return on_event(static_cast<FsmEvent>(-1));
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

uint64_t FsmInstance::max_dwell_ns() const noexcept {
    auto it = timeouts_.find(current_state_);
    if (it == timeouts_.end()) return 0;
    return it->second.max_dwell_ns;
}

FsmState FsmInstance::fallback_state() const noexcept {
    auto it = timeouts_.find(current_state_);
    if (it == timeouts_.end()) return 0;
    return it->second.fallback_state;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

Result<Bytes> FsmInstance::serialize() const {
    Bytes out;

    auto append_u64 = [&](uint64_t v) {
        for (int i = 7; i >= 0; --i)
            out.push_back(static_cast<uint8_t>(v >> (i * 8)));
    };

    // Format: [current_state(8)] [history_count(4)] [records...]
    append_u64(static_cast<uint64_t>(current_state_));

    uint32_t count = static_cast<uint32_t>(history_.size());
    for (int i = 3; i >= 0; --i)
        out.push_back(static_cast<uint8_t>(count >> (i * 8)));

    for (const auto& rec : history_) {
        append_u64(static_cast<uint64_t>(rec.from_state));
        append_u64(static_cast<uint64_t>(rec.event));
        append_u64(static_cast<uint64_t>(rec.to_state));
        append_u64(rec.elapsed_ns);
        append_u64(static_cast<uint64_t>(rec.timestamp_ns));
        // state_hash length + data
        uint32_t hlen = static_cast<uint32_t>(rec.state_hash.size());
        for (int i = 3; i >= 0; --i)
            out.push_back(static_cast<uint8_t>(hlen >> (i * 8)));
        out.insert(out.end(), rec.state_hash.begin(), rec.state_hash.end());
    }

    return out;
}

Result<FsmInstance> FsmInstance::deserialize(
    BytesView data,
    const TransitionRule* rules, size_t rule_count,
    const StateTimeout* timeouts, size_t timeout_count)
{
    FsmInstance fsm;
    fsm.set_transitions(rules, rule_count);
    fsm.set_timeouts(timeouts, timeout_count);

    size_t offset = 0;

    auto read_u64 = [&]() -> uint64_t {
        uint64_t v = 0;
        for (int i = 0; i < 8 && offset < data.size(); ++i) {
            v = (v << 8) | data[offset++];
        }
        return v;
    };

    auto read_u32 = [&]() -> uint32_t {
        uint32_t v = 0;
        for (int i = 0; i < 4 && offset < data.size(); ++i) {
            v = (v << 8) | data[offset++];
        }
        return v;
    };

    if (offset + 12 > data.size()) {
        return SMO_ERR_RUNTIME(702, Critical, NoRetry, RebootNode,
                               "truncated FSM serialized state");
    }

    fsm.current_state_ = static_cast<FsmState>(read_u64());
    uint32_t hist_count = read_u32();

    for (uint32_t i = 0; i < hist_count; ++i) {
        if (offset + 40 > data.size()) {
            return SMO_ERR_RUNTIME(702, Critical, NoRetry, RebootNode,
                                   "truncated transition record");
        }
        TransitionRecord rec;
        rec.from_state   = static_cast<FsmState>(read_u64());
        rec.event        = static_cast<FsmEvent>(read_u64());
        rec.to_state     = static_cast<FsmState>(read_u64());
        rec.elapsed_ns   = read_u64();
        rec.timestamp_ns = static_cast<int64_t>(read_u64());

        uint32_t hlen = read_u32();
        if (offset + hlen > data.size()) {
            return SMO_ERR_RUNTIME(702, Critical, NoRetry, RebootNode,
                                   "truncated state hash");
        }
        rec.state_hash.assign(data.begin() + static_cast<ptrdiff_t>(offset),
                              data.begin() + static_cast<ptrdiff_t>(offset + hlen));
        offset += hlen;

        fsm.history_.push_back(std::move(rec));
    }

    return fsm;
}

} // namespace smo
