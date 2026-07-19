#pragma once

#include "../errors/error.hpp"
#include "../types.hpp"
#include "registry.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace smo::authority {

struct AuthorityInfo {
    std::string node_id_hex;
    std::string role;               // "Authority" | "Root"
    std::string status;
    std::string cert_fingerprint;
    std::string issuer_pubkey_hex;
    std::string subject_pubkey_hex;
    uint64_t    epoch = 0;
    int64_t     issued_at = 0;
    int64_t     expires_at = 0;
};

class AuthorityStore {
public:
    explicit AuthorityStore(const NodeRegistry& registry)
        : registry_(registry) {}

    AuthorityStore(AuthorityStore&&) = default;
    AuthorityStore& operator=(AuthorityStore&&) = default;

    AuthorityStore(const AuthorityStore&) = delete;
    AuthorityStore& operator=(const AuthorityStore&) = delete;

    Result<std::vector<AuthorityInfo>> list_authorities(const std::string& mesh_id = "") const;
    Result<std::vector<AuthorityInfo>> list_roots(const std::string& mesh_id = "") const;
    Result<bool> is_trusted(const std::string& node_id_hex, const std::string& mesh_id = "") const;
    Result<size_t> count(const std::string& mesh_id = "") const;

private:
    const NodeRegistry& registry_;
    Result<std::vector<AuthorityInfo>> list_by_role(
        const std::string& role, const std::string& mesh_id) const;
};

} // namespace smo::authority
