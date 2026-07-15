# Network Layer — connectivity, bootstrap, topology management
# Full implementation in Sprint 4. Currently skeleton / interface stubs.
# See ARCHITECTURE.md §Connectivity Layer for design rationale.

add_library(smo_network INTERFACE)
target_include_directories(smo_network INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(smo_network INTERFACE smo_core)
