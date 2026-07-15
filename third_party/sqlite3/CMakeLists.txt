add_library(smo_sqlite3 STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3.c
)
target_include_directories(smo_sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# SQLite3 compile flags
target_compile_definitions(smo_sqlite3 PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_JSON1
)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(smo_sqlite3 PRIVATE SQLITE_OMIT_LOAD_EXTENSION)
endif()
