#include "mesh_fsm.hpp"

#include <algorithm>

namespace smo::mesh {

std::vector<TransitionRule> MeshFsm::default_rules() {
    return {
        // Draft → Genesis
        {(int64_t)MeshState::Draft,    (int64_t)MeshEvent::StartGenesis,      (int64_t)MeshState::Genesis},
        // Genesis → Bootstrap
        {(int64_t)MeshState::Genesis,  (int64_t)MeshEvent::BootstrapReady,    (int64_t)MeshState::Bootstrap},
        // Bootstrap → Online or Recovery
        {(int64_t)MeshState::Bootstrap,(int64_t)MeshEvent::AllSlotsFulfilled, (int64_t)MeshState::Online},
        {(int64_t)MeshState::Bootstrap,(int64_t)MeshEvent::TriggerRecovery,   (int64_t)MeshState::Recovery},
        // Online ↔ Maintenance
        {(int64_t)MeshState::Online,   (int64_t)MeshEvent::EnterMaintenance,  (int64_t)MeshState::Maintenance},
        {(int64_t)MeshState::Maintenance,(int64_t)MeshEvent::ExitMaintenance, (int64_t)MeshState::Online},
        // Online → Recovery
        {(int64_t)MeshState::Online,   (int64_t)MeshEvent::TriggerRecovery,   (int64_t)MeshState::Recovery},
        // Online → Archived
        {(int64_t)MeshState::Online,   (int64_t)MeshEvent::Archive,           (int64_t)MeshState::Archived},
        // Maintenance → Recovery
        {(int64_t)MeshState::Maintenance,(int64_t)MeshEvent::TriggerRecovery, (int64_t)MeshState::Recovery},
        // Recovery → Online or Archived
        {(int64_t)MeshState::Recovery, (int64_t)MeshEvent::RecoveryComplete,  (int64_t)MeshState::Online},
        {(int64_t)MeshState::Recovery, (int64_t)MeshEvent::Archive,           (int64_t)MeshState::Archived},
        // Timeout fallbacks
        {(int64_t)MeshState::Bootstrap,(int64_t)MeshEvent::Timeout,           (int64_t)MeshState::Recovery},
        {(int64_t)MeshState::Genesis,  (int64_t)MeshEvent::Timeout,           (int64_t)MeshState::Draft},
    };
}

std::vector<StateTimeout> MeshFsm::default_timeouts() {
    return {
        { (int64_t)MeshState::Genesis,   uint64_t(5) * 60 * 1'000'000'000ULL, (int64_t)MeshState::Draft     },   // 5 min
        { (int64_t)MeshState::Bootstrap, uint64_t(30) * 60 * 1'000'000'000ULL,(int64_t)MeshState::Recovery  },  // 30 min
        { (int64_t)MeshState::Online,    0,                                     (int64_t)MeshState::Online   },  // unlimited
        { (int64_t)MeshState::Maintenance, uint64_t(24) * 60 * 60 * 1'000'000'000ULL, (int64_t)MeshState::Recovery }, // 24h
        { (int64_t)MeshState::Recovery,  uint64_t(72) * 60 * 60 * 1'000'000'000ULL,(int64_t)MeshState::Archived}, // 72h
    };
}

MeshFsm::MeshFsm() {
    fsm.set_transitions(default_rules());
    fsm.set_timeouts(default_timeouts());
    fsm.reset((int64_t)MeshState::Draft);
}

Result<void> MeshFsm::on_event(MeshEvent event) {
    auto res = fsm.on_event((int64_t)event);
    if (!res) return res;
    return {};
}

MeshState MeshFsm::current_state() const {
    return (MeshState)fsm.current_state();
}

bool MeshFsm::is_online() const {
    return current_state() == MeshState::Online;
}

bool MeshFsm::is_bootstrapping() const {
    auto s = current_state();
    return s == MeshState::Genesis || s == MeshState::Bootstrap;
}

bool MeshFsm::is_terminal() const {
    return current_state() == MeshState::Archived;
}

std::vector<TransitionRecord> MeshFsm::recent_history(size_t n) const {
    auto all = fsm.history();
    if (all.size() <= n) return all;
    return std::vector<TransitionRecord>(all.end() - (ptrdiff_t)n, all.end());
}

} // namespace smo::mesh
