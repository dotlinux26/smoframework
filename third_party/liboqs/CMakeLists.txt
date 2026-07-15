# liboqs — downloaded at configure time via FetchContent
# Version: 0.16.0 (July 9, 2026)
# URL: https://github.com/open-quantum-safe/liboqs
# Update: change GIT_TAG below and re-run cmake
#
# Algorithms used:
#   - ML-DSA-44/65/87 (FIPS 204) — post-quantum signatures
#   - ML-KEM-512/768/1024 (FIPS 203) — post-quantum key encapsulation

include(FetchContent)

# Build only what we need
set(OQS_MINIMAL_BUILD "KEM_ml_kem_512;KEM_ml_kem_768;KEM_ml_kem_1024;SIG_ml_dsa_44;SIG_ml_dsa_65;SIG_ml_dsa_87" CACHE STRING "" FORCE)
set(OQS_BUILD_ONLY_LIB ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OQS_ENABLE_KEM_KYBER OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    liboqs
    GIT_REPOSITORY  https://github.com/open-quantum-safe/liboqs.git
    GIT_TAG         0.16.0
    GIT_SHALLOW     TRUE
    GIT_PROGRESS    FALSE
)

FetchContent_MakeAvailable(liboqs)

# Re-export includes for consumers
target_include_directories(oqs INTERFACE
    $<BUILD_INTERFACE:${liboqs_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
