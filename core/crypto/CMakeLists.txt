target_sources(smo_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hash_provider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hash/sha256.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/secure/zeroize.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/secure/secure_compare.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/random/getrandom.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/kdf/hkdf.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/signer/ed25519_provider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/kem/x25519_provider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/aead/xchacha20_provider.cpp
)

if(WITH_PQC)
    target_sources(smo_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/signer/mldsa_provider.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/kem/mlkem_provider.cpp
    )
endif()

target_sources(smo_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/fwd.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/suite.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/impl.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/registry.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hash_provider.hpp
)

target_include_directories(smo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(smo_core PUBLIC smo_monocypher)
if(WITH_PQC)
    target_link_libraries(smo_core PUBLIC oqs)
endif()
