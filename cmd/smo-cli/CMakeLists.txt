add_executable(smo-cli main.cpp cli_context.cpp intent_parser.cpp ${CMAKE_SOURCE_DIR}/third_party/readline/readline.cpp)
target_include_directories(smo-cli PRIVATE ${CMAKE_SOURCE_DIR}/third_party/readline)
target_link_libraries(smo-cli PRIVATE smo_sdk smo_genesis smo_tooling smo_suite1_classical smo_suite3_purepqc)
smo_set_warnings(smo-cli)
smo_set_optimisation(smo-cli)
smo_link_dependencies(smo-cli)

# Suppress unused-result warning for system() call and unused functions
target_compile_options(smo-cli PRIVATE -Wno-unused-result -Wno-unused-function -Wno-error)
