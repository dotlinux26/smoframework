#pragma once

#include "../errors/error.hpp"
#include "../fsm/fsm.hpp"
#include "mesh_state.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace smo::mesh {

enum class MeshEvent : int64_t {
    StartGenesis      = 100,
    BootstrapReady    = 101,
    AllSlotsFulfilled = 102,
    EnterMaintenance  = 103,
    ExitMaintenance   = 104,
    TriggerRecovery   = 105,
    RecoveryComplete  = 106,
    Archive           = 107,
    Timeout           = 999,
};

struct MeshFsm {
    FsmInstance fsm;

    MeshFsm();

    Result<void> on_event(MeshEvent event);

    MeshState current_state() const;
    bool is_online() const;
    bool is_bootstrapping() const;
    bool is_terminal() const;

    std::vector<TransitionRecord> recent_history(size_t n) const;

    static std::vector<TransitionRule> default_rules();
    static std::vector<StateTimeout> default_timeouts();
};

} // namespace smo::mesh
