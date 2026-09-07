# pdk/loop_agent/CMakeLists.txt
# Loop Agent Plugin - 通过 lib/loop/react.agent.md 执行 ReactLoop
# 关联: docs/adr/adr-0021-pdk-design.md

cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


add_library(LoopAgent SHARED
    src/pdk_entry.cpp
    # §4.0.11 chat-async-io-consumer-loop: compile globals directly (avoid non-PIC OBJECT lib link)
    ${PROJECT_SOURCE_DIR}/examples/pdk_chat_demo/commands/cancellation_globals.cpp
)

target_include_directories(LoopAgent PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    # §4.0.11: shared CancellationRegistry globals
    ${PROJECT_SOURCE_DIR}/examples/pdk_chat_demo
    ${PROJECT_SOURCE_DIR}/examples/pdk_chat_demo/commands
    ${PROJECT_SOURCE_DIR}/examples

  ${PROJECT_SOURCE_DIR}/external/nlohmann_json/single_include)

target_link_libraries(LoopAgent PRIVATE
    hydraforge_pdk
)

set_target_properties(LoopAgent PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdk/loop_agent"
)

install(TARGETS LoopAgent LIBRARY DESTINATION lib/pdk/loop_agent)