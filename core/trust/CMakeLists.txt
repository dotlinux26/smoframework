target_sources(smo_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/trust.cpp
)

target_sources(smo_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/trust.hpp
)
