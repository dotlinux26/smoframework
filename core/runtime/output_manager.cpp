#include "output_manager.hpp"

#include <chrono>
#include <algorithm>

namespace smo::runtime {

void OutputManager::add_result(const std::string& node_id,
                                const std::string& contract_id,
                                const std::string& execution_id,
                                const std::string& status,
                                const std::string& error_message) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Enforce max results cap
    if (results_.size() >= max_results_) {
        // Remove oldest 10% to make room
        size_t remove_count = max_results_ / 10;
        if (remove_count == 0) remove_count = 1;
        results_.erase(results_.begin(), results_.begin() + remove_count);
    }

    results_.push_back({
        .node_id = node_id,
        .contract_id = contract_id,
        .execution_id = execution_id,
        .status = status,
        .error_message = error_message,
        .timestamp_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count())
    });
}

OutputManager::AggregatedOutput OutputManager::get_summary() const {
    std::lock_guard<std::mutex> lock(mutex_);

    AggregatedOutput agg;

    for (const auto& r : results_) {
        agg.total++;

        if (r.status == "success") agg.success++;
        else if (r.status == "denied") agg.denied++;
        else if (r.status == "timeout") agg.timeout++;
        else if (r.status == "offline") agg.offline++;
        else agg.error++;

        agg.by_contract[r.contract_id]++;
    }

    // Extract top N error samples
    for (const auto& r : results_) {
        if (r.status == "error" && !r.error_message.empty() && agg.error_samples.size() < 10) {
            agg.error_samples.push_back(r.error_message.substr(0, 200));
        }
    }

    return agg;
}

OutputManager::DetailedOutput OutputManager::drill_down(const DrillFilter& filter) const {
    std::lock_guard<std::mutex> lock(mutex_);

    DetailedOutput detail;

    for (const auto& r : results_) {
        bool match = true;
        if (!filter.contract_id.empty() && r.contract_id != filter.contract_id) match = false;
        if (!filter.node_id.empty() && r.node_id != filter.node_id) match = false;
        if (!filter.execution_id.empty() && r.execution_id != filter.execution_id) match = false;
        if (!filter.status_filter.empty() && r.status != filter.status_filter) match = false;

        if (match) {
            detail.results.push_back({
                .node_id = r.node_id,
                .contract_id = r.contract_id,
                .execution_id = r.execution_id,
                .status = r.status,
                .error_message = r.error_message,
                .elapsed_ns = 0,  // not stored in basic version
                .timestamp_ns = r.timestamp_ns
            });
        }
    }

    return detail;
}

void OutputManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    results_.clear();
}

size_t OutputManager::result_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return results_.size();
}

} // namespace smo::runtime