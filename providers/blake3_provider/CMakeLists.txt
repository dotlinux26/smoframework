add_library(smo_blake3_provider STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/blake3_provider.cpp
)
target_include_directories(smo_blake3_provider PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(smo_blake3_provider PUBLIC smo_core smo_blake3)
target_compile_options(smo_blake3_provider PRIVATE -Wall -Wextra)
