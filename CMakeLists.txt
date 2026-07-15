cmake_minimum_required(VERSION 3.20)
project(smo
    VERSION 3.0.0
    DESCRIPTION "Secure Mesh Operation — capability-scoped distributed execution runtime"
    LANGUAGES C CXX ASM
)

enable_testing()

# ── Feature flags ────────────────────────────────────────────────────
option(WITH_PQC "Build post-quantum crypto suites (ML-DSA, ML-KEM via liboqs)" ON)
if(WITH_PQC)
    add_compile_definitions(SMO_WITH_PQC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── Build type defaults ──────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── Third-party libraries ────────────────────────────────────────────
add_subdirectory(third_party/sqlite3)
add_subdirectory(third_party/blake3)
add_subdirectory(third_party/monocypher)
if(WITH_PQC)
    add_subdirectory(third_party/liboqs)
endif()

# ── Custom CMake modules ─────────────────────────────────────────────
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerOptions)
include(dependencies)

# ── Modules (ordered by dependency chain) ────────────────────────────
add_subdirectory(core)
add_subdirectory(protocol)
add_subdirectory(storage)
add_subdirectory(contract)
add_subdirectory(transport)
add_subdirectory(trust)
add_subdirectory(acl)
add_subdirectory(compiler)
add_subdirectory(runtime)
add_subdirectory(sdk)
add_subdirectory(providers)
add_subdirectory(tooling)
add_subdirectory(cmd)
add_subdirectory(tests)

# ── Aliases ──────────────────────────────────────────────────────────
add_library(smo::core      ALIAS smo_core)
add_library(smo::protocol  ALIAS smo_protocol)
add_library(smo::storage   ALIAS smo_storage)
add_library(smo::transport ALIAS smo_transport)
add_library(smo::trust     ALIAS smo_trust)
add_library(smo::acl       ALIAS smo_acl)
add_library(smo::compiler  ALIAS smo_compiler)
add_library(smo::runtime   ALIAS smo_runtime)
add_library(smo::sdk       ALIAS smo_sdk)
add_library(smo::blake3    ALIAS smo_blake3)
add_library(smo::blake3_provider ALIAS smo_blake3_provider)
add_library(smo::monocypher ALIAS smo_monocypher)
if(WITH_PQC)
    add_library(smo::liboqs ALIAS oqs)
    add_library(smo::suite3_purepqc ALIAS smo_suite3_purepqc)
endif()
add_library(smo::suite1_classical ALIAS smo_suite1_classical)
add_library(smo::suite2_modern ALIAS smo_suite2_modern)
add_library(smo::tooling   ALIAS smo_tooling)
