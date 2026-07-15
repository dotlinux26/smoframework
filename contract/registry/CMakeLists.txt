target_sources(smo_contract PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/contract_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/native.cpp
)

target_sources(smo_contract INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/contract_registry.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/native.hpp
)
