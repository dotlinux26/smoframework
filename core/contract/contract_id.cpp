#include "contract_id.hpp"
#include "core/crypto/hash_provider.hpp"
#include <algorithm>

namespace smo {

ContractID ContractID::compute(std::string_view canonical_json) {
    return ContractID{
        HashProvider::default_provider().hash_hex(canonical_json)
    };
}

bool ContractID::is_valid_hex(std::string_view h) {
    if (h.size() != 64) return false;
    return std::all_of(h.begin(), h.end(), [](char c) {
        return (c >= '0' && c <= '9') ||
               (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F');
    });
}

} // namespace smo
