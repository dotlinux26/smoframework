// SPDX-License-Identifier: Apache-2.0
//
// Error Model — unit tests

#include <errors/error.hpp>
#include <cstdio>
#include <string>

using namespace smo;

// ---------------------------------------------------------------------------
// Minimal test runner (no framework dependency)
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
                   "      LHS=%d  RHS=%d\n",                            \
                   __FILE__, __LINE__, #a, #b,                          \
                   static_cast<int>(a), static_cast<int>(b));           \
            return false;                                               \
        }                                                               \
    } while (false)

// ==========================================================================
// Tests
// ==========================================================================

static bool test_error_code_default_constructor() {
    ErrorCode ec;
    ASSERT_EQ(ec.category, ErrorCategory::Internal);
    ASSERT_EQ(ec.code, 0);
    ASSERT_EQ(ec.severity, Severity::Error);
    ASSERT_EQ(ec.retry, RetryClass::NoRetry);
    ASSERT_EQ(ec.recovery, Recovery::None);
    return true;
}

static bool test_error_code_packed_size() {
    // Verify the bitfield packing doesn't exceed 4 bytes
    ASSERT_EQ(sizeof(ErrorCode), 4);
    return true;
}

static bool test_error_code_is_retryable() {
    ErrorCode safe(ErrorCategory::Transport, 1, Severity::Warn, RetryClass::RetrySafe, Recovery::Reconnect);
    ASSERT(safe.is_retryable());

    ErrorCode backoff(ErrorCategory::Discovery, 2, Severity::Warn, RetryClass::RetryBackoff, Recovery::None);
    ASSERT(backoff.is_retryable());

    ErrorCode no(ErrorCategory::Internal, 0, Severity::Error, RetryClass::NoRetry, Recovery::None);
    ASSERT(!no.is_retryable());

    ErrorCode never(ErrorCategory::Protocol, 3, Severity::Alert, RetryClass::RetryNever, Recovery::ManualIntervention);
    ASSERT(!never.is_retryable());
    return true;
}

static bool test_error_code_is_fatal() {
    ErrorCode info(ErrorCategory::Session, 0, Severity::Info, RetryClass::NoRetry, Recovery::None);
    ASSERT(!info.is_fatal());

    ErrorCode warn(ErrorCategory::Storage, 0, Severity::Warn, RetryClass::RetrySafe, Recovery::None);
    ASSERT(!warn.is_fatal());

    ErrorCode critical(ErrorCategory::Identity, 0, Severity::Critical, RetryClass::NoRetry, Recovery::RebootNode);
    ASSERT(critical.is_fatal());

    ErrorCode alert(ErrorCategory::Certificate, 0, Severity::Alert, RetryClass::RetryNever, Recovery::ManualIntervention);
    ASSERT(alert.is_fatal());
    return true;
}

static bool test_error_constructor() {
    Error err(ErrorCode(ErrorCategory::Crypto, 42, Severity::Error, RetryClass::NoRetry, Recovery::RetryOperation),
              "key generation failed", __FILE__, __LINE__);
    ASSERT_EQ(err.code.category, ErrorCategory::Crypto);
    ASSERT_EQ(err.code.code, 42);
    ASSERT(err.message.find("key generation failed") != std::string::npos);
    ASSERT(err.source_file != nullptr);
    ASSERT(err.source_line > 0);
    ASSERT(err.timestamp_ns > 0);
    return true;
}

static bool test_error_to_string() {
    Error err(ErrorCode(ErrorCategory::Storage, 7, Severity::Critical, RetryClass::RetrySafe, Recovery::None),
              "disk full", __FILE__, __LINE__);
    std::string s = err.to_string();
    ASSERT(!s.empty());
    ASSERT(s.find("Storage") != std::string::npos || s.find("7") != std::string::npos);
    ASSERT(s.find("disk full") != std::string::npos);
    return true;
}

static bool test_smo_err_macro() {
    Error err = SMO_ERR(Crypto, 1, Error, NoRetry, RetryOperation, "keygen failed");
    ASSERT_EQ(err.code.category, ErrorCategory::Crypto);
    ASSERT_EQ(err.code.code, 1);
    ASSERT(err.message.find("keygen failed") != std::string::npos);
    ASSERT(err.source_file != nullptr);
    return true;
}

static bool test_result_ok() {
    Result<int> r(42);
    ASSERT(r);
    ASSERT(r.value() == 42);
    return true;
}

static bool test_result_error() {
    Result<int> r = SMO_ERR(Protocol, 99, Error, NoRetry, None, "bad packet");
    ASSERT(!r);
    ASSERT(r.error().code.category == ErrorCategory::Protocol);
    ASSERT(r.error().code.code == 99);
    return true;
}

static bool test_result_void_ok() {
    Result<void> r;
    ASSERT(r);
    ASSERT(!r.has_error());
    return true;
}

static bool test_result_void_error() {
    Result<void> r = SMO_ERR(Session, 5, Error, RetrySafe, Reconnect, "session expired");
    ASSERT(!r);
    ASSERT(r.has_error());
    ASSERT(r.error().code.category == ErrorCategory::Session);
    return true;
}

static bool test_result_move_semantics() {
    Result<std::string> r1(std::string("hello"));
    ASSERT(r1);
    ASSERT(r1.value() == "hello");

    Result<std::string> r2(std::move(r1));
    ASSERT(r2);
    ASSERT(r2.value() == "hello");
    return true;
}

static bool test_smo_try_void() {
    // SMO_TRY on a void Result should not return if ok
    auto fn = []() -> Result<void> {
        SMO_TRY(Result<void>());
        return {};
    };
    Result<void> r = fn();
    ASSERT(r);
    return true;
}

static bool test_smo_try_propagates_error() {
    // SMO_TRY on a void Result should propagate error
    auto fn = []() -> Result<void> {
        SMO_TRY(Result<void>(SMO_ERR(Internal, 1, Error, NoRetry, None, "nested fail")));
        return {};
    };
    Result<void> r = fn();
    ASSERT(!r);
    ASSERT(r.error().code.code == 1);
    return true;
}

static bool test_smo_try_val() {
    // SMO_TRY_VAL on a value Result
    auto fn = []() -> Result<int> {
        SMO_TRY_VAL(auto val, Result<int>(42));
        return val + 1;
    };
    Result<int> r = fn();
    ASSERT(r);
    ASSERT(r.value() == 43);
    return true;
}

static bool test_smo_try_val_propagates() {
    auto fn = []() -> Result<int> {
        SMO_TRY_VAL(auto val, Result<int>(SMO_ERR(Storage, 2, Error, NoRetry, None, "no space")));
        (void)val;
        return 0;
    };
    Result<int> r = fn();
    ASSERT(!r);
    ASSERT(r.error().code.category == ErrorCategory::Storage);
    return true;
}

// ==========================================================================
// Main
// ==========================================================================

int main(int, char*[]) {
    printf("SMO Error Model — Unit Tests\n");
    printf("============================\n\n");

    TEST("ErrorCode default constructor")        END_TEST(test_error_code_default_constructor());
    TEST("ErrorCode packed size == 4")           END_TEST(test_error_code_packed_size());
    TEST("ErrorCode is_retryable")               END_TEST(test_error_code_is_retryable());
    TEST("ErrorCode is_fatal")                   END_TEST(test_error_code_is_fatal());
    TEST("Error constructor")                    END_TEST(test_error_constructor());
    TEST("Error to_string")                      END_TEST(test_error_to_string());
    TEST("SMO_ERR macro")                        END_TEST(test_smo_err_macro());
    TEST("Result<int> ok")                       END_TEST(test_result_ok());
    TEST("Result<int> error")                    END_TEST(test_result_error());
    TEST("Result<void> ok")                      END_TEST(test_result_void_ok());
    TEST("Result<void> error")                   END_TEST(test_result_void_error());
    TEST("Result move semantics")                END_TEST(test_result_move_semantics());
    TEST("SMO_TRY void ok")                      END_TEST(test_smo_try_void());
    TEST("SMO_TRY propagates error")             END_TEST(test_smo_try_propagates_error());
    TEST("SMO_TRY_VAL ok")                       END_TEST(test_smo_try_val());
    TEST("SMO_TRY_VAL propagates error")         END_TEST(test_smo_try_val_propagates());

    printf("\n");
    if (failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("%d TEST(S) FAILED\n", failures);
        return 1;
    }
}
