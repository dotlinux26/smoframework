#include "sync_service.hpp"

#include "../../discovery/gossip.hpp"
#include "../../recovery/crl.hpp"

#include <cstring>
#include <unordered_map>

namespace smo::sync {

struct SyncService::Impl {
    GossipEngine& gossip;
    recovery::CRL* crl;
    SyncSchedule schedule;

    int64_t last_membership_sync = 0;
    int64_t last_policy_sync = 0;
    int64_t last_crl_sync = 0;
    int64_t last_routing_sync = 0;

    std::unordered_map<std::string, DeltaCallback> delta_callbacks;

    Impl(GossipEngine& g, recovery::CRL* c, SyncSchedule s)
        : gossip(g), crl(c), schedule(s) {}

    uint32_t tick(int64_t now_ns) {
        uint32_t flags = 0;
        uint32_t flag = 1;

        auto check = [&](int64_t& last, uint64_t interval, const char* name) {
            if (interval > 0 && (last == 0 || now_ns - last >= interval)) {
                last = now_ns;
                flags |= flag;
                // Fire external delta callback if registered
                auto it = delta_callbacks.find(name);
                if (it != delta_callbacks.end()) {
                    (void)it->second(name);
                }
                // For membership delta: trigger gossip fanout
                if (std::strcmp(name, "membership") == 0) {
                    gossip.tick(now_ns);
                }
            }
            flag <<= 1;
        };

        check(last_routing_sync, schedule.routing_interval_ns, "routing");
        check(last_membership_sync, schedule.membership_interval_ns, "membership");
        check(last_policy_sync, schedule.policy_interval_ns, "policy");
        check(last_crl_sync, schedule.crl_interval_ns, "crl");

        return flags;
    }
};

SyncService::SyncService(GossipEngine& gossip,
                         recovery::CRL* crl,
                         SyncSchedule schedule)
    : impl_(std::make_unique<Impl>(gossip, crl, schedule))
{}

SyncService::~SyncService() = default;

void SyncService::start() { running_ = true; }
void SyncService::stop() { running_ = false; }

uint32_t SyncService::tick(int64_t now_ns) {
    if (!running_) return 0;
    return impl_->tick(now_ns);
}

void SyncService::on_delta(const std::string& delta_type, DeltaCallback cb) {
    impl_->delta_callbacks[delta_type] = std::move(cb);
}

Result<void> SyncService::sync_now(const std::string& delta_type) {
    auto it = impl_->delta_callbacks.find(delta_type);
    if (it != impl_->delta_callbacks.end()) {
        return it->second(delta_type);
    }
    return {};
}

} // namespace smo::sync
