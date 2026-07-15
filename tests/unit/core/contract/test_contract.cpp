#include <contract/contract_id.hpp>
#include <contract/contract_definition.hpp>
#include <crypto/hash_provider.hpp>
#include <opcode/opcode_registry.hpp>
#include <opcode/opcode.h>
#include <intent/intent.h>
#include "providers/blake3_provider/blake3_provider.hpp"
#include <cstdio>
#include <string>

using namespace smo;

static int failures = 0;

#define TEST(name)                                                      \
    do {                                                                \
        printf("  TEST %-50s ... ", name);                              \
        fflush(stdout);

#define END_TEST(result)                                                \
        if (result) {                                                   \
            printf("PASS\n");                                           \
        } else {                                                        \
            printf("FAIL\n");                                           \
            ++failures;                                                 \
        }                                                               \
    } while (false)

#define ASSERT(cond)                                                    \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("\n    ASSERTION FAILED at %s:%d: %s\n",             \
                   __FILE__, __LINE__, #cond);                          \
            return false;                                               \
        }                                                               \
    } while (false)

#define ASSERT_EQ(a, b)                                                 \
    do {                                                                \
        if ((a) != (b)) {                                               \
            printf("\n    ASSERTION FAILED at %s:%d: %s == %s\n",       \
                   __FILE__, __LINE__, #a, #b);                         \
            return false;                                               \
        }                                                               \
    } while (false)

// ==========================================================================
// HashProvider tests
// ==========================================================================

static bool test_hash_hex() {
    auto h = HashProvider::default_provider().hash_hex("");
    ASSERT(h.size() == 64);
    ASSERT(h.find("af1349b9") == 0);
    return true;
}

static bool test_hash_known() {
    auto h = HashProvider::default_provider().hash_hex("hello");
    ASSERT(h.size() == 64);
    ASSERT(h.find("ea8f163d") == 0);
    return true;
}

static bool test_hex_roundtrip() {
    auto h = HashProvider::default_provider().hash_hex("test data");
    auto raw = HashProvider::hex_to_bytes(h);
    auto h2 = HashProvider::bytes_to_hex(raw);
    ASSERT(h == h2);
    return true;
}

// ==========================================================================
// ContractID tests
// ==========================================================================

static bool test_contract_id_from_json() {
    std::string json = R"({"key":"value","contract_version":"1.0"})";
    auto cid = ContractID::compute(json);
    ASSERT(cid.hex.size() == 64);
    ASSERT(!cid.empty());
    ASSERT(ContractID::is_valid_hex(cid.hex));
    return true;
}

static bool test_contract_id_deterministic() {
    std::string json = R"({"a":1,"b":2})";
    auto cid1 = ContractID::compute(json);
    auto cid2 = ContractID::compute(json);
    ASSERT(cid1.hex == cid2.hex);
    return true;
}

static bool test_contract_id_different() {
    auto a = ContractID::compute(R"({"x":1})");
    auto b = ContractID::compute(R"({"x":2})");
    ASSERT(a.hex != b.hex);
    return true;
}

// ==========================================================================
// ContractDefinition tests
// ==========================================================================

static bool test_definition_minimal() {
    auto res = ContractDefinition::from_canonical_json(R"({
        "contract_version":"1.0",
        "category":"native",
        "opcode":"ls",
        "name":"List",
        "publisher":"00000000-0000-0000-0000-000000000000",
        "semver":"1.0.0",
        "parameters":[],
        "capabilities_required":{},
        "compiler_hints":{"max_parallelism":1,"timeout_sec":30,"idempotent":true},
        "signature":null
    })");
    ASSERT(res);
    auto& def = res.value();
    ASSERT(def.contract_version == "1.0");
    ASSERT(def.category == "native");
    ASSERT(def.opcode == "ls");
    ASSERT(def.name == "List");
    ASSERT(!def.contract_id.empty());
    return true;
}

static bool test_definition_roundtrip() {
    auto res = ContractDefinition::from_canonical_json(R"({
        "contract_version":"1.0",
        "category":"user_defined",
        "opcode":"exec",
        "name":"Execute Command",
        "description":"Run a shell command",
        "publisher":"a1b2c3d4-e5f6-7890-abcd-ef1234567890",
        "semver":"2.1.0",
        "parameters":[{"name":"cmd","type":"string","required":true}],
        "capabilities_required":{"process_exec":1},
        "compiler_hints":{"max_parallelism":1,"timeout_sec":60,"idempotent":false},
        "signature":"abc123"
    })");
    ASSERT(res);
    auto& def = res.value();

    auto json_out = def.to_canonical_json();
    auto res2 = ContractDefinition::from_canonical_json(json_out);
    ASSERT(res2);
    auto& def2 = res2.value();

    ASSERT(def.contract_id.hex == def2.contract_id.hex);
    ASSERT(def.opcode == def2.opcode);
    ASSERT(def.semver == def2.semver);
    ASSERT(def.parameters.size() == def2.parameters.size());
    ASSERT(def.capabilities_required.size() == def2.capabilities_required.size());
    ASSERT(def.compiler_hints.idempotent == def2.compiler_hints.idempotent);
    return true;
}

// ==========================================================================
// OpcodeRegistry tests
// ==========================================================================

static bool test_opcode_registry_ls() {
    auto& reg = OpcodeRegistry::instance();
    auto res = reg.resolve(Opcode::LS);
    ASSERT(res);
    ASSERT(res.value().name == "ls");
    ASSERT(res.value().idempotent);
    return true;
}

static bool test_opcode_registry_all() {
    auto& reg = OpcodeRegistry::instance();
    auto all = reg.all();
    ASSERT(all.size() >= 5);
    return true;
}

static bool test_opcode_registry_by_name() {
    auto& reg = OpcodeRegistry::instance();
    auto res = reg.resolve_by_name("exec");
    ASSERT(res);
    ASSERT(res.value().id == Opcode::EXEC);
    ASSERT(!res.value().idempotent);
    return true;
}

static bool test_opcode_registry_unknown() {
    auto& reg = OpcodeRegistry::instance();
    auto res = reg.resolve(Opcode(0xFF));
    ASSERT(res);
    return true;
}

// ==========================================================================
// Intent extension tests
// ==========================================================================

static bool test_intent_contract_hint() {
    Intent intent;
    intent.opcode = Opcode::GET;
    intent.contract_hint = "abc123...";
    ASSERT(intent.contract_hint == "abc123...");
    return true;
}

static bool test_intent_empty_hint() {
    Intent intent;
    intent.opcode = Opcode::PUT;
    ASSERT(intent.contract_hint.empty());
    return true;
}

// ==========================================================================
// Main
// ==========================================================================

int main() {
    Blake3Provider::register_as_default();

    printf("=== Contract Architecture Tests ===\n\n");

    printf("[HashProvider]\n");
    TEST("Blake3 empty hash");   END_TEST(test_hash_hex());
    TEST("Blake3 known input");  END_TEST(test_hash_known());
    TEST("Hex round-trip");      END_TEST(test_hex_roundtrip());

    printf("\n[ContractID]\n");
    TEST("Compute from JSON");       END_TEST(test_contract_id_from_json());
    TEST("Deterministic hash");     END_TEST(test_contract_id_deterministic());
    TEST("Different inputs differ"); END_TEST(test_contract_id_different());

    printf("\n[ContractDefinition]\n");
    TEST("Parse minimal");        END_TEST(test_definition_minimal());
    TEST("Serialize roundtrip");  END_TEST(test_definition_roundtrip());

    printf("\n[OpcodeRegistry]\n");
    TEST("Resolve LS");           END_TEST(test_opcode_registry_ls());
    TEST("List all opcodes");     END_TEST(test_opcode_registry_all());
    TEST("Resolve by name");      END_TEST(test_opcode_registry_by_name());
    TEST("Unknown opcode");       END_TEST(test_opcode_registry_unknown());

    printf("\n[Intent]\n");
    TEST("Contract hint present");   END_TEST(test_intent_contract_hint());
    TEST("Contract hint empty");     END_TEST(test_intent_empty_hint());

    printf("\n=== %s ===\n", failures ? "FAILURES" : "ALL PASS");
    return failures ? 1 : 0;
}
