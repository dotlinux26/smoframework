target_sources(smo_contract PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/contract_factory.cpp
)

target_sources(smo_contract INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/contract_factory.hpp
)
