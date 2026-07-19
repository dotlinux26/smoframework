#pragma once

#include "core/errors/error.hpp"
#include "core/types.hpp"

#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <any>
#include <typeindex>

namespace smo::runtime {

// Service Registry — lightweight DI container (MASTER_SYSTEM_MAP.md §XVI)
// Replaces constructor explosion in RuntimeContext
class ServiceRegistry {
public:
    ServiceRegistry() = default;
    ~ServiceRegistry() = default;

    // Register a service instance
    template <typename T>
    void register_service(const std::string& name, std::shared_ptr<T> instance) {
        std::lock_guard<std::mutex> lock(mutex_);
        services_[name] = std::static_pointer_cast<void>(instance);
    }

    // Get a service by name (type-safe)
    template <typename T>
    std::shared_ptr<T> get_service(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = services_.find(name);
        if (it == services_.end()) return nullptr;
        return std::static_pointer_cast<T>(it->second);
    }

    // Check if service exists
    bool has_service(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return services_.find(name) != services_.end();
    }

    // Remove service
    void unregister_service(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        services_.erase(name);
    }

    // Get all registered service names
    std::vector<std::string> list_services() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        names.reserve(services_.size());
        for (const auto& [name, _] : services_) names.push_back(name);
        return names;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<void>> services_;
};

// Global registry accessor
inline ServiceRegistry& global_registry() {
    static ServiceRegistry registry;
    return registry;
}

// Helper: register service with type-based name
template <typename T>
void register_service(std::shared_ptr<T> instance) {
    global_registry().register_service(typeid(T).name(), std::static_pointer_cast<void>(instance));
}

// Helper: get service by type
template <typename T>
std::shared_ptr<T> get_service() {
    return global_registry().get_service<T>(typeid(T).name());
}

} // namespace smo::runtime