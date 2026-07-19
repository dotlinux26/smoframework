#pragma once

#include "../errors/error.hpp"
#include "../types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace smo {

struct ManifestRecord {
    uint64_t    epoch = 0;
    Bytes       manifest_cbor;
    std::string hash_hex;
    int64_t     created_at = 0;
};

class ManifestStore {
public:
    ManifestStore() = default;

    ManifestStore(ManifestStore&&) = default;
    ManifestStore& operator=(ManifestStore&&) = default;
    ManifestStore(const ManifestStore&) = delete;
    ManifestStore& operator=(const ManifestStore&) = delete;

    Result<void> open(std::string_view base_path);
    void close();

    bool is_open() const { return opened_; }

    // Put a new manifest version (immutable — stored as v{epoch}_manifest.cbor)
    Result<void> put(const ManifestRecord& rec);

    // Get manifest by epoch
    Result<ManifestRecord> get(uint64_t epoch) const;

    // Get latest manifest (from LATEST symlink)
    Result<ManifestRecord> latest() const;

    // List all manifest versions
    Result<std::vector<uint64_t>> list_epochs() const;

    // Current latest epoch
    Result<uint64_t> latest_epoch() const;

    // Delta: get all manifests with epoch > since_epoch
    Result<std::vector<ManifestRecord>> since(uint64_t since_epoch) const;

private:
    bool opened_ = false;
    std::string manifests_dir_;

    std::string manifest_path(uint64_t epoch) const;
    std::string latest_path() const;
    Result<void> update_latest_symlink(uint64_t epoch);
};

} // namespace smo
