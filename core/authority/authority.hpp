#pragma once

#include "../crypto/impl.hpp"
#include "../certificate/certificate.hpp"
#include "../identity/identity.hpp"
#include "registry.hpp"

#include <cstdint>
#include <string>
#include <memory>

namespace smo::authority {

// ---------------------------------------------------------------------------
// MeshAuthority — manages Root/Authority keys, CSR signing, certificate issuance
//
// Lifecycle:
//   1. Mesh create → Authority.create_root() → generates Root keypair
//   2. Root signs Authority certificate → Authority keypair created
//   3. Root key exported (RecoveryPackage) → deleted from runtime
//   4. Authority key stays online for daily signing
// ---------------------------------------------------------------------------
class MeshAuthority {
public:
    struct Config {
        std::string mesh_id;
        std::string data_dir;          // Path to mesh data directory
        std::string registry_path;     // Path to node_registry.db
    };

    MeshAuthority();
    ~MeshAuthority();

    MeshAuthority(const MeshAuthority&) = delete;
    MeshAuthority& operator=(const MeshAuthority&) = delete;
    MeshAuthority(MeshAuthority&&) = default;
    MeshAuthority& operator=(MeshAuthority&&) = default;

    // ── Initialization ──────────────────────────────────────────

    // Initialize with crypto provider
    Result<void> init(const CryptoProvider& crypto, RngRef& rng);

    // Open existing authority (load keys from disk)
    Result<void> open(const Config& config);

    // ── Root / Authority Key Management ──────────────────────────

    // DEPRECATED — use the genesis flow instead.
    //   smo genesis create  (Stage 0 + Stage 1)
    //
    // Create mesh: generate Root keypair + first Authority keypair
    // root_pubkey_out: output hex of root public key
    // recovery_out: output AES-256-GCM encrypted recovery package
    [[deprecated("Use genesis flow: smo genesis create")]]
    Result<void> create_mesh_keys(const Config& config,
                                   const CryptoProvider& crypto,
                                   RngRef& rng,
                                   std::string& root_pubkey_out);

    // ── Bootstrap Signing (Root during Genesis/Bootstrap) ────────

    struct BootstrapSignRequest {
        Bytes csr_blob;            // Serialized CSR from joining node
        std::string mesh_id;
        std::string slot_token;    // Bootstrap Slot token for verification
        uint32_t slot_index;
    };

    // Sign a CSR during bootstrap. Validates slot token first, then
    // issues a certificate. Called by Root during Stage 1.
    // Returns: slot-signed Certificate
    Result<Certificate> sign_bootstrap_csr(const BootstrapSignRequest& req);

    // ── CSR Signing (Authority runtime) ──────────────────────────

    // Sign a CSR: validate, issue certificate, log to registry
    // csr_blob: raw serialized CSR bytes
    // mesh_id: target mesh
    // Returns: serialized Certificate
    Result<Certificate> sign_csr(BytesView csr_blob,
                                  const std::string& mesh_id);

    // ── Certificate Operations ───────────────────────────────────

    // Verify a certificate chain
    Result<void> verify_chain(const CertificateChain& chain) const;

    // Revoke a certificate by fingerprint
    Result<void> revoke_certificate(const std::string& cert_fingerprint,
                                     const std::string& reason);

    // ── Accessors ────────────────────────────────────────────────

    NodeRegistry& registry() { return *registry_; }
    const NodeRegistry& registry() const { return *registry_; }
    bool is_initialized() const { return initialized_; }
    std::string mesh_id() const { return config_.mesh_id; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    Config config_;
    bool initialized_ = false;
    std::unique_ptr<NodeRegistry> registry_;
};

} // namespace smo::authority
