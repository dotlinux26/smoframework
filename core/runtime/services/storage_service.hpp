#pragma once

#include "core/types.hpp"
#include "core/errors/error.hpp"

#include <string>
#include <vector>

namespace smo::runtime {

// ── StorageService ───────────────────────────────────────────────────
class StorageService {
public:
    virtual ~StorageService() = default;
    virtual Result<void> put(const std::string& key, const Bytes& value) = 0;
    virtual Result<Bytes> get(const std::string& key) = 0;
    virtual Result<void> erase(const std::string& key) = 0;
    virtual Result<bool> exists(const std::string& key) = 0;
    virtual Result<std::vector<std::string>> list(const std::string& prefix) = 0;
};

} // namespace smo::runtime
