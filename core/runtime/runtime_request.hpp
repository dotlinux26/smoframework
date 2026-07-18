#pragma once

#include "runtime_types.hpp"
#include "core/types.hpp"
#include "core/errors/error.hpp"
#include "core/crypto/impl.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <chrono>
#include <cstdint>
#include <variant>
#include <atomic>

#include "core/crypto/impl.hpp"
#include "event_bus.hpp"

namespace smo::runtime {

// Forward declarations
class EventBus;
class OutputManager;
class PolicyEngine;
class Scheduler;
class Dispatcher;
class ContractInterface;

// ── ContractInterface ────────────────────────────────────────────────
// Defined in contract_interface.hpp

// ── NativeContract ──────────────────────────────────────────────────
// Defined in contract_interface.hpp

// ── Middleware Stages ────────────────────────────────────────────────
// Defined in runtime_types.hpp

// ── PlanResolver ─────────────────────────────────────────────────────
// Defined in plan_executor.hpp

// ── PlanExecutor ─────────────────────────────────────────────────────
// Defined in plan_executor.hpp

// ── ExecutionMiddleware ──────────────────────────────────────────────
// Defined in middleware.hpp

// ── MiddlewareStage ─────────────────────────────────────────────────
// Defined in runtime_types.hpp

// ── Dispatcher ───────────────────────────────────────────────────────
// Defined in dispatcher.hpp

} // namespace smo::runtime