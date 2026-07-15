add_library(smo_monocypher STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/monocypher.c
)

target_include_directories(smo_monocypher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(smo_monocypher PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Disable pedantic warnings for third-party code
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(smo_monocypher PRIVATE -w)
endif()
