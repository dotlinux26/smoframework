target_sources(smo_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/error.cpp
)

target_sources(smo_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/error.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/errors.h
)
