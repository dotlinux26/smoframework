#include "manifest_store.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace smo {

Result<void> ManifestStore::open(std::string_view base_path) {
    if (opened_) return {};
    manifests_dir_ = std::string(base_path) + "/manifests";
    std::error_code ec;
    std::filesystem::create_directories(manifests_dir_, ec);
    if (ec) {
        return SMO_ERR_STORAGE(900, Error, RetrySafe, None,
                               "Failed to create manifests dir: " + ec.message());
    }
    opened_ = true;
    return {};
}

void ManifestStore::close() {
    opened_ = false;
}

std::string ManifestStore::manifest_path(uint64_t epoch) const {
    return manifests_dir_ + "/v" + std::to_string(epoch) + "_manifest.cbor";
}

std::string ManifestStore::latest_path() const {
    return manifests_dir_ + "/LATEST";
}

Result<void> ManifestStore::update_latest_symlink(uint64_t epoch) {
    std::string target = "v" + std::to_string(epoch) + "_manifest.cbor";
    std::string link = latest_path();
    std::filesystem::remove(link);
    std::error_code ec;
    std::filesystem::create_symlink(target, link, ec);
    if (ec) {
        // Fallback: write a marker file instead of symlink
        std::ofstream f(link);
        if (!f) {
            return SMO_ERR_STORAGE(900, Error, RetrySafe, None,
                                   "Failed to create LATEST marker");
        }
        f << target << "\n";
    }
    return {};
}

Result<void> ManifestStore::put(const ManifestRecord& rec) {
    if (!opened_)
        return SMO_ERR_STORAGE(905, Error, RetrySafe, None, "ManifestStore not open");

    std::string path = manifest_path(rec.epoch);
    if (std::filesystem::exists(path)) {
        return {}; // immutable — already exists
    }

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return SMO_ERR_STORAGE(900, Error, RetrySafe, None,
                               "Failed to write manifest to " + path);
    }
    f.write(reinterpret_cast<const char*>(rec.manifest_cbor.data()),
            rec.manifest_cbor.size());

    return update_latest_symlink(rec.epoch);
}

Result<ManifestRecord> ManifestStore::get(uint64_t epoch) const {
    if (!opened_)
        return SMO_ERR_STORAGE(905, Error, RetrySafe, None, "ManifestStore not open");

    std::string path = manifest_path(epoch);
    if (!std::filesystem::exists(path)) {
        return SMO_ERR_STORAGE(404, Info, NoRetry, None,
                               "Manifest epoch " + std::to_string(epoch) + " not found");
    }

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        return SMO_ERR_STORAGE(900, Error, RetrySafe, None,
                               "Failed to read manifest " + path);
    }

    size_t sz = static_cast<size_t>(f.tellg());
    f.seekg(0);

    ManifestRecord rec;
    rec.manifest_cbor.resize(sz);
    f.read(reinterpret_cast<char*>(rec.manifest_cbor.data()), sz);
    rec.epoch = epoch;
    return rec;
}

Result<ManifestRecord> ManifestStore::latest() const {
    if (!opened_)
        return SMO_ERR_STORAGE(905, Error, RetrySafe, None, "ManifestStore not open");

    auto epoch_res = latest_epoch();
    if (!epoch_res) return epoch_res.error();
    return get(epoch_res.value());
}

Result<uint64_t> ManifestStore::latest_epoch() const {
    if (!opened_)
        return SMO_ERR_STORAGE(905, Error, RetrySafe, None, "ManifestStore not open");

    std::string link = latest_path();

    // Try symlink first
    if (std::filesystem::is_symlink(link)) {
        auto target = std::filesystem::read_symlink(link);
        std::string name = target.filename().string();
        // Parse "v{epoch}_manifest.cbor"
        if (name.size() > 1 && name[0] == 'v') {
            auto us = name.find('_');
            if (us != std::string::npos) {
                return std::stoull(name.substr(1, us - 1));
            }
        }
    }

    // Fallback: marker file
    std::ifstream f(link);
    if (f) {
        std::string name;
        std::getline(f, name);
        if (name.size() > 1 && name[0] == 'v') {
            auto us = name.find('_');
            if (us != std::string::npos) {
                return std::stoull(name.substr(1, us - 1));
            }
        }
    }

    // Scan directory for highest epoch
    uint64_t max_ep = 0;
    for (auto& entry : std::filesystem::directory_iterator(manifests_dir_)) {
        std::string name = entry.path().filename().string();
        if (name.size() > 1 && name[0] == 'v') {
            auto us = name.find('_');
            if (us != std::string::npos) {
                try {
                    uint64_t ep = std::stoull(name.substr(1, us - 1));
                    if (ep > max_ep) max_ep = ep;
                } catch (...) {}
            }
        }
    }

    if (max_ep == 0) {
        return SMO_ERR_STORAGE(404, Info, NoRetry, None, "No manifests found");
    }
    return max_ep;
}

Result<std::vector<uint64_t>> ManifestStore::list_epochs() const {
    if (!opened_)
        return SMO_ERR_STORAGE(905, Error, RetrySafe, None, "ManifestStore not open");

    std::vector<uint64_t> epochs;
    for (auto& entry : std::filesystem::directory_iterator(manifests_dir_)) {
        std::string name = entry.path().filename().string();
        if (name.size() > 1 && name[0] == 'v') {
            auto us = name.find('_');
            if (us != std::string::npos) {
                try {
                    epochs.push_back(std::stoull(name.substr(1, us - 1)));
                } catch (...) {}
            }
        }
    }
    std::sort(epochs.begin(), epochs.end());
    return epochs;
}

Result<std::vector<ManifestRecord>> ManifestStore::since(uint64_t since_epoch) const {
    auto epochs = list_epochs();
    if (!epochs) return epochs.error();

    std::vector<ManifestRecord> result;
    for (auto ep : epochs.value()) {
        if (ep > since_epoch) {
            auto rec = get(ep);
            if (rec) result.push_back(std::move(rec.value()));
        }
    }
    return result;
}

} // namespace smo
