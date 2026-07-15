#include <fsm/fsm.hpp>
#include <cstdio>
#include <cstring>

using namespace smo;

// ---------------------------------------------------------------------------
// Minimal test runner
// ---------------------------------------------------------------------------
static int failures = 0;

#define TEST(name)                                                      \
    do {                                                                \
        printf("  TEST %-50s ... ", name);                              \
        fflush(stdout);

#define END_TEST(result)                                                \
        if (result) {                                                   \
            printf("PASS\n");                                           \
        } else {                                                        \
            printf("FAIL\n");                                           \
            ++failures;                                                 \
        }                                                               \
    } while (false)

#define ASSERT(cond)                                                    \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("\n    ASSERTION FAILED at %s:%d: %s\n",             \
                   __FILE__, __LINE__, #cond);                          \
            return false;                                               \
        }                                                               \
    } while (false)

#define ASSERT_EQ(a, b)                                                 \
    do {                                                                \
        if ((a) != (b)) {                                               \
            printf("\n    ASSERTION FAILED at %s:%d: %s == %s\n"        \
                   "      LHS=%lld  RHS=%lld\n",                        \
                   __FILE__, __LINE__, #a, #b,                          \
                   static_cast<long long>(a),                           \
                   static_cast<long long>(b));                          \
            return false;                                               \
        }                                                               \
    } while (false)

// ==========================================================================
// FSM test: simple 3-state machine
// ==========================================================================

enum : FsmState { S_IDLE = 0, S_RUNNING = 1, S_STOPPED = 2 };
enum : FsmEvent { E_START = 10, E_STOP = 11, E_RESET = 12 };

static const TransitionRule kRules[] = {
    { S_IDLE,    E_START, S_RUNNING },
    { S_RUNNING, E_STOP,  S_STOPPED },
    { S_RUNNING, E_RESET, S_IDLE    },
    { S_STOPPED, E_RESET, S_IDLE    },
};

// ==========================================================================
// Tests
// ==========================================================================

static bool test_basic_transition() {
    FsmInstance fsm;
    fsm.set_transitions(kRules, sizeof(kRules) / sizeof(kRules[0]));
    fsm.reset(S_IDLE);

    ASSERT_EQ(fsm.current_state(), S_IDLE);
    ASSERT_EQ(fsm.transition_count(), 0U);

    auto r = fsm.on_event(E_START);
    ASSERT(r);
    ASSERT_EQ(fsm.current_state(), S_RUNNING);
    ASSERT_EQ(fsm.transition_count(), 1U);

    r = fsm.on_event(E_STOP);
    ASSERT(r);
    ASSERT_EQ(fsm.current_state(), S_STOPPED);
    ASSERT_EQ(fsm.transition_count(), 2U);

    r = fsm.on_event(E_RESET);
    ASSERT(r);
    ASSERT_EQ(fsm.current_state(), S_IDLE);
    ASSERT_EQ(fsm.transition_count(), 3U);

    return true;
}

static bool test_invalid_event() {
    FsmInstance fsm;
    fsm.set_transitions(kRules, sizeof(kRules) / sizeof(kRules[0]));
    fsm.reset(S_IDLE);

    // From S_IDLE, only E_START is valid; E_STOP should fail
    auto r = fsm.on_event(E_STOP);
    ASSERT(!r);
    ASSERT_EQ(r.error().code.code, 700);
    ASSERT_EQ(fsm.current_state(), S_IDLE);
    ASSERT_EQ(fsm.transition_count(), 0U);

    return true;
}

static bool test_no_transitions_configured() {
    FsmInstance fsm;
    fsm.reset(42);

    auto r = fsm.on_event(E_START);
    ASSERT(!r);
    ASSERT_EQ(r.error().code.code, 701);
    return true;
}

static bool test_history_audit_trail() {
    FsmInstance fsm;
    fsm.set_transitions(kRules, sizeof(kRules) / sizeof(kRules[0]));
    fsm.reset(S_IDLE);

    fsm.on_event(E_START);  // idle → running
    fsm.on_event(E_RESET);  // running → idle
    fsm.on_event(E_START);  // idle → running
    fsm.on_event(E_STOP);   // running → stopped

    const auto& hist = fsm.history();
    ASSERT_EQ(hist.size(), 4U);

    // First record: idle → running via E_START
    ASSERT_EQ(hist[0].from_state, S_IDLE);
    ASSERT_EQ(hist[0].event, E_START);
    ASSERT_EQ(hist[0].to_state, S_RUNNING);

    // Second record: running → idle via E_RESET
    ASSERT_EQ(hist[1].from_state, S_RUNNING);
    ASSERT_EQ(hist[1].event, E_RESET);
    ASSERT_EQ(hist[1].to_state, S_IDLE);

    // Third record: idle → running via E_START
    ASSERT_EQ(hist[2].from_state, S_IDLE);
    ASSERT_EQ(hist[2].event, E_START);
    ASSERT_EQ(hist[2].to_state, S_RUNNING);

    // Fourth record: running → stopped via E_STOP
    ASSERT_EQ(hist[3].from_state, S_RUNNING);
    ASSERT_EQ(hist[3].event, E_STOP);
    ASSERT_EQ(hist[3].to_state, S_STOPPED);

    // Each record should have a non-empty state_hash
    for (const auto& rec : hist) {
        ASSERT(!rec.state_hash.empty());
    }

    return true;
}

static bool test_timeout_config() {
    FsmInstance fsm;
    fsm.set_transitions(kRules, sizeof(kRules) / sizeof(kRules[0]));

    const StateTimeout timeouts[] = {
        { S_RUNNING, 5000000, S_STOPPED }  // 5ms → fallback to stopped
    };
    fsm.set_timeouts(timeouts, 1);
    fsm.reset(S_IDLE);

    // S_IDLE should have no timeout configured
    ASSERT_EQ(fsm.max_dwell_ns(), 0U);
    ASSERT_EQ(fsm.fallback_state(), 0);

    // Move to S_RUNNING
    fsm.on_event(E_START);
    ASSERT_EQ(fsm.max_dwell_ns(), 5000000U);
    ASSERT_EQ(fsm.fallback_state(), S_STOPPED);

    return true;
}

static bool test_timeout_no_config() {
    FsmInstance fsm;
    fsm.set_transitions(kRules, sizeof(kRules) / sizeof(kRules[0]));
    fsm.reset(S_IDLE);
    fsm.on_event(E_START);

    // No timeout configured for S_RUNNING (we haven't set timeouts)
    auto r = fsm.on_timeout();
    ASSERT(!r);
    ASSERT_EQ(r.error().code.code, 701);

    return true;
}

static bool test_timeout_transition() {
    FsmInstance fsm;
    fsm.set_transitions(kRules, sizeof(kRules) / sizeof(kRules[0]));

    // To handle timeout, we need a rule for the TIMEOUT event
    const TransitionRule rules_with_timeout[] = {
        { S_IDLE,    E_START, S_RUNNING },
        { S_RUNNING, E_STOP,  S_STOPPED },
        { S_RUNNING, static_cast<FsmEvent>(-1), S_STOPPED },  // TIMEOUT → stopped
        { S_STOPPED, E_RESET, S_IDLE    },
    };
    fsm.set_transitions(rules_with_timeout, 4);

    const StateTimeout timeouts[] = {
        { S_RUNNING, 5000000, S_STOPPED }
    };
    fsm.set_timeouts(timeouts, 1);
    fsm.reset(S_IDLE);
    fsm.on_event(E_START);
    ASSERT_EQ(fsm.current_state(), S_RUNNING);

    auto r = fsm.on_timeout();
    ASSERT(r);
    ASSERT_EQ(fsm.current_state(), S_STOPPED);
    ASSERT_EQ(fsm.transition_count(), 2U);

    return true;
}

static bool test_serialization_roundtrip() {
    FsmInstance fsm;
    fsm.set_transitions(kRules, sizeof(kRules) / sizeof(kRules[0]));
    fsm.reset(S_IDLE);

    fsm.on_event(E_START);
    fsm.on_event(E_RESET);
    fsm.on_event(E_START);
    fsm.on_event(E_STOP);

    auto ser = fsm.serialize();
    ASSERT(ser);

    auto restored = FsmInstance::deserialize(
        ser.value(),
        kRules, sizeof(kRules) / sizeof(kRules[0]),
        nullptr, 0);

    ASSERT(restored);
    ASSERT_EQ(restored.value().current_state(), fsm.current_state());
    ASSERT_EQ(restored.value().transition_count(), fsm.transition_count());

    const auto& orig_hist = fsm.history();
    const auto& rest_hist = restored.value().history();
    for (size_t i = 0; i < orig_hist.size(); ++i) {
        ASSERT_EQ(rest_hist[i].from_state, orig_hist[i].from_state);
        ASSERT_EQ(rest_hist[i].event, orig_hist[i].event);
        ASSERT_EQ(rest_hist[i].to_state, orig_hist[i].to_state);
        ASSERT_EQ(rest_hist[i].state_hash.size(), orig_hist[i].state_hash.size());
    }

    return true;
}

static bool test_serialization_truncated_data() {
    FsmInstance fsm;
    fsm.set_transitions(kRules, sizeof(kRules) / sizeof(kRules[0]));
    fsm.reset(S_IDLE);
    fsm.on_event(E_START);

    auto ser = fsm.serialize();
    ASSERT(ser);

    // Truncate the serialized data
    Bytes truncated(ser.value().begin(), ser.value().begin() + 5);
    auto restored = FsmInstance::deserialize(
        truncated,
        kRules, sizeof(kRules) / sizeof(kRules[0]),
        nullptr, 0);
    ASSERT(!restored);
    ASSERT_EQ(restored.error().code.code, 702);

    return true;
}

static bool test_reset_clears_history() {
    FsmInstance fsm;
    fsm.set_transitions(kRules, sizeof(kRules) / sizeof(kRules[0]));
    fsm.reset(S_IDLE);

    fsm.on_event(E_START);
    ASSERT_EQ(fsm.transition_count(), 1U);

    fsm.reset(S_IDLE);
    ASSERT_EQ(fsm.current_state(), S_IDLE);
    ASSERT_EQ(fsm.transition_count(), 0U);
    ASSERT(fsm.history().empty());

    return true;
}

static bool test_multiple_transitions_audit() {
    FsmInstance fsm;
    fsm.set_transitions(kRules, sizeof(kRules) / sizeof(kRules[0]));
    fsm.reset(S_IDLE);

    // Run through many transitions to stress-history
    for (int i = 0; i < 100; ++i) {
        fsm.on_event(E_START);
        fsm.on_event(E_STOP);
        fsm.on_event(E_RESET);
    }

    ASSERT_EQ(fsm.current_state(), S_IDLE);
    ASSERT_EQ(fsm.transition_count(), 300U);

    return true;
}

// ==========================================================================
// Main
// ==========================================================================

int main(int, char*[]) {
    printf("SMO FSM — Unit Tests\n");
    printf("====================\n\n");

    TEST("Basic transition")                    END_TEST(test_basic_transition());
    TEST("Invalid event fails")                 END_TEST(test_invalid_event());
    TEST("No transitions configured")           END_TEST(test_no_transitions_configured());
    TEST("History audit trail")                 END_TEST(test_history_audit_trail());
    TEST("Timeout configuration")               END_TEST(test_timeout_config());
    TEST("Timeout with no config")              END_TEST(test_timeout_no_config());
    TEST("Timeout transition")                  END_TEST(test_timeout_transition());
    TEST("Serialization roundtrip")             END_TEST(test_serialization_roundtrip());
    TEST("Deserialize truncated fails")         END_TEST(test_serialization_truncated_data());
    TEST("Reset clears history")                END_TEST(test_reset_clears_history());
    TEST("Multiple transitions audit")          END_TEST(test_multiple_transitions_audit());

    printf("\n");
    if (failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("%d TEST(S) FAILED\n", failures);
        return 1;
    }
}
