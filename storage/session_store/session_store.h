#pragma once

#include <memory>
#include <optional>
#include <string>
#include "storage/storage.h"
#include "core/session/session.h"

namespace smo {

class SqliteStore;

// Active session state: capabilities, keys, expiration.
class SessionStore : public Store {
public:
    explicit SessionStore(std::string base_path) : base_path_(std::move(base_path)) {}

    std::error_code open() noexcept override;
    void close() noexcept override;
    std::error_code flush() noexcept override;

    std::error_code put(const Session& session);
    std::optional<Session> get(const SessionId& id);
    std::error_code remove(const SessionId& id);
    std::error_code expire_before(int64_t timestamp);

private:
    std::string base_path_;
    std::unique_ptr<SqliteStore> store_;
};

} // namespace smo
