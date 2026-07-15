#pragma once

#include <cstddef>
#include <cstdint>

namespace smo {
namespace secure {

bool constant_time_compare(const uint8_t* a, const uint8_t* b, size_t len) noexcept;

inline bool constant_time_compare(const void* a, const void* b, size_t len) noexcept {
    return constant_time_compare(
        static_cast<const uint8_t*>(a),
        static_cast<const uint8_t*>(b),
        len
    );
}

} // namespace secure
} // namespace smo
