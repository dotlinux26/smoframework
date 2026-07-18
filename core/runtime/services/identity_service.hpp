#pragma once

#include "core/types.hpp"
#include "core/errors/error.hpp"

#include <string>

namespace smo::runtime {

// ── IdentityService ──────────────────────────────────────────────────
class IdentityService {
public:
    virtual ~IdentityService() = default;
    virtual Result<std::string> node_id() const = 0;
    virtual Result<std::string> mesh_id() const = 0;
    virtual Result<PublicKey> public_key() const = 0;
    virtual Result<std::string> fingerprint() const = 0;
};

} // namespace smo::runtime
