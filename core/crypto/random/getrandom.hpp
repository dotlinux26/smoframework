#pragma once

#include "core/crypto/fwd.hpp"

#include <cstddef>
#include <cstdint>

namespace smo {
namespace random {

void fill(BytesMutView buf);

inline uint32_t uint32() {
    uint32_t val;
    fill(BytesMutView{reinterpret_cast<uint8_t*>(&val), sizeof(val)});
    return val;
}

inline uint64_t uint64() {
    uint64_t val;
    fill(BytesMutView{reinterpret_cast<uint8_t*>(&val), sizeof(val)});
    return val;
}

} // namespace random
} // namespace smo
