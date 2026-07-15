add_library(smo_transport_framing INTERFACE)
target_include_directories(smo_transport_framing INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_sources(smo_transport PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/framing.cpp
)
