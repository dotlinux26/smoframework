add_library(smo_genesis STATIC
    genesis_manifest.cpp
    bootstrap_slot.cpp
    recovery_package.cpp
    root_session.cpp
    genesis.cpp
)

target_link_libraries(smo_genesis PUBLIC
    smo_core
)

target_include_directories(smo_genesis PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
