# Network Layer — connectivity, bootstrap, topology management
# Sprint 4: UDP transport, heartbeat, bootstrap integration
# See ARCHITECTURE.md §Connectivity Layer for design rationale.

# UDP transport + heartbeat + gossip + sync are part of smo_core
target_sources(smo_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/udp/udp_transport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/udp/heartbeat_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sync/membership_sync.cpp
)

target_sources(smo_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/udp/udp_transport.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/udp/heartbeat_service.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp/listener.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp/connector.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp/session.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/udp/heartbeat.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/udp/discovery.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/udp/gossip.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bootstrap/seed_resolver.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bootstrap/peer_cache.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/transport/selector.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/transport/dispatcher.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sync/membership_sync.hpp
)
