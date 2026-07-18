#pragma once

#include "core/types.hpp"
#include "core/errors/error.hpp"

#include <string>

namespace smo::runtime {

// ── FileService ──────────────────────────────────────────────────────
class FileService {
public:
    virtual ~FileService() = default;
    virtual Result<std::string> read_text(const std::string& path) = 0;
    virtual Result<Bytes> read_binary(const std::string& path) = 0;
    virtual Result<void> write_text(const std::string& path, const std::string& content) = 0;
    virtual Result<void> write_binary(const std::string& path, const Bytes& data) = 0;
    virtual Result<bool> exists(const std::string& path) = 0;
};

} // namespace smo::runtime
