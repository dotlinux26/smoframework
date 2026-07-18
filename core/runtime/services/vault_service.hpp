#pragma once

#include "core/types.hpp"
#include "core/errors/error.hpp"

#include <string>

namespace smo::runtime {

// ── VaultService (RFC 0037 §2.5) ─────────────────────────────────────
class VaultService {
public:
    virtual ~VaultService() = default;
    virtual Result<void> store(const std::string& key, const Bytes& secret) = 0;
    virtual Result<Bytes> retrieve(const std::string& key) = 0;
    virtual Result<void> delete_key(const std::string& key) = 0;
    virtual Result<bool> exists(const std::string& key) = 0;
};

} // namespace smo::runtime
