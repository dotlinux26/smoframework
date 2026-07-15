#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include "storage/storage.h"

namespace smo {

class SqliteStore;

// Audit log — every FSM transition is recorded here.
// Invariant I-05: sufficient to reconstruct the full execution history.
// Invariant I-06: replay from audit log must produce identical state.
class AuditStore : public Store {
public:
    struct Entry {
        int64_t     sequence{0};
        std::string contract_id;
        std::string from_state;
        std::string to_state;
        std::string trigger;       // what caused the transition
        int64_t     timestamp{0};
        std::string signature;     // node's signature over this entry
    };

    explicit AuditStore(std::string base_path) : base_path_(std::move(base_path)) {}

    std::error_code open() noexcept override;
    void close() noexcept override;
    std::error_code flush() noexcept override;

    std::error_code append(const Entry& entry);
    std::vector<Entry> query(const std::string& contract_id);
    std::optional<Entry> last(const std::string& contract_id);

private:
    std::string base_path_;
    std::unique_ptr<SqliteStore> store_;
    int64_t sequence_ = 0;
};

} // namespace smo
