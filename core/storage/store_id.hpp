#pragma once

#include <cstdint>

namespace smo {

enum class StoreID : uint8_t {
    Node        = 0,
    Mesh        = 1,
    Session     = 2,
    Trust       = 3,
    Audit       = 4,
    DAG         = 5,
    Peer        = 6,
    Governance  = 7,
};

inline constexpr int kStoreCount = 8;

struct StoreInfo {
    StoreID     id;
    const char* name;
    const char* filename;
    int         schema_version;
};

inline constexpr StoreInfo kStoreInfos[kStoreCount] = {
    { StoreID::Node,       "node",       "node.db",       1 },
    { StoreID::Mesh,       "mesh",       "mesh.db",       1 },
    { StoreID::Session,    "session",    "session.db",    1 },
    { StoreID::Trust,      "trust",      "trust.db",      1 },
    { StoreID::Audit,      "audit",      "audit.db",      1 },
    { StoreID::DAG,        "dag",        "dag.db",        1 },
    { StoreID::Peer,       "peer",       "peer.db",       1 },
    { StoreID::Governance, "governance", "governance.db", 1 },
};

inline const StoreInfo& store_info(StoreID id) {
    return kStoreInfos[static_cast<int>(id)];
}

} // namespace smo
