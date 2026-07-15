add_library(smo_blake3 STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/blake3.c
    ${CMAKE_CURRENT_SOURCE_DIR}/blake3_dispatch.c
    ${CMAKE_CURRENT_SOURCE_DIR}/blake3_portable.c
    ${CMAKE_CURRENT_SOURCE_DIR}/blake3_sse2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/blake3_sse41.c
    ${CMAKE_CURRENT_SOURCE_DIR}/blake3_avx2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/blake3_avx512.c
)
target_include_directories(smo_blake3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(smo_blake3 PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
    target_sources(smo_blake3 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/blake3_neon.c
    )
endif()

# SIMD assembly for x86_64
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_sources(smo_blake3 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/blake3_sse2_x86-64_unix.S
        ${CMAKE_CURRENT_SOURCE_DIR}/blake3_sse41_x86-64_unix.S
        ${CMAKE_CURRENT_SOURCE_DIR}/blake3_avx2_x86-64_unix.S
        ${CMAKE_CURRENT_SOURCE_DIR}/blake3_avx512_x86-64_unix.S
    )
    target_compile_options(smo_blake3 PRIVATE
        -msse2 -msse4.1 -mavx2
    )
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/blake3_avx512.c
        PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512vl"
    )
endif()
