target_sources(smo_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/transport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/framing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp_transport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/secure_session.cpp
)

target_sources(smo_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/transport.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/framing.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp_transport.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/secure_session.hpp
)
