#pragma once

#include "core/types.hpp"
#include "core/errors/error.hpp"

#include <string>
#include <cstdint>
#include <map>

namespace smo::runtime {

// ── AuditEvent ───────────────────────────────────────────────────────
struct AuditEvent {
    std::string event_type;
    std::string source_id;
    std::string correlation_id;
    std::string details;
    uint64_t timestamp_ns = 0;
    std::map<std::string, std::string> tags;
};

// ── AuditService ─────────────────────────────────────────────────────
class AuditService {
public:
    virtual ~AuditService() = default;
    virtual void emit(const AuditEvent& event) = 0;
};

} // namespace smo::runtime
