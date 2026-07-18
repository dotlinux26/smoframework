# mesh_manager.cpp is added directly to smo_core to resolve circular
# dependency: core/bootstrap/ references MeshManager, but smo_mesh
# links smo_core. Moving mesh_manager into smo_core avoids the cycle.
target_sources(smo_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mesh_manager.cpp
)

target_sources(smo_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/mesh_manager.hpp
)

# smo_mesh remains for mesh_fsm.cpp (no circular dep)
add_library(smo_mesh STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/mesh_fsm.cpp
)

target_link_libraries(smo_mesh PUBLIC
    smo_core
    smo_blake3_provider
)

target_include_directories(smo_mesh PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
