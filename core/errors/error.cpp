// SPDX-License-Identifier: Apache-2.0
//
// SMO — Secure Mesh Operation
// Core Error Model — implementation

#include "error.hpp"
#include "errors.h"

#include <ctime>    // clock_gettime
#include <fmt/core.h>

namespace smo {

// ---------------------------------------------------------------------------
// Monotonic clock for error timestamps
// ---------------------------------------------------------------------------
uint64_t Error::monotonic_ns() noexcept {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL
         + static_cast<uint64_t>(ts.tv_nsec);
}

// ---------------------------------------------------------------------------
// Human-readable error string
// ---------------------------------------------------------------------------
std::string Error::to_string() const {
    return fmt::format("[{}:{}] {} (cat={}, code={}, sev={})",
                       source_file ? source_file : "?",
                       source_line,
                       message,
                       static_cast<int>(code.category),
                       code.code,
                       static_cast<int>(code.severity));
}

// ---------------------------------------------------------------------------
// std::error_category interface
// ---------------------------------------------------------------------------
const char* ErrorCategoryImpl::name() const noexcept {
    return "smo";
}

std::string ErrorCategoryImpl::message(int ev) const {
    switch (static_cast<ErrorCategory>(ev)) {
    case ErrorCategory::Crypto:      return "Crypto";
    case ErrorCategory::Identity:    return "Identity";
    case ErrorCategory::Certificate: return "Certificate";
    case ErrorCategory::Transport:   return "Transport";
    case ErrorCategory::Discovery:   return "Discovery";
    case ErrorCategory::Session:     return "Session";
    case ErrorCategory::Protocol:    return "Protocol";
    case ErrorCategory::Runtime:     return "Runtime";
    case ErrorCategory::Governance:  return "Governance";
    case ErrorCategory::Storage:     return "Storage";
    case ErrorCategory::Internal:    return "Internal";
    case ErrorCategory::Compiler:    return "Compiler";
    case ErrorCategory::Trust:       return "Trust";
    default:                         return "Unknown";
    }
}

// ---------------------------------------------------------------------------
// Legacy Errc → std::error_code bridge
// ---------------------------------------------------------------------------
namespace {

struct ErrcCategory : std::error_category {
    const char* name() const noexcept override { return "smo_errc"; }
    std::string message(int ev) const override {
        switch (static_cast<Errc>(ev)) {
        case Errc::OK:                    return "OK";
        case Errc::UNKNOWN:               return "Unknown";
        case Errc::NOT_IMPLEMENTED:       return "Not implemented";
        case Errc::INVALID_CONTRACT:      return "Invalid contract";
        case Errc::INVALID_SIGNATURE:     return "Invalid signature";
        case Errc::INVALID_SESSION:       return "Invalid session";
        case Errc::INVALID_TIMESTAMP:     return "Invalid timestamp";
        case Errc::POLICY_DENIED:         return "Policy denied";
        case Errc::CAPABILITY_INSUFFICIENT: return "Capability insufficient";
        case Errc::CAPABILITY_REVOKED:    return "Capability revoked";
        case Errc::TRUST_INSUFFICIENT:    return "Trust insufficient";
        case Errc::WITNESS_UNAVAILABLE:   return "Witness unavailable";
        case Errc::WITNESS_REJECTED:      return "Witness rejected";
        case Errc::EXECUTION_FAILED:      return "Execution failed";
        case Errc::EXECUTION_TIMEOUT:     return "Execution timeout";
        case Errc::OPCODE_NOT_FOUND:      return "Opcode not found";
        case Errc::OPCODE_NOT_IDEMPOTENT: return "Opcode not idempotent";
        case Errc::CONNECTION_FAILED:     return "Connection failed";
        case Errc::SESSION_EXPIRED:       return "Session expired";
        case Errc::REPLAY_DETECTED:       return "Replay detected";
        case Errc::STORE_UNAVAILABLE:     return "Store unavailable";
        case Errc::STORE_CORRUPTION:      return "Store corruption";
        default:                          return "Unknown error";
        }
    }
};

} // anonymous namespace

std::error_code make_error_code(Errc e) noexcept {
    static const ErrcCategory cat;
    return std::error_code(static_cast<int>(e), cat);
}

} // namespace smo
