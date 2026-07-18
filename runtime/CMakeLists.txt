add_library(smo_legacy_runtime STATIC
    runtime.h
    runtime.hpp
)
target_include_directories(smo_legacy_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(smo_legacy_runtime PUBLIC
    smo_core smo_compiler smo_transport smo_storage smo_trust smo_acl smo_contract
)
smo_set_warnings(smo_legacy_runtime)
smo_set_optimisation(smo_legacy_runtime)

add_subdirectory(scheduler)
add_subdirectory(executor)
add_subdirectory(sandbox)
add_subdirectory(workerpool)
add_subdirectory(fsm)
add_subdirectory(audit)
add_subdirectory(recovery)
