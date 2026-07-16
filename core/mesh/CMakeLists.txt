add_library(smo_mesh STATIC
    mesh_manager.cpp
)

target_link_libraries(smo_mesh PUBLIC
    smo_core
    smo_blake3_provider
)

target_include_directories(smo_mesh PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)