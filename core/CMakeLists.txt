add_library(smo_core STATIC)
target_include_directories(smo_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)
target_link_libraries(smo_core PUBLIC fmt::fmt)

# ── Sub-modules ──────────────────────────────────────────────────────
add_subdirectory(intent)
add_subdirectory(opcode)
add_subdirectory(contract)
add_subdirectory(capability)
add_subdirectory(session)
add_subdirectory(state)
add_subdirectory(errors)
add_subdirectory(crypto)
add_subdirectory(storage)
add_subdirectory(identity)
add_subdirectory(certificate)
add_subdirectory(fsm)
add_subdirectory(transport)
add_subdirectory(discovery)
add_subdirectory(governance)
add_subdirectory(trust)
add_subdirectory(select)
add_subdirectory(network)
add_subdirectory(enroll)
add_subdirectory(authority)
add_subdirectory(mesh)
