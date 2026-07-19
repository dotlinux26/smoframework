#include "telemetry.hpp"

#include <sstream>
#include <iomanip>
#include <random>
#include <chrono>
#include <algorithm>
#include <cstdio>

namespace smo::runtime {

// ── Metrics ─────────────────────────────────────────────────────────────
void Telemetry::increment_counter(const std::string& name, const std::string& labels, int64_t delta) {
    std::string key = name + (labels.empty() ? "" : "{" + labels + "}");
    counters_[key].value += delta;
    counters_[key].labels = labels;
}

void Telemetry::set_gauge(const std::string& name, double value, const std::string& labels) {
    std::string key = name + (labels.empty() ? "" : "{" + labels + "}");
    gauges_[key].value = value;
    gauges_[key].labels = labels;
}

void Telemetry::record_histogram(const std::string& name, double value, const std::string& labels) {
    std::string key = name + (labels.empty() ? "" : "{" + labels + "}");
    auto& hist = histograms_[key];
    std::lock_guard<std::mutex> lock(hist.mutex);
    hist.observations.push_back(value);
    hist.labels = labels;
}

// ── Distributed Tracing ─────────────────────────────────────────────────
std::string Telemetry::generate_trace_id() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dist;

    uint64_t high = dist(gen);
    uint64_t low = dist(gen);

    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << high
        << std::setw(16) << std::setfill('0') << low;
    return oss.str();
}

void Telemetry::start_span(const std::string& operation_name, const std::string& trace_id,
                            const std::string& parent_span_id) {
    std::string span_id = generate_trace_id().substr(0, 16);

    Span span;
    span.span_id = span_id;
    span.trace_id = trace_id.empty() ? generate_trace_id() : trace_id;
    span.parent_span_id = parent_span_id;
    span.operation_name = operation_name;
    span.start_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    std::lock_guard<std::mutex> lock(spans_mutex_);
    spans_[span_id] = std::move(span);
}

void Telemetry::end_span(const std::string& span_id, const std::string& status) {
    int64_t end_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    std::lock_guard<std::mutex> lock(spans_mutex_);
    auto it = spans_.find(span_id);
    if (it != spans_.end()) {
        it->second.end_ns = end_ns;
        it->second.status = status;

        // Emit span event if EventBus available
        if (event_bus_) {
            Event ev;
            ev.type = EventType::ExecutionCompleted;
            ev.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            ev.source_id = "telemetry";
            ev.correlation_id = it->second.trace_id;
            ev.details = it->second.operation_name + " duration=" +
                std::to_string(it->second.end_ns - it->second.start_ns) + "ns status=" + status;
            event_bus_->publish(ev);
        }
    }
}

std::vector<std::string> Telemetry::active_spans() const {
    std::lock_guard<std::mutex> lock(spans_mutex_);
    std::vector<std::string> ids;
    for (const auto& [id, span] : spans_) {
        if (span.end_ns == 0) ids.push_back(span.span_id);
    }
    return ids;
}

// ── Health Endpoint ─────────────────────────────────────────────────────
void Telemetry::register_health_check(const std::string& name, HealthCheck check) {
    health_checks_.push_back({name, std::move(check)});
}

bool Telemetry::run_health_checks(std::unordered_map<std::string, bool>& results) const {
    bool all_healthy = true;
    for (const auto& entry : health_checks_) {
        std::string error_msg;
        bool healthy = entry.check(error_msg);
        results[entry.name] = healthy;
        if (!healthy) all_healthy = false;
    }
    return all_healthy;
}

std::string Telemetry::health_status() const {
    std::unordered_map<std::string, bool> results;
    bool healthy = run_health_checks(results);

    std::ostringstream oss;
    oss << "status=" << (healthy ? "healthy" : "unhealthy") << "\n";
    for (const auto& [name, healthy] : results) {
        oss << name << "=" << (healthy ? "ok" : "fail") << "\n";
    }
    return oss.str();
}

// ── Export ──────────────────────────────────────────────────────────────
std::string Telemetry::metrics_snapshot() const {
    std::ostringstream oss;

    // Counters
    for (const auto& [name, counter] : counters_) {
        oss << name << " " << counter.value.load() << "\n";
    }

    // Gauges
    for (const auto& [name, gauge] : gauges_) {
        oss << name << " " << gauge.value.load() << "\n";
    }

    // Histograms (summary)
    for (const auto& [name, hist] : histograms_) {
        std::lock_guard<std::mutex> lock(hist.mutex);
        if (!hist.observations.empty()) {
            double sum = 0;
            for (double v : hist.observations) sum += v;
            double avg = sum / hist.observations.size();
            double min = *std::min_element(hist.observations.begin(), hist.observations.end());
            double max = *std::max_element(hist.observations.begin(), hist.observations.end());

            oss << name << "_count " << hist.observations.size() << "\n";
            oss << name << "_sum " << sum << "\n";
            oss << name << "_avg " << avg << "\n";
            oss << name << "_min " << min << "\n";
            oss << name << "_max " << max << "\n";
        }
    }

    return oss.str();
}

std::string Telemetry::export_prometheus() const {
    return metrics_snapshot();
}

} // namespace smo::runtime