#include "hkdf.hpp"

#include "core/crypto/hash/sha256.hpp"

#include <cstring>
#include <stdexcept>

namespace smo {
namespace kdf {

static Bytes hmac_sha256(BytesView key, BytesView data) {
    constexpr size_t BLOCK_SIZE = 64;
    hash::Sha256Provider h;
    Bytes key_block(BLOCK_SIZE, 0);

    if (key.size() > BLOCK_SIZE) {
        auto hashed = h.hash(key);
        std::memcpy(key_block.data(), hashed.data(), hashed.size());
    } else {
        std::memcpy(key_block.data(), key.data(), key.size());
    }

    Bytes ipad(BLOCK_SIZE), opad(BLOCK_SIZE);
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        ipad[i] = key_block[i] ^ 0x36;
        opad[i] = key_block[i] ^ 0x5c;
    }

    Bytes inner_input(ipad.size() + data.size());
    std::memcpy(inner_input.data(), ipad.data(), ipad.size());
    std::memcpy(inner_input.data() + ipad.size(), data.data(), data.size());
    auto inner = h.hash(inner_input);

    Bytes outer_input(opad.size() + inner.size());
    std::memcpy(outer_input.data(), opad.data(), opad.size());
    std::memcpy(outer_input.data() + opad.size(), inner.data(), inner.size());

    return h.hash(outer_input);
}

Bytes hkdf_extract(BytesView salt, BytesView ikm) {
    constexpr size_t HASH_LEN = 32;
    Bytes actual_salt;
    if (salt.empty()) {
        actual_salt.resize(HASH_LEN, 0);
    } else {
        actual_salt.assign(salt.data(), salt.data() + salt.size());
    }
    return hmac_sha256(actual_salt, ikm);
}

Bytes hkdf_expand(BytesView prk, BytesView info, size_t length) {
    constexpr size_t HASH_LEN = 32;
    if (length > 255 * HASH_LEN) {
        throw std::runtime_error("HKDF-Expand: requested length too large");
    }

    Bytes result;
    result.reserve(length);
    Bytes t;
    uint8_t counter = 0;

    while (result.size() < length) {
        ++counter;
        Bytes input;
        if (!t.empty()) {
            input.insert(input.end(), t.begin(), t.end());
        }
        input.insert(input.end(), info.begin(), info.end());
        input.push_back(counter);
        t = hmac_sha256(prk, input);
        size_t to_copy = std::min(t.size(), length - result.size());
        result.insert(result.end(), t.begin(), t.begin() + to_copy);
    }

    return result;
}

} // namespace kdf
} // namespace smo
