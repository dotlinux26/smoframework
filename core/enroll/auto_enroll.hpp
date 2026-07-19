#pragma once

#include "core/errors/error.hpp"
#include "core/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace smo {
namespace enroll {

struct AutoEnrollConfig {
    std::string data_dir;
    std::string mesh_dir;
    std::string node_name;
    uint16_t port = 5454;
    bool daemon_mode = true;
};

struct JoinResult {
    std::string mesh_id;
    std::string role;
    std::string profile;
    std::vector<std::string> bootstrap_endpoints;
    uint64_t manifest_epoch = 0;
    Bytes    manifest_digest;
    std::string node_name;
};

Result<JoinResult> run_join_command(const std::string& token_str,
                                     const std::string& data_dir,
                                     const std::string& node_name,
                                     uint16_t port,
                                     const std::string& mesh_dir);

} // namespace enroll
} // namespace smo