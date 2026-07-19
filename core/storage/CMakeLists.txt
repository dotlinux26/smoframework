target_sources(smo_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/database.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/migration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sqlite_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/manifest_store.cpp
)

target_sources(smo_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/store_id.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/database.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/migration.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sqlite_store.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/manifest_store.hpp
)

target_link_libraries(smo_core PUBLIC smo_sqlite3)
