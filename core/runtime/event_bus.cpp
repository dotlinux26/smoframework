#include "event_bus.hpp"

namespace smo::runtime {

void EventBus::publish(const Event& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscribers_.find(event.type);
    if (it == subscribers_.end()) return;

    // Copy subscribers to avoid holding lock during callback
    auto subs = it->second;
    // lock.unlock(); // lock_guard auto-unlocks on scope exit

    for (const auto& sub : subs) {
        try {
            sub.handler(event);
        } catch (...) {
            // Swallow exceptions in subscribers to not break other subscribers
        }
    }
}

SubscriptionId EventBus::subscribe(EventType type, EventHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    SubscriptionId id = next_sub_id_.fetch_add(1, std::memory_order_relaxed);
    subscribers_[type].push_back({id, std::move(handler)});
    return id;
}

void EventBus::unsubscribe(SubscriptionId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [type, subs] : subscribers_) {
        subs.erase(std::remove_if(subs.begin(), subs.end(),
            [id](const Subscription& s) { return s.id == id; }), subs.end());
    }
}

void EventBus::unsubscribe(EventType type, SubscriptionId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscribers_.find(type);
    if (it != subscribers_.end()) {
        auto& vec = it->second;
        vec.erase(std::remove_if(vec.begin(), vec.end(),
            [id](const Subscription& s) { return s.id == id; }), vec.end());
    }
}

size_t EventBus::subscriber_count(EventType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscribers_.find(type);
    return it == subscribers_.end() ? 0 : it->second.size();
}

} // namespace smo::runtime