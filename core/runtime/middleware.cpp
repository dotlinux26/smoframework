#include "middleware.hpp"
#include "runtime_context.hpp"

#include <chrono>
#include <algorithm>

namespace smo::runtime {

// ── Built-in Middlewares ──────────────────────────────────────────────

// AuthMiddleware
Result<void> AuthMiddleware::on_stage(MiddlewareStage stage, MiddlewareContext& ctx) {
    if (stage != MiddlewareStage::BeforeResolve) return {};

    auto& req = *ctx.request;
    if (req.requester.empty()) {
        return Result<void>(static_cast<Error>(RuntimeError::unauthorized("empty requester")));
    }
    return {};
}

// PolicyMiddleware
Result<void> PolicyMiddleware::on_stage(MiddlewareStage stage, MiddlewareContext& ctx) {
    if (stage != MiddlewareStage::BeforeDispatch) return {};
    
    // TODO: Integrate with PolicyEngine
    // Check if requester has required capabilities for the contract/step
    return {};
}

// TracingMiddleware
Result<void> TracingMiddleware::on_stage(MiddlewareStage stage, MiddlewareContext& ctx) {
    if (!ctx.plan_context->event_bus) return {};
    
    Event e;
    e.type = EventType::ExecutionStarted;
    e.timestamp_ns = now_ns();
    e.source_id = "tracing";
    e.correlation_id = ctx.request->request_id;
    e.details = "stage: " + stage_to_string(stage);
    ctx.plan_context->event_bus->publish(e);
    
    return {};
}

uint64_t TracingMiddleware::now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string TracingMiddleware::stage_to_string(MiddlewareStage s) {
    switch(s) {
        case MiddlewareStage::BeforeResolve: return "before_resolve";
        case MiddlewareStage::BeforeDispatch: return "before_dispatch";
        case MiddlewareStage::AfterDispatch: return "after_dispatch";
        case MiddlewareStage::AfterCommit: return "after_commit";
    }
    return "unknown";
}

// TimeoutMiddleware
Result<void> TimeoutMiddleware::on_stage(MiddlewareStage stage, MiddlewareContext& ctx) {
    if (stage != MiddlewareStage::AfterDispatch) return {};
    
    if (ctx.plan_context->info.deadline_ns > 0) {
        auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (now > ctx.plan_context->info.deadline_ns) {
            return Result<void>(static_cast<Error>(RuntimeError::timeout("execution deadline exceeded")));
        }
    }
    return {};
}

// MetricsMiddleware
Result<void> MetricsMiddleware::on_stage(MiddlewareStage stage, MiddlewareContext& ctx) {
    // TODO: Record metrics to metrics collector
    return {};
}

// AuditMiddleware
Result<void> AuditMiddleware::on_stage(MiddlewareStage stage, MiddlewareContext& ctx) {
    if (stage != MiddlewareStage::AfterCommit) return {};
    if (!ctx.plan_context->event_bus) return {};
    
    Event e;
    e.type = EventType::AuditLogged;
    e.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    e.source_id = "audit";
    e.correlation_id = ctx.request->request_id;
    e.details = "contract=" + ctx.request->contract_id + " status=success";
    ctx.plan_context->event_bus->publish(e);
    
    return {};
}

} // namespace smo::runtime