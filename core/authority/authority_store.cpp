#include "authority_store.hpp"

namespace smo::authority {

Result<std::vector<AuthorityInfo>> AuthorityStore::list_by_role(
    const std::string& role, const std::string& mesh_id) const
{
    auto nodes = registry_.list_nodes(mesh_id);
    if (!nodes) return nodes.error();

    std::vector<AuthorityInfo> result;
    for (auto& n : nodes.value()) {
        if (n.role != role) continue;
        AuthorityInfo info;
        info.node_id_hex = n.node_id_hex;
        info.role = n.role;
        info.status = n.status;
        info.cert_fingerprint = n.cert_fingerprint;
        info.epoch = n.epoch;
        result.push_back(std::move(info));
    }
    return result;
}

Result<std::vector<AuthorityInfo>> AuthorityStore::list_authorities(
    const std::string& mesh_id) const
{
    return list_by_role("Authority", mesh_id);
}

Result<std::vector<AuthorityInfo>> AuthorityStore::list_roots(
    const std::string& mesh_id) const
{
    return list_by_role("Root", mesh_id);
}

Result<bool> AuthorityStore::is_trusted(
    const std::string& node_id_hex, const std::string& mesh_id) const
{
    auto auths = list_authorities(mesh_id);
    if (!auths) return false;
    for (auto& a : auths.value()) {
        if (a.node_id_hex == node_id_hex && a.status == "active")
            return true;
    }
    auto roots = list_roots(mesh_id);
    if (!roots) return false;
    for (auto& r : roots.value()) {
        if (r.node_id_hex == node_id_hex && r.status == "active")
            return true;
    }
    return false;
}

Result<size_t> AuthorityStore::count(const std::string& mesh_id) const
{
    auto nodes = registry_.list_nodes(mesh_id);
    if (!nodes) return nodes.error();
    size_t c = 0;
    for (auto& n : nodes.value()) {
        if (n.role == "Authority" || n.role == "Root") ++c;
    }
    return c;
}

} // namespace smo::authority
