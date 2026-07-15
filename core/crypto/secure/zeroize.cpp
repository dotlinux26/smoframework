#include "zeroize.hpp"

#include <cstring>

namespace smo {
namespace secure {

void zeroize(volatile void* ptr, size_t len) noexcept {
    if (ptr && len) {
        volatile uint8_t* p = static_cast<volatile uint8_t*>(ptr);
        for (size_t i = 0; i < len; ++i) {
            p[i] = 0;
        }
    }
}

} // namespace secure
} // namespace smo
