#include "secure_compare.hpp"

namespace smo {
namespace secure {

bool constant_time_compare(const uint8_t* a, const uint8_t* b, size_t len) noexcept {
    if (len == 0) return true;
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i) {
        diff |= (a[i] ^ b[i]);
    }
    return diff == 0;
}

} // namespace secure
} // namespace smo
