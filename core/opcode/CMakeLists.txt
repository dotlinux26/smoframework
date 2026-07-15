target_sources(smo_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/opcode_registry.cpp
)

target_sources(smo_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/opcode.h
    ${CMAKE_CURRENT_SOURCE_DIR}/opcode_registry.hpp
)
