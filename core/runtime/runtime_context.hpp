#pragma once

#include "runtime_types.hpp"

#include <string>
#include <cstdint>

namespace smo::runtime {

// ── RuntimeServices (RFC 0037 §2.4) ─────────────────────────────────
struct RuntimeServices {
    // Core services (injected via capability gating)
    class CryptoService*    crypto    = nullptr;
    class IdentityService*  identity  = nullptr;
    class VaultService*     vault     = nullptr;
    class StorageService*   storage   = nullptr;
    class FileService*      fs        = nullptr;
    class NetworkService*   network   = nullptr;
    class TransportService* transport = nullptr;
    class SchedulerService* scheduler = nullptr;
    class PolicyEngine*     policy    = nullptr;
    class AuditService*     audit     = nullptr;
    class HistoryService*   history   = nullptr;
    class MetricsService*   metrics   = nullptr;
    class LoggerService*    logger    = nullptr;
    class ClockService*     clock     = nullptr;
    class RandomService*    random    = nullptr;

    // Capability gating
    ContractCapabilities granted_caps;

    bool has_capability(ContractCapability cap) const {
        return granted_caps.test(static_cast<size_t>(cap));
    }
};

// ── RuntimeContext (RFC 0037 §2.1) ──────────────────────────────────
struct RuntimeContext {
    ExecutionInfo info;             // read-only execution metadata
    Variables vars;                 // mutable context key-value store
    RuntimeServices* services;      // injected services (may be null per capability)
    EventBus* event_bus = nullptr;  // event bus for middleware/audit (not for contracts)
};

} // namespace smo::runtime
