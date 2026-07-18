#pragma once

#include "core/types.hpp"
#include "core/errors/error.hpp"

#include <string>
#include <cstdint>

namespace smo::runtime {

// ── SchedulerService ─────────────────────────────────────────────────
class SchedulerService {
public:
    virtual ~SchedulerService() = default;
    virtual Result<void> schedule_retry(const std::string& execution_id, const std::string& step_id, uint64_t delay_ns) = 0;
    virtual Result<void> cancel_scheduled(const std::string& execution_id) = 0;
};

} // namespace smo::runtime
