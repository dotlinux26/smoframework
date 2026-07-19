target_sources(smo_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gossip.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/peer_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/seed_store.cpp
)

target_sources(smo_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gossip.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/peer_store.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/seed_store.hpp
)
