#pragma once

#include "core/crypto/fwd.hpp"

#include <cstddef>

namespace smo {
namespace kdf {

// HKDF (RFC 5869) using fixed HMAC-SHA256.
// Hash is NOT configurable — HKDF-SHA256 is the standard across ALL suites.
// This guarantees session key derivation is identical regardless of
// which hash suite the application uses.

Bytes hkdf_extract(BytesView salt, BytesView ikm);
Bytes hkdf_expand(BytesView prk, BytesView info, size_t length);

inline Bytes hkdf(BytesView salt, BytesView ikm, BytesView info, size_t length) {
    auto prk = hkdf_extract(salt, ikm);
    return hkdf_expand(prk, info, length);
}

} // namespace kdf
} // namespace smo
