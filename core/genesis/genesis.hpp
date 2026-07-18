#pragma once

#include "bootstrap_slot.hpp"
#include "genesis_manifest.hpp"
#include "recovery_package.hpp"
#include "root_session.hpp"

#include "../errors/error.hpp"
#include "../types.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace smo::genesis {

enum class GenesisStage : uint8_t {
    NotStarted   = 0,
    Stage0       = 1,  // Root bootstrap: manifest + recovery.pkg + slots
    Stage1       = 2,  // Authority bootstrap: slots claimed → certs issued
    Complete     = 3,
    Failed       = 4,
};

struct GenesisResult {
    GenesisManifest manifest;
    RecoveryPackage recovery_pkg;
    SlotRing slot_ring;
    RootSession root_session;
    std::string mesh_json_path;
    std::string recovery_pkg_path;
};

// ── Callbacks for crypto operations (injected to avoid hard crypto dep) ──
struct GenesisCryptoProvider {
    std::function<Result<Bytes>(const std::string&)> hash;
    std::function<Result<Bytes>(BytesView data, BytesView key)> encrypt_keypair;
    std::function<Result<bool>(BytesView data, BytesView signature, BytesView pubkey)> verify;
};

class GenesisWizard {
public:
    explicit GenesisWizard(GenesisCryptoProvider crypto);

    // Run Stage 0: create manifest, recovery package, slot ring
    Result<GenesisResult> run_stage_0(
        const std::string& mesh_id,
        const std::string& root_node_id,
        const std::string& root_public_key,
        std::unique_ptr<smo::crypto::SignerContext> root_signer,
        DeploymentProfile profile,
        uint32_t authority_count,
        const std::string& recovery_passphrase,
        uint64_t now_ns
    );

    // Run Stage 1: claim a slot and get signed CSR for a joining authority
    Result<Bytes> run_stage_1_claim_slot(
        GenesisResult& result,
        const std::string& role,
        const std::string& node_public_key,
        const std::string& join_token_id,
        uint64_t now_ns
    );

    // Finalize slot fulfillment after Root signs CSR
    Result<void> run_stage_1_fulfill(
        GenesisResult& result,
        uint32_t slot_index,
        const std::string& signed_csr,
        uint64_t now_ns
    );

    // Check if genesis is complete
    bool is_complete(const GenesisResult& result) const;

    GenesisStage stage() const { return stage_; }

private:
    GenesisCryptoProvider crypto_;
    GenesisStage stage_ = GenesisStage::NotStarted;
};

} // namespace smo::genesis
