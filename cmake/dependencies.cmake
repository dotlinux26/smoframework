include(FetchContent)

# ── Dependency: fmt (formatting) ─────────────────────────────────────
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        11.0.2
    GIT_SHALLOW    TRUE
)
set(FMT_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(fmt)

# ── Dependency: spdlog (logging) ─────────────────────────────────────
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
    GIT_SHALLOW    TRUE
)
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

# ── Dependency: simdjson (zero-copy JSON) ────────────────────────────
FetchContent_Declare(
    simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG        v3.10.1
    GIT_SHALLOW    TRUE
)
set(SIMDJSON_DEVELOPER_MODE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(simdjson)

# ── Dependency: liboqs (post-quantum cryptography) ───────────────────
FetchContent_Declare(
    liboqs
    GIT_REPOSITORY https://github.com/open-quantum-safe/liboqs.git
    GIT_TAG        0.11.0
    GIT_SHALLOW    TRUE
)
set(OQS_BUILD_ONLY_LIB ON CACHE BOOL "" FORCE)
set(OQS_ENABLE_KEM_KYBER ON CACHE BOOL "" FORCE)
set(OQS_ENABLE_SIG_DILITHIUM ON CACHE BOOL "" FORCE)
set(OQS_ENABLE_SIG_SPHINCS_PLUS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(liboqs)
