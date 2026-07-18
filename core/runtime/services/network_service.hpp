#pragma once

#include "core/types.hpp"
#include "core/errors/error.hpp"

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace smo::runtime {

// ── NetworkService ───────────────────────────────────────────────────
class NetworkService {
public:
    virtual ~NetworkService() = default;
    virtual Result<void> send(const std::string& target, const std::vector<uint8_t>& data) = 0;
    virtual Result<std::vector<uint8_t>> request(const std::string& target, const std::vector<uint8_t>& data, uint64_t timeout_ns) = 0;
};

} // namespace smo::runtime
