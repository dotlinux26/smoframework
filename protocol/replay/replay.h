#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <unordered_set>

namespace smo {

// §VI.2 — Replay protection via nonce + timestamp window.
//
// Every packet carries a nonce and a timestamp.
// Receivers maintain a sliding set of recently seen nonces.

struct ReplayConfig {
    int64_t  max_time_delta_ms{5'000};   // 5 second window
    size_t   max_nonce_cache{10'000};    // LRU eviction
};

class ReplayProtector {
public:
    explicit ReplayProtector(ReplayConfig cfg) noexcept;
    ~ReplayProtector();

    // Returns true if the packet is fresh (valid time window + unseen nonce).
    // Uses wall-clock time for the timestamp check.
    bool accept(std::array<uint8_t, 8> nonce, int64_t timestamp) noexcept;

    // Test-friendly overload with explicit current time.
    bool accept(std::array<uint8_t, 8> nonce, int64_t timestamp,
                int64_t now) noexcept;

    void clear() noexcept;

    size_t size() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace smo
