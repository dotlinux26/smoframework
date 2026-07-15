add_library(smo_suite1_classical STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/suite1_classical_provider.cpp
)

target_link_libraries(smo_suite1_classical PUBLIC smo_core)
target_include_directories(smo_suite1_classical PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(smo_suite1_classical PROPERTIES POSITION_INDEPENDENT_CODE ON)
