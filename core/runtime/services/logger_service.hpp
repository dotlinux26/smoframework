#pragma once

#include "core/types.hpp"
#include "core/errors/error.hpp"

#include <string>

namespace smo::runtime {

// ── LoggerService (RFC 0037 §2.5) ────────────────────────────────────
class LoggerService {
public:
    virtual ~LoggerService() = default;
    virtual void debug(const std::string& msg) = 0;
    virtual void info(const std::string& msg) = 0;
    virtual void warn(const std::string& msg) = 0;
    virtual void error(const std::string& msg) = 0;
};

} // namespace smo::runtime
