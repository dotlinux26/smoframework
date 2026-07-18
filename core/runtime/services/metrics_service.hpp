#pragma once

#include "core/types.hpp"
#include "core/errors/error.hpp"

#include <string>
#include <cstdint>

namespace smo::runtime {

// ── MetricsService ───────────────────────────────────────────────────
class MetricsService {
public:
    virtual ~MetricsService() = default;
    virtual void increment(const std::string& metric, int64_t delta = 1) = 0;
    virtual void gauge(const std::string& metric, double value) = 0;
    virtual void histogram(const std::string& metric, double value) = 0;
    virtual void timing(const std::string& metric, uint64_t duration_ns) = 0;
};

} // namespace smo::runtime
