#include "bootstrap_service.hpp"

#include "../mesh/mesh_manager.hpp"
#include "../authority/authority.hpp"
#include "../recovery/crl.hpp"
#include "../join/join_protocol.hpp"

namespace smo::bootstrap {

Result<join::BootstrapSyncResponse> BootstrapService::handle_sync(
    const join::BootstrapSyncRequest& req)
{
    // Delegate to process_bootstrap_sync (implemented in join_protocol.cpp)
    return join::process_bootstrap_sync(req, mesh_mgr_, authority_, crl_);
}

} // namespace smo::bootstrap
