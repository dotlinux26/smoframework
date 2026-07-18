#pragma once

#include "runtime_types.hpp"
#include "runtime_context.hpp"
#include "dispatcher.hpp"
#include "services/transport_service.hpp"

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <memory>
#include <queue>

namespace smo::runtime {

// ── PlanExecutor: Executes DAG with parallel support (RFC 0038 + 0039) ─
class PlanExecutor {
public:
    struct StepResult {
        bool success = false;
        ContractResult output;
        RuntimeError error;
    };

    using StepExecutor = std::function<StepResult(const Step&, PlanContext&)>;

    PlanExecutor(const ExecutionPlan& plan, StepExecutor executor)
        : plan_(plan), executor_(std::move(executor)) {}

    struct Result {
        bool success = false;
        std::unordered_map<std::string, std::string> outputs;
        RuntimeError error;
        std::vector<NextAction> final_actions;
    };

    Result execute(PlanContext& ctx) {
        ctx.plan_state = PlanState::Executing;
        ctx.step_status.clear();
        for (const auto& step : plan_.steps) {
            ctx.step_status[step.step_id] = StepStatus{};
            ctx.step_status[step.step_id].state = StepState::Pending;
        }

        std::unordered_map<std::string, std::vector<std::string>> adj;
        std::unordered_map<std::string, int> indegree;
        for (const auto& step : plan_.steps) {
            indegree[step.step_id] = 0;
            for (const auto& dep : step.depends_on) {
                adj[dep].push_back(step.step_id);
                indegree[step.step_id]++;
            }
        }

        std::queue<std::string> ready;
        for (const auto& [id, deg] : indegree) {
            if (deg == 0) {
                ctx.step_status[id].state = StepState::Ready;
                ready.push(id);
            }
        }

        size_t completed = 0;
        while (!ready.empty() && !ctx.aborted) {
            std::vector<std::string> batch;
            while (!ready.empty() && (plan_.mode == ExecutionMode::Parallel || batch.empty())) {
                batch.push_back(ready.front());
                ready.pop();
            }

            for (const auto& step_id : batch) {
                if (ctx.aborted) break;

                const Step& step = get_step(step_id);
                ctx.current_step = step_id;
                ctx.step_status[step_id].state = StepState::Running;
                ctx.step_status[step_id].started_at_ns = now_ns();
                ctx.step_status[step_id].attempt++;

                StepResult res = executor_(get_step(step_id), ctx);

                ctx.step_status[step_id].completed_at_ns = now_ns();
                if (!res.success) {
                    if (ctx.step_status[step_id].is_retryable() && step.max_retries > 0) {
                        ctx.step_status[step_id].state = StepState::WaitingRetry;
                        ctx.step_status[step_id].last_retry_at_ns = now_ns();
                        // Re-enqueue after delay
                        ready.push(step_id);
                    } else {
                        ctx.step_status[step_id].state = StepState::Failed;
                        ctx.step_status[step_id].error_message = res.error.what();

                        if (!step.optional) {
                            // Check compensation policy
                            if (plan_.rollback_policy != "BestEffort") {
                                trigger_compensation(ctx);
                                ctx.plan_state = PlanState::Compensated;
                                return Result{false, {}, RuntimeError::compensation("plan compensated"), {}};
                            }
                            ctx.aborted = true;
                            ctx.abort_reason = "step failed: " + step_id;
                            ctx.plan_state = PlanState::Failed;
                            return Result{false, {}, res.error, {}};
                        } else {
                            ctx.step_status[step_id].state = StepState::Skipped;
                        }
                    }
                } else {
                    ctx.step_status[step_id].state = StepState::Completed;
                    ctx.step_status[step_id].output = res.output.data;
                    ctx.step_status[step_id].contract_result = std::move(res.output);
                    StepStatus& ss = ctx.step_status[step_id];

                    if (!step.output_binding.empty()) {
                        ctx.context[step.output_binding] = ss.output;
                    }

                    // Process NextActions via std::visit (RFC 0039)
                    for (const auto& action : ss.contract_result.next_actions) {
                        process_action(action, step_id, ctx);
                    }
                }
                completed++;
            }

            for (const auto& step_id : batch) {
                for (const auto& next : adj[step_id]) {
                    if (--indegree[next] == 0) {
                        ctx.step_status[next].state = StepState::Ready;
                        ready.push(next);
                    }
                }
            }
        }

        if (ctx.aborted) {
            ctx.plan_state = PlanState::Cancelled;
            return Result{false, {}, RuntimeError::internal("plan aborted: " + ctx.abort_reason), {}};
        }

        ctx.plan_state = PlanState::Completed;
        return Result{true, ctx.context, {}, {}};
    }

private:
    const ExecutionPlan& plan_;
    StepExecutor executor_;

    const Step& get_step(const std::string& id) const {
        for (const auto& s : plan_.steps) if (s.step_id == id) return s;
        static Step dummy; return dummy;
    }

    static uint64_t now_ns() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    void trigger_compensation(PlanContext& ctx) {
        for (auto& [id, status] : ctx.step_status) {
            if (status.state == StepState::Completed) {
                // Find the compensation step for this step
                for (const auto& step : plan_.steps) {
                    if (step.step_id == id && !step.compensation_id.empty()) {
                        status.state = StepState::Compensating;
                        // Execute compensation
                        auto* comp_contract = ctx.dispatcher
                            ? ctx.dispatcher->get_contract(step.compensation_id)
                            : nullptr;
                        if (comp_contract) {
                            ContractInput cin = ContractInput::method_only("compensate");
                            RuntimeContext dummy_ctx;
                            auto comp_res = comp_contract->execute(cin, dummy_ctx);
                            if (comp_res) {
                                status.state = StepState::Compensated;
                            } else {
                                status.state = StepState::CompFault;
                            }
                        } else {
                            status.state = StepState::Compensated;
                        }
                        break;
                    }
                }
            }
        }
    }

    void process_action(const NextAction& action, const std::string& step_id, PlanContext& ctx) {
        std::visit(overloaded{
            [&](const ActionDispatchContract& a) {
                // Queue next contract execution
                // In a full implementation, this would schedule new steps
                if (ctx.dispatcher) {
                    RuntimeContext dummy_ctx;
                    ctx.dispatcher->execute(a.contract_id, a.input, dummy_ctx);
                }
            },
            [&](const ActionDispatchMessage& a) {
                // Send raw message via transport
                if (ctx.services && ctx.services->transport) {
                    ctx.services->transport->send_message(a.target_node, a.opcode, a.data);
                }
            },
            [&](const ActionScheduleRetry& a) {
                // Mark step for scheduled retry
                auto& ss = ctx.step_status[step_id];
                ss.state = StepState::WaitingRetry;
                ss.max_retries = static_cast<int>(a.max_retries);
                ss.retry_delay_ns = a.delay_ns;
            },
            [&](const ActionEmitEvent& a) {
                if (ctx.event_bus) {
                    Event e;
                    e.type = EventType::ExecutionCompleted;
                    e.timestamp_ns = now_ns();
                    e.source_id = ctx.execution_id;
                    e.details = a.payload;
                    ctx.event_bus->publish(e);
                }
            },
            [&](const ActionStoreContext& a) {
                ctx.context[a.key] = a.value.to_string();
            },
            [&](const ActionSpawnPlan& a) {
                // Resolve and execute sub-plan (future)
                (void)a;
            },
            [&](const ActionAbort& a) {
                ctx.aborted = true;
                ctx.abort_reason = a.reason;
                if (a.trigger_compensation) {
                    trigger_compensation(ctx);
                }
            },
            [&](const ActionCompensate& a) {
                trigger_compensation(ctx);
            },
            [&](const ActionNotify& a) {
                // Send notification (future)
                (void)a;
            }
        }, action);
    }
};

} // namespace smo::runtime
