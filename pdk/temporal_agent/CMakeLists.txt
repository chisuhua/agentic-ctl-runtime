# pdk/temporal_agent/CMakeLists.txt
# 功能描述：Temporal Agent PDK Plugin 构建配置 (pkm-temporal-demo-scaffold)
#          Task 1: tests/ 子目录 (ITemporalClient 接口测试)
#          Task 2: mock_client 状态机 (测试直接编译, 非 .so)
#          Task 3: pdk_entry.cpp 5 工具注册 -> libpdk_temporal_agent.so
# 设计依据：.rddf/plans/pkm-temporal-demo-scaffold.md Task 1-3
#           参考范式: pdk/g3_knowledge_base/CMakeLists.txt (Phase 6 W1)
# 作者：pkm-temporal-demo-scaffold Task 1-3
# 最后修改日期：2026-07-28

# ============================================================================
# Task 3: 共享库目标 libpdk_temporal_agent.so
# ============================================================================
add_library(pdk_temporal_agent SHARED
    src/pdk_entry.cpp
    src/temporal_client.cpp
    src/temporal_client_pool.cpp
    src/namespace_manager.cpp
    src/workflow_callback_channel.cpp
)

target_include_directories(pdk_temporal_agent PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include   # temporal_client.h, grpc_temporal_backend.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src       # pdk_entry.h, temporal_client_pool.h
    ${PROJECT_SOURCE_DIR}/external/nlohmann_json/single_include
)

target_compile_features(pdk_temporal_agent PRIVATE cxx_std_20)

# 链接 PDK INTERFACE 头文件库 (ITemporalClient / tool_macros / plugin_info)
target_link_libraries(pdk_temporal_agent PRIVATE
    hydraforge_pdk
    agenticdsl_common  # Sprint 28 microkernel: TimerService (cv + jthread 实现)
)

set_target_properties(pdk_temporal_agent PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "pdk_temporal_agent"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdk/temporal_agent"
)

# 测试在 tests/CMakeLists.txt 中托管 (test_temporal_agent_*.cpp)
