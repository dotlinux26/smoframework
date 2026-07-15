target_sources(smo_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/identity.cpp
)

target_sources(smo_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/identity.hpp
)
