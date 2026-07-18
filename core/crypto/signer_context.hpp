#pragma once

#include "../errors/error.hpp"
#include "../types.hpp"
#include "impl.hpp"

#include <cstring>
#include <memory>
#include <string>

namespace smo::crypto {

// ── SignerMetadata ────────────────────────────────────────────────────
struct SignerMetadata {
    std::string backend;       // "Software", "TPM", "HSM", "YubiKey", "CloudKMS"
    std::string algorithm;     // "ML-DSA-65", "Ed25519", "Falcon-1024"
    std::string provider;      // "OpenSSL", "oqs", "YubiKey PIV"
    std::string key_id;        // "Slot 3", "Key ID abc123", "HSM 0x04"
    bool        persistent = false;   // Key survives process restart
    bool        hardware    = false;   // Backed by dedicated hardware
    std::string origin;        // "recovery-package", "genesis", "hsm-provision"
    uint64_t    created_at = 0;
};

// ── SignerContext (abstract) ──────────────────────────────────────────
class SignerContext {
public:
    virtual ~SignerContext() = default;
    virtual Result<Bytes> sign(BytesView msg, RngRef& rng) = 0;
    virtual const SignerMetadata& metadata() const = 0;
    virtual bool valid() const = 0;
    virtual void destroy() = 0;
};

// ── SoftwareSignerContext ─────────────────────────────────────────────
class SoftwareSignerContext : public SignerContext {
public:
    SoftwareSignerContext(BytesView secret_key,
                          SignerImpl signer,
                          SignerMetadata meta = {})
        : signer_(signer), meta_(std::move(meta))
    {
        secret_key_.assign(secret_key.begin(), secret_key.end());
    }

    ~SoftwareSignerContext() override { destroy(); }

    SoftwareSignerContext(SoftwareSignerContext&& other) noexcept
        : secret_key_(std::move(other.secret_key_)),
          signer_(other.signer_),
          meta_(std::move(other.meta_)),
          valid_(other.valid_)
    {
        other.valid_ = false;
    }

    SoftwareSignerContext& operator=(SoftwareSignerContext&& other) noexcept {
        if (this != &other) {
            destroy();
            secret_key_ = std::move(other.secret_key_);
            signer_     = other.signer_;
            meta_       = std::move(other.meta_);
            valid_      = other.valid_;
            other.valid_ = false;
        }
        return *this;
    }

    SoftwareSignerContext(const SoftwareSignerContext&) = delete;
    SoftwareSignerContext& operator=(const SoftwareSignerContext&) = delete;

    Result<Bytes> sign(BytesView msg, RngRef& rng) override {
        if (!valid_) {
            return SMO_ERR_CRYPTO(110, Error, NoRetry, ManualIntervention,
                                  "SignerContext has been destroyed");
        }
        if (!signer_.sign) {
            return SMO_ERR_CRYPTO(110, Error, NoRetry, ManualIntervention,
                                  "SignerImpl::sign is null");
        }
        return signer_.sign(msg, BytesView(secret_key_), rng);
    }

    const SignerMetadata& metadata() const override { return meta_; }
    bool valid() const override { return valid_; }

    void destroy() override {
        if (!secret_key_.empty()) {
            std::memset(secret_key_.data(), 0, secret_key_.size());
            secret_key_.clear();
            secret_key_.shrink_to_fit();
        }
        valid_ = false;
    }

private:
    Bytes          secret_key_;
    SignerImpl     signer_{};
    SignerMetadata meta_;
    bool           valid_ = true;
};

// ── Factory ───────────────────────────────────────────────────────────
inline std::unique_ptr<SignerContext> make_software_signer_context(
    BytesView secret_key,
    SignerImpl signer,
    SignerMetadata meta = {})
{
    return std::make_unique<SoftwareSignerContext>(secret_key, signer, std::move(meta));
}

} // namespace smo::crypto
