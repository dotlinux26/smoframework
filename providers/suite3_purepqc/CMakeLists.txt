add_library(smo_suite3_purepqc STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/suite3_purepqc_provider.cpp
)

target_link_libraries(smo_suite3_purepqc PUBLIC smo_core smo_blake3_provider oqs)
target_include_directories(smo_suite3_purepqc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(smo_suite3_purepqc PROPERTIES POSITION_INDEPENDENT_CODE ON)
