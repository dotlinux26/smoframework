#pragma once

#include "core/types.hpp"
#include "core/errors/error.hpp"

#include <string>
#include <vector>
#include <cstdint>

namespace smo::runtime {

// ── TransportService ─────────────────────────────────────────────────
class TransportService {
public:
    virtual ~TransportService() = default;
    virtual Result<void> send_message(const std::string& target, const std::string& opcode, const std::vector<uint8_t>& data) = 0;
    virtual Result<std::vector<uint8_t>> send_request(const std::string& target, const std::string& opcode, const std::vector<uint8_t>& data, uint64_t timeout_ns) = 0;
    virtual Result<void> broadcast(const std::string& opcode, const std::vector<uint8_t>& data) = 0;
};

} // namespace smo::runtime
