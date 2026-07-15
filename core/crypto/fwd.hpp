#pragma once

#include "../types.hpp"

#include <cstdint>

namespace smo {

using CryptoSuiteID = uint16_t;
using HashSuiteID   = uint16_t;

enum class HashSuite : HashSuiteID {
    Blake3          = 1,  // default SMO native hash
    Sha256          = 2,  // FIPS compatibility
    Sha3_256        = 3,  // NIST future-proof
    // Non-cryptographic (performance hashes)
    XxHash3         = 101,
    Crc32C          = 102,
    CityHash        = 103,
};

inline constexpr bool is_crypto_hash(HashSuite s) noexcept {
    return s == HashSuite::Blake3 || s == HashSuite::Sha256 || s == HashSuite::Sha3_256;
}

inline constexpr bool is_performance_hash(HashSuite s) noexcept {
    return s == HashSuite::XxHash3 || s == HashSuite::Crc32C || s == HashSuite::CityHash;
}

struct EncapsResult {
    Bytes ciphertext;
    Bytes shared_secret;
};

struct KeypairResult {
    Bytes public_key;
    Bytes secret_key;
};

} // namespace smo
