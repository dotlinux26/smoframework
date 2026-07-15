target_sources(smo_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery.cpp
)

target_sources(smo_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery.hpp
)
