#pragma once

#include "runtime_types.hpp"
#include "dispatcher.hpp"

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <memory>

namespace smo::runtime {

// ── Middleware Registry ──────────────────────────────────────────────
class MiddlewareRegistry {
public:
    void register_middleware(std::unique_ptr<ExecutionMiddleware> mw);
    void add_to_stage(MiddlewareStage stage, std::unique_ptr<ExecutionMiddleware> mw);
    std::vector<ExecutionMiddleware*> get_for_stage(MiddlewareStage stage);

private:
    std::multimap<int, std::unique_ptr<ExecutionMiddleware>> middlewares_;
    std::unordered_map<MiddlewareStage, std::vector<std::unique_ptr<ExecutionMiddleware>>> stage_middlewares_;
};

// ── Built-in Middlewares ─────────────────────────────────────────────

// AuthMiddleware: Verify requester identity
class AuthMiddleware : public ExecutionMiddleware {
public:
    int priority() const override { return -100; }
    std::string name() const override { return "AuthMiddleware"; }
    bool applies_to(const std::string&) const override { return true; }
    Result<void> on_stage(MiddlewareStage stage, MiddlewareContext& ctx) override;
};

// PolicyMiddleware: Check capabilities
class PolicyMiddleware : public ExecutionMiddleware {
public:
    int priority() const override { return -50; }
    std::string name() const override { return "PolicyMiddleware"; }
    bool applies_to(const std::string&) const override { return true; }
    Result<void> on_stage(MiddlewareStage stage, MiddlewareContext& ctx) override;
};

// TracingMiddleware: Emit tracing events
class TracingMiddleware : public ExecutionMiddleware {
public:
    int priority() const override { return 0; }
    std::string name() const override { return "TracingMiddleware"; }
    bool applies_to(const std::string&) const override { return true; }
    Result<void> on_stage(MiddlewareStage stage, MiddlewareContext& ctx) override;
private:
    static uint64_t now_ns();
    static std::string stage_to_string(MiddlewareStage s);
};

// TimeoutMiddleware: Enforce deadline
class TimeoutMiddleware : public ExecutionMiddleware {
public:
    int priority() const override { return 10; }
    std::string name() const override { return "TimeoutMiddleware"; }
    bool applies_to(const std::string&) const override { return true; }
    Result<void> on_stage(MiddlewareStage stage, MiddlewareContext& ctx) override;
};

// MetricsMiddleware: Collect latency, success rate
class MetricsMiddleware : public ExecutionMiddleware {
public:
    int priority() const override { return 50; }
    std::string name() const override { return "MetricsMiddleware"; }
    bool applies_to(const std::string&) const override { return true; }
    Result<void> on_stage(MiddlewareStage stage, MiddlewareContext& ctx) override;
};

// AuditMiddleware: Emit audit events to EventBus
class AuditMiddleware : public ExecutionMiddleware {
public:
    int priority() const override { return 100; }
    std::string name() const override { return "AuditMiddleware"; }
    bool applies_to(const std::string&) const override { return true; }
    Result<void> on_stage(MiddlewareStage stage, MiddlewareContext& ctx) override;
};

} // namespace smo::runtime