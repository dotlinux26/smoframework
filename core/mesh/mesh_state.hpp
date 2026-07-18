#pragma once

#include <cstdint>
#include <string>

namespace smo::mesh {

enum class MeshState : uint8_t {
    Draft       = 0,
    Genesis     = 1,
    Bootstrap   = 2,
    Online      = 3,
    Maintenance = 4,
    Recovery    = 5,
    Archived    = 6,
};

inline std::string to_string(MeshState s) {
    switch (s) {
        case MeshState::Draft:       return "Draft";
        case MeshState::Genesis:     return "Genesis";
        case MeshState::Bootstrap:   return "Bootstrap";
        case MeshState::Online:      return "Online";
        case MeshState::Maintenance: return "Maintenance";
        case MeshState::Recovery:    return "Recovery";
        case MeshState::Archived:    return "Archived";
    }
    return "Unknown";
}

struct MeshTransition {
    MeshState from;
    MeshState to;
};

inline bool is_valid_transition(MeshState from, MeshState to) {
    switch (from) {
        case MeshState::Draft:
            return to == MeshState::Genesis;
        case MeshState::Genesis:
            return to == MeshState::Bootstrap;
        case MeshState::Bootstrap:
            return to == MeshState::Online || to == MeshState::Recovery;
        case MeshState::Online:
            return to == MeshState::Maintenance || to == MeshState::Recovery || to == MeshState::Archived;
        case MeshState::Maintenance:
            return to == MeshState::Online || to == MeshState::Recovery;
        case MeshState::Recovery:
            return to == MeshState::Online || to == MeshState::Archived;
        case MeshState::Archived:
            return false;
    }
    return false;
}

} // namespace smo::mesh
