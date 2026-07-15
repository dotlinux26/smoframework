add_library(smo_contract STATIC)
target_include_directories(smo_contract PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(smo_contract PUBLIC
    smo_core
    smo_storage
    smo_blake3_provider
)

add_subdirectory(registry)
add_subdirectory(factory)
