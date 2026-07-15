add_library(smo_storage_session_store INTERFACE)
target_include_directories(smo_storage_session_store INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_sources(smo_storage PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/session_store.cpp
)
