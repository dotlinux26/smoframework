add_library(smo_protocol STATIC
    packet/packet.h
    schema/schema.h
    signing/signing.h
    encryption/encryption.h
    replay/replay.h
)
target_include_directories(smo_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_link_libraries(smo_protocol PUBLIC smo_core simdjson::simdjson)
smo_set_warnings(smo_protocol)
smo_set_optimisation(smo_protocol)

add_subdirectory(packet)
add_subdirectory(schema)
add_subdirectory(signing)
add_subdirectory(encryption)
add_subdirectory(replay)
