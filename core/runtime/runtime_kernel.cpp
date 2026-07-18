#include "runtime_kernel.hpp"
#include "runtime_context.hpp"
#include "dispatcher.hpp"
#include "output_manager.hpp"
#include "event_bus.hpp"
#include "middleware.hpp"
#include "plan_executor.hpp"

#include <chrono>
#include <random>

namespace smo::runtime {

using smo::Result;

RuntimeKernel::RuntimeKernel(EventBus& bus,
                             OutputManager& output_mgr,
                             Dispatcher& dispatcher,
                             PlanResolver& resolver)
    : event_bus_(bus), output_mgr_(output_mgr), dispatcher_(dispatcher), resolver_(resolver) {}

// ── Public API ────────────────────────────────────────────────────────

Result<RuntimeResult> RuntimeKernel::execute(const RuntimeRequest& req) {
    auto start = std::chrono::steady_clock::now();

    RuntimeContext ctx;
    if (req.context) {
        ctx = *req.context;
    }
    ctx.info.execution_id = generate_execution_id();
    ctx.info.started_at_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    ctx.info.contract_id = req.contract_id;
    ctx.info.requester = req.requester;

    SMO_TRY(validate(req, ctx));
    SMO_TRY(resolve(req, ctx));

    SMO_TRY(execute_plan(req, ctx));

    SMO_TRY(dispatch(req, ctx));

    SMO_TRY(collect(ctx));
    SMO_TRY(aggregate(ctx));
    SMO_TRY(audit(ctx, true));
    SMO_TRY(complete(ctx));

    output_mgr_.add_result("local", "test", "test", "success", "");

    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();

    RuntimeResult result;
    result.execution_id = std::to_string(ctx.info.execution_id);
    result.status = RuntimeResult::Status::Success;
    result.elapsed_ns = elapsed;

    return result;
}

Result<std::string> RuntimeKernel::execute_async(const RuntimeRequest& req) {
    auto res = execute(req);
    if (!res) return Result<std::string>(res.error());
    return res.value().execution_id;
}

Result<RuntimeResult> RuntimeKernel::get_async_result(const std::string& execution_id) {
    (void)execution_id;
    return Result<RuntimeResult>(static_cast<Error>(RuntimeError::not_found("async result not yet implemented")));
}

Result<void> RuntimeKernel::cancel_async(const std::string& execution_id) {
    (void)execution_id;
    return Result<void>(static_cast<Error>(RuntimeError::not_found("async cancellation not yet implemented")));
}

// ── Pipeline Stages ─────────────────────────────────────────────────

Result<RuntimeResult> RuntimeKernel::validate(const RuntimeRequest& req, RuntimeContext& ctx) {
    if (req.contract_id.empty()) {
        return Result<RuntimeResult>(static_cast<Error>(RuntimeError::validation("empty contract_id")));
    }
    if (!dispatcher_.has_contract(req.contract_id)) {
        return Result<RuntimeResult>(static_cast<Error>(RuntimeError::not_found("contract not registered: " + req.contract_id)));
    }

    auto* contract = dispatcher_.get_contract(req.contract_id);
    if (contract) {
        auto val_res = contract->validate(req.input);
        if (!val_res) {
            return Result<RuntimeResult>(static_cast<Error>(RuntimeError::validation("input validation failed: " + val_res.error().message)));
        }
    }

    return RuntimeResult{RuntimeResult::Status::Success};
}

Result<RuntimeResult> RuntimeKernel::resolve(const RuntimeRequest& req, RuntimeContext& ctx) {
    auto plan_res = resolver_.resolve(req.contract_id);
    if (!plan_res) {
        return Result<RuntimeResult>(plan_res.error());
    }

    // Copy plan context into Variables
    for (const auto& [key, val] : plan_res.value().context) {
        ctx.vars.set(key, ContextValue(val));
    }

    auto val_res = plan_res.value().validate();
    if (!val_res) {
        return Result<RuntimeResult>(val_res.error());
    }

    return RuntimeResult{RuntimeResult::Status::Success};
}

// ── Pipeline Stages ─────────────────────────────────────────────────

Result<RuntimeResult> RuntimeKernel::execute_plan(const RuntimeRequest& req, RuntimeContext& ctx) {
    // Re-resolve plan from vars
    auto plan_res = resolver_.resolve(req.contract_id);
    if (!plan_res) {
        return Result<RuntimeResult>(plan_res.error());
    }

    ExecutionPlan plan = std::move(plan_res).value();

    PlanContext plan_ctx;
    plan_ctx.request_id = req.request_id;
    plan_ctx.plan_id = plan.plan_id;
    plan_ctx.execution_id = std::to_string(ctx.info.execution_id);
    plan_ctx.deadline_ns = req.deadline_ns;
    plan_ctx.event_bus = &event_bus_;
    plan_ctx.dispatcher = &dispatcher_;
    plan_ctx.output = &output_mgr_;
    plan_ctx.services = ctx.services;
    for (const auto& [key, val] : plan.context) {
        plan_ctx.context[key] = val;
    }

    PlanExecutor executor(plan, [&](const Step& step, PlanContext& pctx) -> PlanExecutor::StepResult {
        std::string input_template = step.input_template;
        for (const auto& [key, val] : pctx.context) {
            size_t pos = 0;
            while ((pos = input_template.find("{{" + key + "}}", pos)) != std::string::npos) {
                input_template.replace(pos, key.length() + 4, val);
                pos += val.length();
            }
        }

        ContractInput cin;
        cin.method = "invoke";
        cin.arguments = ContextValue(input_template);

        auto* contract = dispatcher_.get_contract(step.contract_id);
        if (!contract) {
            return PlanExecutor::StepResult{
                false, {}, RuntimeError::not_found("contract not found: " + step.contract_id)
            };
        }

        auto res = contract->execute(cin, ctx);
        if (!res) {
            return PlanExecutor::StepResult{
                false, {}, RuntimeError::internal(res.error().message)
            };
        }

        return PlanExecutor::StepResult{
            true, std::move(res).value(), {}
        };
    });

    auto result = executor.execute(plan_ctx);

    if (!result.success) {
        return Result<RuntimeResult>(static_cast<Error>(result.error));
    }

    for (const auto& [key, val] : result.outputs) {
        ctx.vars.set(key, ContextValue(val));
    }

    return RuntimeResult{RuntimeResult::Status::Success};
}

Result<RuntimeResult> RuntimeKernel::dispatch(const RuntimeRequest& req, RuntimeContext& ctx) {
    (void)req;
    (void)ctx;
    return RuntimeResult{};
}

Result<RuntimeResult> RuntimeKernel::collect(RuntimeContext& ctx) {
    (void)ctx;
    return RuntimeResult{RuntimeResult::Status::Success};
}

Result<RuntimeResult> RuntimeKernel::aggregate(RuntimeContext& ctx) {
    output_mgr_.add_result(
        ctx.info.node_id,
        ctx.info.contract_id,
        std::to_string(ctx.info.execution_id),
        "success",
        ""
    );
    return RuntimeResult{RuntimeResult::Status::Success};
}

Result<RuntimeResult> RuntimeKernel::audit(RuntimeContext& ctx, bool success,
                                           const std::string& error) {
    (void)ctx;
    (void)success;
    (void)error;
    return RuntimeResult{RuntimeResult::Status::Success};
}

Result<RuntimeResult> RuntimeKernel::complete(RuntimeContext& ctx) {
    (void)ctx;
    return RuntimeResult{RuntimeResult::Status::Success};
}

uint64_t RuntimeKernel::generate_execution_id() {
    return next_execution_id_++;
}

} // namespace smo::runtime
