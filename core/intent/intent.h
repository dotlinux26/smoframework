#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "core/opcode/opcode.h"
#include "core/capability/capability.h"

namespace smo {

struct IntentId {
    std::string value;
};

struct Intent {
    IntentId          id;
    Opcode            opcode;
    std::string       requester;
    std::string       responder;
    std::string       witness;
    std::string       scope;            // "single", "mesh"
    std::vector<std::string> targets;
    double            trust_min{0.0};
    int32_t           parallelism{1};
    int64_t           created_at{0};

    // Invariant I-01: Intent MUST NOT carry application data.
    // Parameters reference external data by hash only.
    std::string       parameters_json;  // opcode-specific JSON blob

    // Optional: user specifies preferred ContractID (64-char hex).
    // Empty = "best match" via Contract Factory.
    std::string       contract_hint;
};

} // namespace smo
