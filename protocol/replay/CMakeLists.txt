add_library(smo_protocol_replay INTERFACE)
target_include_directories(smo_protocol_replay INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_sources(smo_protocol PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/replay.cpp
)
