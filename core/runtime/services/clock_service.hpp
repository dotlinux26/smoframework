#pragma once

#include <cstdint>

namespace smo::runtime {

// ── ClockService (RFC 0037 §2.5) ─────────────────────────────────────
class ClockService {
public:
    virtual ~ClockService() = default;
    virtual uint64_t now_ns() = 0;
    virtual uint64_t wall_clock_ns() = 0;
    virtual void advance(uint64_t delta_ns) = 0;
};

} // namespace smo::runtime
