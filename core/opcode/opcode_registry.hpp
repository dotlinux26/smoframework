#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "core/errors/error.hpp"
#include "core/opcode/opcode.h"

namespace smo {

struct OpcodeEntry {
    Opcode                    id;
    std::string               name;
    std::string               semver;
    uint32_t                  capability_mask{0};
    bool                      idempotent{false};
    std::string               contract_id;
    std::string               plugin_id;
    std::vector<std::string>  supported_arches;
};

class OpcodeRegistry {
public:
    OpcodeRegistry();

    void register_builtin(Opcode code, std::string_view name,
                          uint32_t capability_mask, bool idempotent);

    Result<void> register_plugin_opcode(const OpcodeEntry& entry);

    Result<OpcodeEntry> resolve(Opcode code) const;
    Result<OpcodeEntry> resolve_by_name(std::string_view name) const;

    std::vector<OpcodeEntry> all() const;

    static OpcodeRegistry& instance();

private:
    std::unordered_map<Opcode, OpcodeEntry> by_code_;
    std::unordered_map<std::string, Opcode> by_name_;
};

} // namespace smo
