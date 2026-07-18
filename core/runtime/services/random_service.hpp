#pragma once

#include "core/types.hpp"

#include <cstdint>

namespace smo::runtime {

// ── RandomService (RFC 0037 §2.5) ────────────────────────────────────
class RandomService {
public:
    virtual ~RandomService() = default;
    virtual Bytes random_bytes(size_t count) = 0;
    virtual uint64_t random_u64() = 0;
    virtual void seed(const Bytes& entropy) = 0;
};

} // namespace smo::runtime
