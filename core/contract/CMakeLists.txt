target_sources(smo_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/contract_id.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/contract_definition.cpp
)

target_sources(smo_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/contract_id.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/contract_definition.hpp
)
