add_library(smo_protocol_encryption INTERFACE)
target_include_directories(smo_protocol_encryption INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_sources(smo_protocol PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/encryption.cpp
)
