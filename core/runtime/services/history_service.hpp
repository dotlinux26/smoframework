#pragma once

#include "core/types.hpp"
#include "core/errors/error.hpp"

#include <string>
#include <vector>

namespace smo::runtime {

// ── HistoryService ───────────────────────────────────────────────────
class HistoryService {
public:
    virtual ~HistoryService() = default;
    virtual Result<void> record_execution(const std::string& execution_id, const std::string& contract_id, bool success, const std::string& details) = 0;
    virtual Result<std::vector<std::string>> get_history(const std::string& contract_id, size_t limit = 100) = 0;
};

} // namespace smo::runtime
