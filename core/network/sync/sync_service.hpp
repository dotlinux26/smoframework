#pragma once

#include "../../errors/error.hpp"
#include "../../types.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace smo {
class GossipEngine;
namespace recovery { class CRL; }
}

namespace smo::sync {

// Per DISCUSSION_0039 §5.19: SyncScheduler with configurable intervals.
//   membership_delta   (every 30s)
//   policy_delta       (every 60s)
//   crl_delta          (every 300s)
//   manifest_delta     (on change)
//   routing_delta      (every 15s)
//   contracts_delta    (on change)
struct SyncSchedule {
    uint64_t membership_interval_ns = 30'000'000'000ULL;   // 30s
    uint64_t policy_interval_ns     = 60'000'000'000ULL;   // 60s
    uint64_t crl_interval_ns        = 300'000'000'000ULL;  // 300s
    uint64_t routing_interval_ns    = 15'000'000'000ULL;   // 15s
    uint64_t manifest_interval_ns   = 0;                   // on change (tick resets timer)
    uint64_t contracts_interval_ns  = 0;                   // on change
};

class SyncService {
public:
    using DeltaCallback = std::function<Result<void>(const std::string& delta_type)>;

    SyncService(GossipEngine& gossip,
                recovery::CRL* crl = nullptr,
                SyncSchedule schedule = {});

    ~SyncService();
    SyncService(SyncService&&) = default;
    SyncService& operator=(SyncService&&) = default;
    SyncService(const SyncService&) = delete;
    SyncService& operator=(const SyncService&) = delete;

    void start();
    void stop();
    bool is_running() const { return running_; }

    // Periodic tick — called by daemon main loop
    // Returns bitmap of triggered delta types
    uint32_t tick(int64_t now_ns);

    // Register external callback for specific delta type
    void on_delta(const std::string& delta_type, DeltaCallback cb);

    // Force a specific delta sync immediately
    Result<void> sync_now(const std::string& delta_type);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool running_ = false;
};

} // namespace smo::sync
