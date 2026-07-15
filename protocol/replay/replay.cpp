#include "replay.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <unordered_set>

namespace smo {

namespace {

uint64_t nonce_to_u64(const std::array<uint8_t, 8>& nonce) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | static_cast<uint64_t>(nonce[static_cast<size_t>(i)]);
    return v;
}

int64_t wall_clock_ms() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

} // anonymous namespace

struct ReplayProtector::Impl {
    ReplayConfig cfg;
    std::unordered_set<uint64_t> seen;
    std::deque<uint64_t> fifo;
};

ReplayProtector::ReplayProtector(ReplayConfig cfg) noexcept
    : impl_(std::make_unique<Impl>(Impl{cfg, {}, {}})) {}

ReplayProtector::~ReplayProtector() = default;

bool ReplayProtector::accept(std::array<uint8_t, 8> nonce, int64_t timestamp) noexcept {
    int64_t now = wall_clock_ms();

    // Timestamp window check
    int64_t delta = now - timestamp;
    if (delta < 0) delta = -delta;
    if (delta > impl_->cfg.max_time_delta_ms) return false;

    uint64_t val = nonce_to_u64(nonce);

    // Nonce seen before?
    if (impl_->seen.count(val)) return false;

    // Evict if at capacity
    if (impl_->seen.size() >= impl_->cfg.max_nonce_cache) {
        uint64_t oldest = impl_->fifo.front();
        impl_->fifo.pop_front();
        impl_->seen.erase(oldest);
    }

    impl_->fifo.push_back(val);
    impl_->seen.insert(val);
    return true;
}

bool ReplayProtector::accept(std::array<uint8_t, 8> nonce, int64_t timestamp,
                              int64_t now) noexcept
{
    int64_t delta = now - timestamp;
    if (delta < 0) delta = -delta;
    if (delta > impl_->cfg.max_time_delta_ms) return false;

    uint64_t val = nonce_to_u64(nonce);
    if (impl_->seen.count(val)) return false;

    if (impl_->seen.size() >= impl_->cfg.max_nonce_cache) {
        uint64_t oldest = impl_->fifo.front();
        impl_->fifo.pop_front();
        impl_->seen.erase(oldest);
    }

    impl_->fifo.push_back(val);
    impl_->seen.insert(val);
    return true;
}

void ReplayProtector::clear() noexcept {
    impl_->seen.clear();
    impl_->fifo.clear();
}

size_t ReplayProtector::size() const noexcept {
    return impl_->seen.size();
}

} // namespace smo
