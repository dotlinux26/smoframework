#include "opcode_registry.hpp"
#include "core/capability/capability.h"

namespace smo {

OpcodeRegistry::OpcodeRegistry() {
    // Register builtin opcodes at construction
    register_builtin(Opcode::LS,         "ls",         0x01, true);
    register_builtin(Opcode::PUT,        "put",        0x01, true);
    register_builtin(Opcode::GET,        "get",        0x01, true);
    register_builtin(Opcode::EXEC,       "exec",       0x01, false);
    register_builtin(Opcode::QUARANTINE, "quarantine", 0x01, false);
    register_builtin(Opcode::MKDIR,      "mkdir",      0x01, true);
    register_builtin(Opcode::RM,         "rm",         0x01, false);
    register_builtin(Opcode::CP,         "cp",         0x01, true);
    register_builtin(Opcode::CUSTOM,     "custom",     0x01, false);
}

OpcodeRegistry& OpcodeRegistry::instance() {
    static OpcodeRegistry reg;
    return reg;
}

void OpcodeRegistry::register_builtin(Opcode code, std::string_view name,
                                      uint32_t capability_mask,
                                      bool idempotent) {
    OpcodeEntry entry;
    entry.id              = code;
    entry.name            = std::string(name);
    entry.semver          = "1.0.0";
    entry.capability_mask = capability_mask;
    entry.idempotent      = idempotent;
    entry.supported_arches = {"x86_64", "aarch64"};
    by_code_[code] = entry;
    by_name_[entry.name] = code;
}

Result<void> OpcodeRegistry::register_plugin_opcode(const OpcodeEntry& entry) {
    if (entry.id < Opcode(0xFB) || entry.id > Opcode(0xFE)) {
        return SMO_ERR_CRYPTO(8, Error, NoRetry, None,
                              "plugin opcode out of range (0xFB-0xFE)");
    }
    if (by_code_.count(entry.id)) {
        return SMO_ERR_CRYPTO(8, Error, NoRetry, None,
                              "duplicate opcode registration");
    }
    by_code_[entry.id] = entry;
    by_name_[entry.name] = entry.id;
    return {};
}

Result<OpcodeEntry> OpcodeRegistry::resolve(Opcode code) const {
    auto it = by_code_.find(code);
    if (it == by_code_.end()) {
        return SMO_ERR_CRYPTO(8, Error, NoRetry, None,
                              "unknown opcode");
    }
    return it->second;
}

Result<OpcodeEntry> OpcodeRegistry::resolve_by_name(
    std::string_view name) const {
    auto it = by_name_.find(std::string(name));
    if (it == by_name_.end()) {
        return SMO_ERR_CRYPTO(8, Error, NoRetry, None,
                              "unknown opcode name");
    }
    return by_code_.at(it->second);
}

std::vector<OpcodeEntry> OpcodeRegistry::all() const {
    std::vector<OpcodeEntry> entries;
    entries.reserve(by_code_.size());
    for (const auto& [_, entry] : by_code_)
        entries.push_back(entry);
    return entries;
}

} // namespace smo
