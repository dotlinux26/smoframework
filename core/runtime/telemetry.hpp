#pragma once

#include "core/errors/error.hpp"
#include "core/types.hpp"
#include "core/runtime/event_bus.hpp"

#include <string>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <functional>
#include <vector>
#include <string_view>

namespace smo::runtime {

// ── Telemetry: metrics, tracing, health endpoint ────────────────────────
class Telemetry {
public:
    Telemetry() = default;
    ~Telemetry() = default;

    // ── Metrics ─────────────────────────────────────────────────────────
    // Counter: increment by 1 (or delta)
    void increment_counter(const std::string& name, const std::string& labels = "", int64_t delta = 1);

    // Gauge: set value
    void set_gauge(const std::string& name, double value, const std::string& labels = "");

    // Histogram: record observation
    void record_histogram(const std::string& name, double value, const std::string& labels = "");

    // Get metric snapshot (Prometheus text format)
    std::string metrics_snapshot() const;

    // ── Distributed Tracing ─────────────────────────────────────────────
    // Generate new trace_id (128-bit hex)
    static std::string generate_trace_id();

    // Start a new span
    void start_span(const std::string& operation_name, const std::string& trace_id = "",
                    const std::string& parent_span_id = "");

    // End span and record
    void end_span(const std::string& span_id, const std::string& status = "ok");

    // Get active spans
    std::vector<std::string> active_spans() const;

    // ── Health Endpoint ─────────────────────────────────────────────────
    // Register health check
    using HealthCheck = std::function<bool(std::string& error_msg)>;
    void register_health_check(const std::string& name, HealthCheck check);

    // Run all health checks
    bool run_health_checks(std::unordered_map<std::string, bool>& results) const;

    // Health status for endpoint
    std::string health_status() const;

    // ── EventBus Integration ────────────────────────────────────────────
    // Publish telemetry events
    void set_event_bus(EventBus* bus) { event_bus_ = bus; }

    // ── Export ──────────────────────────────────────────────────────────
    // Export metrics in Prometheus text format
    std::string export_prometheus() const;

private:
    // ── Internal Types ──────────────────────────────────────────────────
    struct Counter {
        std::atomic<int64_t> value{0};
        std::string labels;
    };

    struct Gauge {
        std::atomic<double> value{0.0};
        std::string labels;
    };

    struct Histogram {
        mutable std::mutex mutex;
        std::vector<double> observations;
        std::string labels;
    };

    struct Span {
        std::string span_id;
        std::string trace_id;
        std::string parent_span_id;
        std::string operation_name;
        int64_t start_ns = 0;
        int64_t end_ns = 0;
        std::string status;
    };

    struct HealthCheckEntry {
        std::string name;
        std::function<bool(std::string&)> check;
    };

    // ── Data ────────────────────────────────────────────────────────────
    std::unordered_map<std::string, Counter> counters_;
    std::unordered_map<std::string, Gauge> gauges_;
    std::unordered_map<std::string, Histogram> histograms_;

    std::unordered_map<std::string, Span> spans_;
    mutable std::mutex spans_mutex_;

    std::vector<HealthCheckEntry> health_checks_;

    EventBus* event_bus_ = nullptr;
};

// ── Global telemetry accessor ───────────────────────────────────────────
inline Telemetry& global_telemetry() {
    static Telemetry telemetry;
    return telemetry;
}

// ── Convenience macros ──────────────────────────────────────────────────
#define TELEMETRY_COUNTER(name, labels) \
    smo::runtime::global_telemetry().increment_counter(name, labels)

#define TELEMETRY_GAUGE(name, value, labels) \
    smo::runtime::global_telemetry().set_gauge(name, value, labels)

#define TELEMETRY_HISTOGRAM(name, value, labels) \
    smo::runtime::global_telemetry().record_histogram(name, value, labels)

} // namespace smo::runtime