target_sources(smo_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/cbor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bootstrap_snapshot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bootstrap_protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bootstrap_service.cpp
)
target_sources(smo_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/cbor.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bootstrap_snapshot.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bootstrap_protocol.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bootstrap_service.hpp
)
