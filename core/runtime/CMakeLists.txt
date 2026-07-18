# ── Runtime Module ────────────────────────────────────────────────────
add_library(smo_runtime STATIC
    event_bus.cpp
    dispatcher.cpp
    output_manager.cpp
    runtime_kernel.cpp
    middleware.cpp
    plan_executor.cpp
    contract_registry.cpp
    contracts/join_contract.cpp
    contracts/bootstrap_contract.cpp
    contracts/governance_contract.cpp
    contracts/recovery_contract.cpp
    contracts/file_contract.cpp
    contracts/process_contract.cpp
)

target_include_directories(smo_runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/smo/runtime>
)

target_link_libraries(smo_runtime PUBLIC
    smo_core
    smo_storage
    smo_contract
)

target_compile_features(smo_runtime PUBLIC cxx_std_20)

# ── Tests ───────────────────────────────────────────────────────────────
if(BUILD_TESTING)
    add_executable(test_runtime test/test_runtime.cpp)
    target_link_libraries(test_runtime PRIVATE smo_runtime gtest_main)
    add_test(NAME runtime_tests COMMAND test_runtime)
endif()

# Install
install(TARGETS smo_runtime
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY .
    DESTINATION include/smo/runtime
    FILES_MATCHING PATTERN "*.hpp"
)