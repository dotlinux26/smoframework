#include "join_service.hpp"

#include "../mesh/mesh_manager.hpp"
#include "../authority/authority.hpp"

#include <filesystem>
#include <fstream>

namespace smo::join {

class JoinService::Impl {
public:
    Impl(MeshManager& mesh_mgr, authority::MeshAuthority& authority, Config cfg)
        : mesh_mgr_(mesh_mgr), authority_(authority), config_(std::move(cfg))
    {
        fsm_.set_transitions(join_transition_table());
        fsm_.set_timeouts(join_timeout_table());
    }

    Result<void> initialize() {
        fsm_.set_transitions(join_transition_table());
        fsm_.set_timeouts(join_timeout_table());
        fsm_.reset(static_cast<int64_t>(JoinState::NEW));
        return {};
    }

    Result<JoinResponse> handle_join_request(const JoinRequest& req) {
        return process_join_request(req, mesh_mgr_, authority_);
    }

    Result<BootstrapSyncResponse> handle_bootstrap_sync(const BootstrapSyncRequest& req) {
        return process_bootstrap_sync(req, mesh_mgr_, authority_, nullptr);
    }

    void reset_fsm() {
        fsm_.reset(static_cast<int64_t>(JoinState::NEW));
    }

    Result<void> save_fsm_state() {
        auto serialized = fsm_.serialize();
        if (!serialized) return serialized.error();

        std::string path = config_.data_dir + "/join_state.bin";
        std::ofstream f(path, std::ios::binary);
        if (!f) {
            return SMO_ERR_STORAGE(900, Error, RetrySafe, None,
                                   "Failed to write join state");
        }
        f.write(reinterpret_cast<const char*>(serialized.value().data()),
                serialized.value().size());
        return {};
    }

    Result<void> load_fsm_state() {
        std::string path = config_.data_dir + "/join_state.bin";
        if (!std::filesystem::exists(path)) {
            return SMO_ERR_STORAGE(404, Info, NoRetry, None, "No saved join state");
        }

        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) {
            return SMO_ERR_STORAGE(900, Error, RetrySafe, None,
                                   "Failed to open join state");
        }
        size_t sz = static_cast<size_t>(f.tellg());
        f.seekg(0);
        Bytes data(sz);
        f.read(reinterpret_cast<char*>(data.data()), sz);

        auto rules = join_transition_table();
        auto timeouts = join_timeout_table();
        auto result = FsmInstance::deserialize(
            BytesView(data), rules.data(), rules.size(),
            timeouts.data(), timeouts.size());
        if (!result) return result.error();
        fsm_ = std::move(result.value());
        return {};
    }

    JoinState current_state() const {
        return static_cast<JoinState>(fsm_.current_state());
    }

private:
    MeshManager& mesh_mgr_;
    authority::MeshAuthority& authority_;
    Config config_;
    FsmInstance fsm_;
};

JoinService::JoinService(MeshManager& mesh_mgr,
                         authority::MeshAuthority& authority,
                         Config config)
    : impl_(std::make_unique<Impl>(mesh_mgr, authority, std::move(config)))
{}

JoinService::~JoinService() = default;

Result<void> JoinService::initialize() { return impl_->initialize(); }
Result<JoinResponse> JoinService::handle_join_request(const JoinRequest& req) {
    return impl_->handle_join_request(req);
}
Result<BootstrapSyncResponse> JoinService::handle_bootstrap_sync(const BootstrapSyncRequest& req) {
    return impl_->handle_bootstrap_sync(req);
}
Result<void> JoinService::reset_fsm() { impl_->reset_fsm(); return {}; }
Result<void> JoinService::save_fsm_state() { return impl_->save_fsm_state(); }
Result<void> JoinService::load_fsm_state() { return impl_->load_fsm_state(); }
JoinState JoinService::current_state() const { return impl_->current_state(); }

} // namespace smo::join
