#include "getrandom.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <sys/random.h>
#include <unistd.h>

namespace smo {
namespace random {

void fill(BytesMutView buf) {
    if (buf.empty()) return;
    size_t offset = 0;
    while (offset < buf.size()) {
        size_t chunk = buf.size() - offset;
        if (chunk > 256) chunk = 256;
        long r = ::getrandom(buf.data() + offset, chunk, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("getrandom() failed");
        }
        offset += static_cast<size_t>(r);
    }
}

} // namespace random
} // namespace smo
