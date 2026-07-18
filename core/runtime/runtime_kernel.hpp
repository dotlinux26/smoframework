#pragma once

#include "runtime_types.hpp"
#include "contract_interface.hpp"
#include "event_bus.hpp"

#include <string>
#include <memory>
#include <vector>
#include <cstdint>

namespace smo::runtime {

// Forward declarations
class Dispatcher;
class OutputManager;
class PlanResolver;
class Scheduler;
class PolicyEngine;

// ── Runtime Request/Result ────────────────────────────────────────────
// (Defined in runtime_types.hpp)

// ── Runtime Kernel ────────────────────────────────────────────────────
class RuntimeKernel {
public:
    RuntimeKernel(EventBus& bus,
                  OutputManager& output_mgr,
                  Dispatcher& dispatcher,
                  PlanResolver& resolver);

    ~RuntimeKernel() = default;

    // Execute a request synchronously (full pipeline: validate → resolve → plan → middleware → dispatch)
    // Single entry point - no execute_direct() bypass
    Result<RuntimeResult> execute(const RuntimeRequest& req);

    // Execute asynchronously - returns execution_id
    Result<std::string> execute_async(const RuntimeRequest& req);

    // Get status of async execution
    Result<RuntimeResult> get_async_result(const std::string& execution_id);

    // Cancel async execution
    Result<void> cancel_async(const std::string& execution_id);

private:
    EventBus& event_bus_;
    OutputManager& output_mgr_;
    Dispatcher& dispatcher_;
    PlanResolver& resolver_;

    // Pipeline stages
    Result<RuntimeResult> validate(const RuntimeRequest& req, RuntimeContext& ctx);
    Result<RuntimeResult> resolve(const RuntimeRequest& req, RuntimeContext& ctx);
    Result<RuntimeResult> execute_plan(const RuntimeRequest& req, RuntimeContext& ctx);
    Result<RuntimeResult> dispatch(const RuntimeRequest& req, RuntimeContext& ctx);
    Result<RuntimeResult> collect(RuntimeContext& ctx);
    Result<RuntimeResult> aggregate(RuntimeContext& ctx);
    Result<RuntimeResult> audit(RuntimeContext& ctx, bool success, const std::string& error = "");
    Result<RuntimeResult> complete(RuntimeContext& ctx);

    uint64_t next_execution_id_ = 1;
    uint64_t generate_execution_id();
};

} // namespace smo::runtime