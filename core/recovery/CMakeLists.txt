target_sources(smo_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/recovery_engine.cpp
)

target_sources(smo_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/recovery_engine.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/crl.hpp
)
