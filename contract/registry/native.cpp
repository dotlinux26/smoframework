#include "native.hpp"
#include <sstream>

namespace smo {

static std::string make_native_json(const char* opcode, const char* name,
                                    const char* params_schema,
                                    const char* cap, uint32_t cap_level,
                                    bool idempotent, uint32_t timeout) {
    std::ostringstream os;
    os << R"({)";
    os << R"("contract_version":"1.0",)";
    os << R"("category":"native",)";
    os << R"("opcode":")" << opcode << R"(",)";
    os << R"("name":")" << name << R"(",)";
    os << R"("description":"Builtin native contract for )" << opcode << R"(",)";
    os << R"("publisher":"00000000-0000-0000-0000-000000000000",)";
    os << R"("semver":"1.0.0",)";
    os << R"("parameters":)" << params_schema << R"(,)";
    os << R"("capabilities_required":{" )" << cap << R"(":)" << cap_level << R"(},)";
    os << R"("compiler_hints":{"max_parallelism":1,"timeout_sec":)" << timeout
       << R"(,"idempotent":)" << (idempotent ? "true" : "false") << R"(},)";
    os << R"("signature":null)";
    os << R"(})";
    return os.str();
}

Result<void> register_native_contracts(ContractRegistry& registry) {
    // ls
    registry.register_native(make_native_json(
        "ls", "List Directory",
        R"([{"name":"path","type":"string","required":true}])",
        "filesystem_read", 1, true, 30));

    // put
    registry.register_native(make_native_json(
        "put", "Write File",
        R"([{"name":"source","type":"string","required":true},)"
        R"({"name":"target","type":"string","required":true}])",
        "filesystem_write", 1, true, 60));

    // get
    registry.register_native(make_native_json(
        "get", "Read File",
        R"([{"name":"path","type":"string","required":true}])",
        "filesystem_read", 1, true, 60));

    // exec
    registry.register_native(make_native_json(
        "exec", "Execute Command",
        R"([{"name":"command","type":"string","required":true},)"
        R"({"name":"args","type":"array","required":false}])",
        "process_exec", 1, false, 300));

    // quarantine
    registry.register_native(make_native_json(
        "quarantine", "Quarantine Node",
        R"([{"name":"target_node","type":"string","required":true}])",
        "node_quarantine", 1, false, 30));

    return {};
}

} // namespace smo
