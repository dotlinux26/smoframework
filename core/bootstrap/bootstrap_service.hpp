#pragma once

#include "../errors/error.hpp"
#include "../types.hpp"
#include "../join/join_protocol.hpp"

namespace smo {
class MeshManager;
namespace recovery { class CRL; }
namespace authority { class MeshAuthority; }
}

namespace smo::bootstrap {

// Stateless BootstrapService — queries stores to build bootstrap sync responses.
// Per DISCUSSION_0039 §5.18: BootstrapService is stateless. All state lives in stores.
class BootstrapService {
public:
    BootstrapService(MeshManager& mesh_mgr,
                     authority::MeshAuthority& authority,
                     recovery::CRL* crl = nullptr)
        : mesh_mgr_(mesh_mgr), authority_(authority), crl_(crl) {}

    // Process a BootstrapSyncRequest — builds delta sync response
    Result<join::BootstrapSyncResponse> handle_sync(
        const join::BootstrapSyncRequest& req);

private:
    MeshManager& mesh_mgr_;
    authority::MeshAuthority& authority_;
    recovery::CRL* crl_;
};

} // namespace smo::bootstrap
