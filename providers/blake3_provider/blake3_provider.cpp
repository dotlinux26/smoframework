#include "blake3_provider.hpp"
#include <blake3.h>

namespace smo {

Blake3Provider::Blake3Provider() {}

Bytes Blake3Provider::hash(BytesView data) {
    Bytes result(32);
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data.data(), data.size());
    blake3_hasher_finalize(&hasher, result.data(), result.size());
    return result;
}

Result<void> Blake3Provider::register_as_default() {
    HashProvider::set_default_provider(std::make_unique<Blake3Provider>());
    return {};
}

} // namespace smo
