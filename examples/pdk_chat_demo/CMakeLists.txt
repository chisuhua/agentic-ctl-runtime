# examples/pdk_chat_demo/CMakeLists.txt
# PDK Chat Demo - Agent-as-Plugin 架构端到端验证
# 关联: docs/examples/pdk_chat_demo/DESIGN.md
#      docs/adr/adr-0052 ~ adr-0065

# --- Source files (shared between demo and tests) ---
set(SOURCES
    chat_session.cpp
    event_handler.cpp
    dsl_validator.cpp
    cli_args_parser.cpp
    cancellation_registry.cpp
    commands/command_globals.cpp
    commands/cancellation_globals.cpp
    commands/help_command.cpp
    commands/compact_command.cpp
    commands/model_command.cpp
    commands/tree_command.cpp
    commands/fork_command.cpp
    commands/clone_command.cpp
    commands/cancel_command.cpp
    tools/provider_switch_stub.cpp
    tools/session_fork.cpp
    tools/session_clone.cpp
    tui/tree_renderer.cpp
)

set(HEADERS
    chat_session.h
    event_handler.h
    dsl_validator.h
    cli_options.h
    cli_args_parser.h
    commands/command_globals.h
    commands/cancellation_globals.h
    commands/help_command.h
    commands/compact_command.h
    commands/model_command.h
    commands/tree_command.h
    commands/fork_command.h
    commands/clone_command.h
    commands/cancel_command.h
    tools/provider_switch_stub.h
    tools/session_fork.h
    tools/session_clone.h
    tui/tree_renderer.h
)

# --- OBJECT library (shared between demo and tests) ---
add_library(pdk_chat_demo_obj OBJECT ${SOURCES} ${HEADERS})

target_include_directories(pdk_chat_demo_obj PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/commands
    ${CMAKE_CURRENT_SOURCE_DIR}/tools
    ${CMAKE_CURRENT_SOURCE_DIR}/tui
)
target_link_libraries(pdk_chat_demo_obj PUBLIC
    agenticdsl_includes
    hydraforge_pdk
    hydraforge_cxxopts
    Threads::Threads
)

# --- Main executable ---
add_executable(pdk_chat_demo main.cpp)

target_include_directories(pdk_chat_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/commands
    ${CMAKE_CURRENT_SOURCE_DIR}/tools
    ${CMAKE_CURRENT_SOURCE_DIR}/tui
)
target_link_libraries(pdk_chat_demo PRIVATE
    pdk_chat_demo_obj
    agenticdsl_includes
    agenticdsl_core
    hydraforge_pdk
    Threads::Threads
)

# 使 dlopen 的 .so 插件能解析宿主二进制中的 agenticdsl_core 符号
target_link_options(pdk_chat_demo PRIVATE -Wl,--export-dynamic)

# T2: DslValidator 集成需要项目源码根路径定位 lib/loop/*.agent.md
target_compile_definitions(pdk_chat_demo PRIVATE
    AGENTICDSL_PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

# --- Configure config.json with build directory paths ---
set(SKILL_CODE_REVIEW_PATH "${PROJECT_SOURCE_DIR}/skills/code-review")
set(SKILL_CODE_REVIEW_RUN_PATH "${CMAKE_CURRENT_SOURCE_DIR}/skills")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.json
    ${CMAKE_CURRENT_BINARY_DIR}/config.json
    @ONLY
)

# --- Tests ---
option(PDK_CHAT_BUILD_TESTS "Build pdk_chat_demo tests" ON)
if(PDK_CHAT_BUILD_TESTS)
    # Catch2 amalgamated (same as root tests)
    set(CATCH_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/tests")
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Install ---
install(TARGETS pdk_chat_demo RUNTIME DESTINATION bin)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/config.json DESTINATION share/pdk_chat_demo)