# ── Protocol tests ────────────────────────────────────────────────────
add_executable(smo_test_protocol
    unit/protocol/test_protocol.cpp
)
target_link_libraries(smo_test_protocol PRIVATE
    smo_core smo_protocol fmt::fmt
)
smo_set_warnings(smo_test_protocol)
smo_set_optimisation(smo_test_protocol)
add_test(NAME protocol_model COMMAND smo_test_protocol)

# ── Trust tests ───────────────────────────────────────────────────────
add_executable(smo_test_trust
    unit/core/trust/test_trust.cpp
)
target_link_libraries(smo_test_trust PRIVATE
    smo_core fmt::fmt
)
smo_set_warnings(smo_test_trust)
smo_set_optimisation(smo_test_trust)
add_test(NAME trust_model COMMAND smo_test_trust)

# ── Governance tests ──────────────────────────────────────────────────
add_executable(smo_test_governance
    unit/core/governance/test_governance.cpp
)
target_link_libraries(smo_test_governance PRIVATE
    smo_core fmt::fmt
)
smo_set_warnings(smo_test_governance)
smo_set_optimisation(smo_test_governance)
add_test(NAME governance_model COMMAND smo_test_governance)

# ── Discovery tests ───────────────────────────────────────────────────
add_executable(smo_test_discovery
    unit/core/discovery/test_discovery.cpp
)
target_link_libraries(smo_test_discovery PRIVATE
    smo_core fmt::fmt
)
smo_set_warnings(smo_test_discovery)
smo_set_optimisation(smo_test_discovery)
add_test(NAME discovery_model COMMAND smo_test_discovery)

# ── Session tests ─────────────────────────────────────────────────────
add_executable(smo_test_session
    unit/core/session/test_session.cpp
)
target_link_libraries(smo_test_session PRIVATE
    smo_core fmt::fmt
)
smo_set_warnings(smo_test_session)
smo_set_optimisation(smo_test_session)
add_test(NAME session_model COMMAND smo_test_session)

# ── Transport tests ───────────────────────────────────────────────────
add_executable(smo_test_transport
    unit/core/transport/test_transport.cpp
)
target_link_libraries(smo_test_transport PRIVATE
    smo_core fmt::fmt
)
smo_set_warnings(smo_test_transport)
smo_set_optimisation(smo_test_transport)
add_test(NAME transport_model COMMAND smo_test_transport)

# ── High-Level Transport tests ────────────────────────────────────────
add_executable(smo_test_transport_hl
    unit/transport/test_transport_highlevel.cpp
)
target_link_libraries(smo_test_transport_hl PRIVATE
    smo_core smo_protocol smo_transport fmt::fmt
)
smo_set_warnings(smo_test_transport_hl)
smo_set_optimisation(smo_test_transport_hl)
add_test(NAME transport_highlevel COMMAND smo_test_transport_hl)

# ── FSM tests ────────────────────────────────────────────────────────
add_executable(smo_test_fsm
    unit/core/fsm/test_fsm.cpp
)
target_link_libraries(smo_test_fsm PRIVATE
    smo_core fmt::fmt
)
smo_set_warnings(smo_test_fsm)
smo_set_optimisation(smo_test_fsm)
add_test(NAME fsm_model COMMAND smo_test_fsm)

# ── Certificate tests ────────────────────────────────────────────────
add_executable(smo_test_certificate
    unit/core/certificate/test_certificate.cpp
)
target_link_libraries(smo_test_certificate PRIVATE
    smo_core fmt::fmt
)
smo_set_warnings(smo_test_certificate)
smo_set_optimisation(smo_test_certificate)
add_test(NAME certificate_model COMMAND smo_test_certificate)

# ── Identity tests ───────────────────────────────────────────────────
add_executable(smo_test_identity
    unit/core/identity/test_identity.cpp
)
target_link_libraries(smo_test_identity PRIVATE
    smo_core fmt::fmt
)
smo_set_warnings(smo_test_identity)
smo_set_optimisation(smo_test_identity)
add_test(NAME identity_model COMMAND smo_test_identity)

# ── Storage store tests ─────────────────────────────────────────────
add_executable(smo_test_storage_stores
    unit/storage/test_storage_stores.cpp
)
target_link_libraries(smo_test_storage_stores PRIVATE
    smo_core smo_protocol smo_storage fmt::fmt
)
smo_set_warnings(smo_test_storage_stores)
smo_set_optimisation(smo_test_storage_stores)
add_test(NAME storage_stores COMMAND smo_test_storage_stores)

# ── Storage tests ────────────────────────────────────────────────────
add_executable(smo_test_storage
    unit/core/storage/test_storage.cpp
)
target_link_libraries(smo_test_storage PRIVATE
    smo_core fmt::fmt
)
smo_set_warnings(smo_test_storage)
smo_set_optimisation(smo_test_storage)
add_test(NAME storage_model COMMAND smo_test_storage)

# ── Crypto tests ─────────────────────────────────────────────────────
add_executable(smo_test_crypto
    unit/core/crypto/test_crypto.cpp
)
target_link_libraries(smo_test_crypto PRIVATE
    smo_core fmt::fmt
)
smo_set_warnings(smo_test_crypto)
smo_set_optimisation(smo_test_crypto)
add_test(NAME crypto_model COMMAND smo_test_crypto)

# ── Error model tests ────────────────────────────────────────────────
add_executable(smo_test_error
    unit/core/errors/test_error.cpp
)
target_link_libraries(smo_test_error PRIVATE
    smo_core fmt::fmt
)
smo_set_warnings(smo_test_error)
smo_set_optimisation(smo_test_error)
add_test(NAME error_model COMMAND smo_test_error)

# ── Contract tests ──────────────────────────────────────────────────
add_executable(smo_test_contract
    unit/core/contract/test_contract.cpp
)
target_link_libraries(smo_test_contract PRIVATE
    smo_core smo_contract smo_blake3_provider fmt::fmt
)
smo_set_warnings(smo_test_contract)
smo_set_optimisation(smo_test_contract)
add_test(NAME contract_model COMMAND smo_test_contract)

# ── Legacy test executable ───────────────────────────────────────────
add_executable(smo_all_tests
    unit/core_test.cpp
    unit/protocol_test.cpp
    unit/compiler_test.cpp
)

target_link_libraries(smo_all_tests PRIVATE
    smo_core smo_protocol smo_compiler smo_transport
    smo_storage smo_trust smo_acl smo_runtime
)

smo_set_warnings(smo_all_tests)
smo_set_optimisation(smo_all_tests)
smo_link_dependencies(smo_all_tests)

# ── Register legacy tests ────────────────────────────────────────────
add_test(NAME core     COMMAND smo_all_tests --subtest core)
add_test(NAME protocol COMMAND smo_all_tests --subtest protocol)
add_test(NAME compiler COMMAND smo_all_tests --subtest compiler)

# Future test suites (placeholders for now):
# add_subdirectory(integration)
# add_subdirectory(mesh)
# add_subdirectory(chaos)
# add_subdirectory(replay)
# add_subdirectory(adversarial)
