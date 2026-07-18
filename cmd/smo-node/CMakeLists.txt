add_executable(smo-node main.cpp)
target_link_libraries(smo-node PRIVATE smo_runtime smo_core smo_storage smo_contract smo_tooling smo_suite1_classical smo_suite3_purepqc)
smo_set_warnings(smo-node)
smo_set_optimisation(smo-node)
smo_link_dependencies(smo-node)

# Suppress unused-function warnings for static command functions
target_compile_options(smo-node PRIVATE -Wno-unused-function -Wno-error=old-style-cast)
