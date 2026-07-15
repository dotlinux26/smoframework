#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace smo {

struct ContractID {
    std::string hex;  // 64 hex chars (Blake3-256)

    bool operator==(const ContractID& other) const { return hex == other.hex; }
    bool operator!=(const ContractID& other) const { return hex != other.hex; }
    bool operator<(const ContractID& other) const { return hex < other.hex; }

    bool empty() const { return hex.empty(); }
    size_t size() const { return hex.size(); }

    static ContractID compute(std::string_view canonical_json);

    static bool is_valid_hex(std::string_view h);
};

} // namespace smo

namespace std {
template<>
struct hash<smo::ContractID> {
    size_t operator()(const smo::ContractID& id) const noexcept {
        return hash<std::string>{}(id.hex);
    }
};
} // namespace std
