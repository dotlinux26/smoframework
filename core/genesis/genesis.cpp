#include "genesis.hpp"

#include <algorithm>
#include <sstream>

namespace smo::genesis {

GenesisWizard::GenesisWizard(GenesisCryptoProvider crypto)
    : crypto_(std::move(crypto))
{}

Result<GenesisResult> GenesisWizard::run_stage_0(
    const std::string& mesh_id,
    const std::string& root_node_id,
    const std::string& root_public_key,
    std::unique_ptr<smo::crypto::SignerContext> root_signer,
    DeploymentProfile profile,
    uint32_t authority_count,
    const std::string& recovery_passphrase,
    uint64_t now_ns)
{

    if (stage_ != GenesisStage::NotStarted) {
        return SMO_ERR_GENESIS(1407, Info, NoRetry, GovernanceVote,
                               "genesis already in progress or complete");
    }

    GenesisResult result;

    // ── 1. Build manifest ─────────────────────────────────────────────
    GenesisManifest manifest;
    manifest.mesh_id         = mesh_id;
    manifest.root_public_key = root_public_key;
    manifest.profile         = profile;
    apply_profile_defaults(profile, manifest.authorities, manifest.quorum, manifest.fault_tolerance);

    // Override authority count if specified
    if (authority_count > 0) {
        manifest.authorities.preferred = authority_count;
        manifest.authorities.minimum   = std::min(manifest.authorities.minimum, authority_count);
        if (authority_count > manifest.authorities.maximum) {
            manifest.authorities.maximum = authority_count;
        }
        manifest.quorum = compute_recommended_quorum(authority_count);
    }

    if (!manifest.authorities.valid()) {
        return SMO_ERR_GENESIS(1401, Error, NoRetry, ManualIntervention,
                               "invalid authority range in manifest");
    }

    manifest.created_at = now_ns;

    // ── 2. Build recovery package ─────────────────────────────────────
    RecoveryPackage pkg;
    pkg.mesh_id            = mesh_id;
    pkg.root_public_key    = root_public_key;
    pkg.recovery_passphrase_hash = recovery_passphrase;  // placeholder
    pkg.epoch              = manifest.epoch;
    pkg.manifest_version   = manifest.manifest_version;
    pkg.created_at         = now_ns;

    auto manifest_bytes_res = manifest.serialize();
    if (!manifest_bytes_res) {
        return std::move(manifest_bytes_res).error();
    }
    auto manifest_bytes = std::move(manifest_bytes_res).value();
    pkg.genesis_manifest_json.assign(
        reinterpret_cast<const char*>(manifest_bytes.data()),
        manifest_bytes.size()
    );

    // ── 3. Build slot ring ────────────────────────────────────────────
    BootstrapSlotConfig slot_config;
    slot_config.count   = manifest.authorities.preferred;
    SlotRing ring;
    ring.config = slot_config;
    ring.slots.resize(ring.config.count);

    // ── 4. Create root session ────────────────────────────────────────
    RootSessionManager session_mgr;
    AuditSink noop_sink; // silent — caller can override
    auto session_id = session_mgr.start_session(
        root_node_id, root_public_key,
        std::move(root_signer),
        SessionPolicy::full(), std::move(noop_sink), now_ns);
    if (!session_id) {
        return std::move(session_id).error();
    }

    stage_ = GenesisStage::Stage0;

    result.manifest      = std::move(manifest);
    result.recovery_pkg  = std::move(pkg);
    result.slot_ring     = std::move(ring);
    result.root_session  = std::move(session_mgr.session);

    return result;
}

Result<Bytes> GenesisWizard::run_stage_1_claim_slot(
    GenesisResult& result,
    const std::string& role,
    const std::string& node_public_key,
    const std::string& join_token_id,
    uint64_t now_ns)
{
    if (stage_ == GenesisStage::NotStarted) {
        return SMO_ERR_GENESIS(1400, Error, NoRetry, ManualIntervention,
                               "must run stage 0 before claiming slots");
    }

    auto slot_idx_res = result.slot_ring.claim_slot(role, node_public_key, join_token_id, now_ns);
    if (!slot_idx_res) {
        return std::move(slot_idx_res).error();
    }
    auto slot_idx = std::move(slot_idx_res).value();

    stage_ = GenesisStage::Stage1;

    // Return slot info as serialized claim receipt
    auto& slot = result.slot_ring.slots[slot_idx];
    return slot.serialize();
}

Result<void> GenesisWizard::run_stage_1_fulfill(
    GenesisResult& result,
    uint32_t slot_index,
    const std::string& signed_csr,
    uint64_t now_ns)
{
    auto res = result.slot_ring.fulfill_slot(slot_index, signed_csr, now_ns);
    if (!res) return res;

    return {};
}

bool GenesisWizard::is_complete(const GenesisResult& result) const {
    if (result.slot_ring.slots.empty()) return false;
    return result.slot_ring.fulfilled_count() >=
           result.slot_ring.slots.size();
}

} // namespace smo::genesis
