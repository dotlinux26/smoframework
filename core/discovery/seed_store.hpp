#pragma once

#include "../errors/error.hpp"
#include "../storage/database.hpp"
#include "../types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace smo {

struct SeedEntry {
    std::string endpoint;       // "host:port"
    uint32_t    priority = 1;   // lower = higher priority
    uint32_t    weight   = 100; // relative weight for weighted selection
    std::string mesh_id;        // optional — mesh this seed belongs to
    int64_t     added_at = 0;
};

class SeedStore {
public:
    SeedStore() = default;
    SeedStore(SeedStore&&) = default;
    SeedStore& operator=(SeedStore&&) = default;
    SeedStore(const SeedStore&) = delete;
    SeedStore& operator=(const SeedStore&) = delete;

    Result<void> open(std::string_view base_path);
    void close();
    bool is_open() const { return opened_; }

    Result<void> put(const SeedEntry& entry);
    Result<std::vector<SeedEntry>> list(const std::string& mesh_id = "") const;
    Result<void> remove(const std::string& endpoint);
    Result<SeedEntry> select_best(const std::string& mesh_id = "") const;
    Result<size_t> count() const;

private:
    bool opened_ = false;
    std::string db_path_;
    DatabaseHandle db_;
};

} // namespace smo
