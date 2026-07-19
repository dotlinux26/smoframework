#pragma once

#include "../errors/error.hpp"
#include "../types.hpp"
#include "join_protocol.hpp"
#include "../fsm/fsm.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace smo {
class MeshManager;
namespace authority { class MeshAuthority; }
}

namespace smo::join {

class JoinService {
public:
    struct Config {
        std::string data_dir;
    };

    explicit JoinService(MeshManager& mesh_mgr,
                         authority::MeshAuthority& authority,
                         Config config = {});

    ~JoinService();
    JoinService(JoinService&&) = default;
    JoinService& operator=(JoinService&&) = default;
    JoinService(const JoinService&) = delete;
    JoinService& operator=(const JoinService&) = delete;

    Result<void> initialize();

    // Process an incoming JoinRequest — returns JoinResponse CBOR
    Result<JoinResponse> handle_join_request(const JoinRequest& req);

    // Process an incoming BootstrapSyncRequest — returns BootstrapSyncResponse
    Result<BootstrapSyncResponse> handle_bootstrap_sync(const BootstrapSyncRequest& req);

    // FSM access
    Result<void> reset_fsm();
    Result<void> save_fsm_state();
    Result<void> load_fsm_state();
    JoinState current_state() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace smo::join
