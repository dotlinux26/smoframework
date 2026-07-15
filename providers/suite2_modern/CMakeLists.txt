add_library(smo_suite2_modern STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/suite2_modern_provider.cpp
)

target_link_libraries(smo_suite2_modern PUBLIC smo_core smo_blake3_provider smo_monocypher)
target_include_directories(smo_suite2_modern PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(smo_suite2_modern PROPERTIES POSITION_INDEPENDENT_CODE ON)
