#pragma once

#include <memory>
#include <optional>
#include <string>
#include "storage/storage.h"

namespace smo {

class SqliteStore;

// Compiled execution DAG records.
// DAG is immutable after compile (Invariant I-03).
class DagStore : public Store {
public:
    struct DagRecord {
        std::string graph_id;
        std::string intent_id;
        std::string dag_json;       // serialized DAG
        std::string dag_hash;       // Blake3 of the DAG
        int64_t     compiled_at{0};
    };

    explicit DagStore(std::string base_path) : base_path_(std::move(base_path)) {}

    std::error_code open() noexcept override;
    void close() noexcept override;
    std::error_code flush() noexcept override;

    std::error_code put(const DagRecord& dag);
    std::optional<DagRecord> get(const std::string& graph_id);

private:
    std::string base_path_;
    std::unique_ptr<SqliteStore> store_;
};

} // namespace smo
