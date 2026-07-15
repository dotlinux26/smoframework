#pragma once

#include "fwd.hpp"

#include <cstdint>

namespace smo {

// Known Crypto Suite IDs (see SPEC.md §6.4)
inline constexpr CryptoSuiteID kSuiteClassical    = 1;
inline constexpr CryptoSuiteID kSuiteHybridPQC    = 2;
inline constexpr CryptoSuiteID kSuitePurePQC      = 3;

inline constexpr CryptoSuiteID kSuiteMin          = 1;
inline constexpr CryptoSuiteID kSuiteMax          = 3;

// Known Hash Suite IDs (see SPEC.md §16.6)
inline constexpr HashSuiteID kHashSuiteBlake3     = 1;
inline constexpr HashSuiteID kHashSuiteSha256     = 2;
inline constexpr HashSuiteID kHashSuiteSha3_256   = 3;
inline constexpr HashSuiteID kHashSuiteXxHash3    = 101;

// Minimum crypto hash suite ID — suites below this are performance-only
inline constexpr HashSuiteID kMinCryptoHashSuite  = 1;
inline constexpr HashSuiteID kMaxCryptoHashSuite  = 3;

struct SuiteInfo {
    CryptoSuiteID id;
    const char*   name;
    const char*   signing;
    const char*   kem;
    const char*   aead;
    HashSuiteID   hash_suite;  // reference to a HashSuite, not hardcoded
};

} // namespace smo
