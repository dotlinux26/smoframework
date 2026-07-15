#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace smo {

using Bytes       = std::vector<uint8_t>;
using BytesView   = std::span<const uint8_t>;
using BytesMutView = std::span<uint8_t>;

} // namespace smo
