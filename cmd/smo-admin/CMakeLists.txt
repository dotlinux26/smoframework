add_executable(smo-admin main.cpp)
target_link_libraries(smo-admin PRIVATE smo_sdk smo_tooling smo_core smo_mesh smo_genesis smo_suite1_classical smo_suite3_purepqc)
smo_set_warnings(smo-admin)
smo_set_optimisation(smo-admin)
smo_link_dependencies(smo-admin)

# Suppress unused-function warnings for static command functions
target_compile_options(smo-admin PRIVATE -Wno-unused-function -Wno-error)
