#pragma once

#include "runtime_context.hpp"
#include "event_bus.hpp"

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <cstdint>

namespace smo::runtime {

// ── OutputManager ─────────────────────────────────────────────────────
//
// Aggregates execution results from multiple nodes/contracts.
// Provides summary view first, then drill-down on demand.
//
// Design: NOT a formatter. It collects, summarizes, and enables drill-down.
// UI/CLI consumes the summary and calls drill_down() for details.
//
class OutputManager {
public:
    struct NodeResult {
        std::string node_id;
        std::string contract_id;
        std::string execution_id;
        std::string status;           // success, denied, timeout, offline, error
        std::string error_message;
        int64_t elapsed_ns = 0;
        uint64_t timestamp_ns = 0;
    };

    struct AggregatedOutput {
        uint64_t total = 0;
        uint64_t success = 0;
        uint64_t denied = 0;
        uint64_t timeout = 0;
        uint64_t offline = 0;
        uint64_t error = 0;
        std::unordered_map<std::string, uint64_t> by_contract;
        std::unordered_map<std::string, uint64_t> by_node;
        std::vector<std::string> error_samples;  // first N error messages

        // Human-readable summary
        std::string summary() const {
            return "Executed: " + std::to_string(total) +
                   " | Success: " + std::to_string(success) +
                   " | Denied: " + std::to_string(denied) +
                   " | Timeout: " + std::to_string(timeout) +
                   " | Offline: " + std::to_string(offline) +
                   " | Error: " + std::to_string(error);
        }
    };

    struct DetailedOutput {
        std::vector<NodeResult> results;
        std::vector<std::string> traces;      // execution traces if available
        std::vector<std::string> logs;        // contract logs
        std::vector<std::string> metrics;     // performance metrics
        std::vector<std::string> timeline;    // event timeline
    };

    struct DrillFilter {
        std::string contract_id;
        std::string node_id;
        std::string execution_id;
        std::string status_filter;  // success, denied, timeout, offline, error
    };

    OutputManager() = default;
    ~OutputManager() = default;

    // Add a single execution result
    void add_result(const std::string& node_id,
                    const std::string& contract_id,
                    const std::string& execution_id,
                    const std::string& status,
                    const std::string& error_message = "");

    // Get aggregated summary
    AggregatedOutput get_summary() const;

    // Drill down into specific executions
    DetailedOutput drill_down(const DrillFilter& filter) const;

    // Clear all stored results
    void clear();

    // Get count of stored results
    size_t result_count() const;

    // Set maximum stored results (for memory management)
    void set_max_results(size_t max) { max_results_ = max; }

private:
    struct StoredResult {
        std::string node_id;
        std::string contract_id;
        std::string execution_id;
        std::string status;
        std::string error_message;
        uint64_t timestamp_ns = 0;
    };

    mutable std::mutex mutex_;
    std::vector<StoredResult> results_;
    size_t max_results_ = 10000;  // default cap
};

} // namespace smo::runtime