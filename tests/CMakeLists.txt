# 启用测试支持
enable_testing()

# Catch2 单头文件路径
set(CATCH_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# 定义创建单个 Catch2 测试的函数
function(add_catch_test TEST_NAME)
    # 参数：测试名称，其余为该测试的源文件（不含 catch_amalgamated.cpp/main_test_runner.cpp）
    set(SOURCES ${ARGN})

    # test_skill_interpreter 需要自定义 main（支持 --skill-child 早期分支）
    if(TEST_NAME STREQUAL "test_skill_interpreter")
        list(APPEND SOURCES
            ${CATCH_INCLUDE_DIR}/catch_amalgamated.cpp
            ${CATCH_INCLUDE_DIR}/main_skill_test_runner.cpp
        )
    else()
        # 自动包含 catch_amalgamated.cpp 和 main_test_runner.cpp
        list(APPEND SOURCES ${CATCH_INCLUDE_DIR}/catch_amalgamated.cpp main_test_runner.cpp)
    endif()

    # 创建可执行文件
    add_executable(${TEST_NAME} ${SOURCES})

    # 包含目录
    target_include_directories(${TEST_NAME} PRIVATE
        ${CATCH_INCLUDE_DIR}
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/external/nlohmann_json/single_include
        ${PROJECT_SOURCE_DIR}/external/inja/include
        ${PROJECT_SOURCE_DIR}/external/yaml-cpp/include
        ${PROJECT_SOURCE_DIR}/external/taskflow
        ${PROJECT_SOURCE_DIR}/pdk/temporal_agent/include
    )

    target_link_libraries(${TEST_NAME} PRIVATE agenticdsl agenticdsl_modules_parser yaml-cpp::yaml-cpp)
    # Plugin 模块 (Sprint 5): dlopen/dlsym 实现的 PluginLoader
    target_link_libraries(${TEST_NAME} PRIVATE agenticdsl_modules_plugin)
    # Sprint 16 (B.5): test_system_nodes 需要 EndNode 定义 (来自 agenticdsl_modules_system)
    target_link_libraries(${TEST_NAME} PRIVATE agenticdsl_modules_system)
    # Sprint 23 (T14): 行为回归测试套件需要 testing 模块
    if(TEST_NAME MATCHES "^test_behavioral_regression")
        target_link_libraries(${TEST_NAME} PRIVATE agenticdsl_modules_testing)
    endif()
    # Sprint 24 (T15): TrajectoryIR 测试需要 ir 模块 (backends + pass)
    if(TEST_NAME STREQUAL "test_trajectory_ir")
        target_link_libraries(${TEST_NAME} PRIVATE agenticdsl_modules_ir)
    endif()
    # Phase 1 (capture-mode-and-distillation-writer-v1): FileDistillationWriter 测试需要 distillation 模块
    if(TEST_NAME STREQUAL "test_distillation_writer")
        target_link_libraries(${TEST_NAME} PRIVATE agenticdsl_distillation)
    endif()
    # Phase 2 (capture-mode-and-distillation-writer-v1): TrajectoryIR bridge 测试需要 distillation 模块
    if(TEST_NAME STREQUAL "test_trajectory_bridge")
        target_link_libraries(${TEST_NAME} PRIVATE agenticdsl_distillation)
    endif()
    target_compile_definitions(${TEST_NAME} PRIVATE CATCH_CONFIG_ENABLE_ALL_STRINGMAKERS=1)

    # temporal_agent 测试需要直接链接 plugin 源码 (pdk_register_tools + pdk_plugin_info)
    if(TEST_NAME MATCHES "^test_temporal_agent")
        target_sources(${TEST_NAME} PRIVATE
            ${PROJECT_SOURCE_DIR}/pdk/temporal_agent/src/pdk_entry.cpp
            ${PROJECT_SOURCE_DIR}/pdk/temporal_agent/src/temporal_client.cpp
            ${PROJECT_SOURCE_DIR}/pdk/temporal_agent/src/temporal_client_pool.cpp
            ${PROJECT_SOURCE_DIR}/pdk/temporal_agent/src/workflow_callback_channel.cpp
            ${PROJECT_SOURCE_DIR}/pdk/temporal_agent/src/namespace_manager.cpp
        )
        target_include_directories(${TEST_NAME} PRIVATE
            ${PROJECT_SOURCE_DIR}/pdk/temporal_agent/src
        )
        target_link_libraries(${TEST_NAME} PRIVATE dl)
    endif()

    # session_agent 测试需要直接链接 plugin 源码 (ADR-0079 v1.1 append-only 修复验证)
    if(TEST_NAME MATCHES "^test_session_store|^test_session_node_id_addressing|^test_session_extract_fork|^test_session_branch_cursor")
        target_sources(${TEST_NAME} PRIVATE
            ${PROJECT_SOURCE_DIR}/pdk/session_agent/src/session_store.cpp
        )
        target_include_directories(${TEST_NAME} PRIVATE
            ${PROJECT_SOURCE_DIR}/pdk/session_agent/include
        )
    endif()

    # EventLog 测试 (ADR-0080 v1.1)
    if(TEST_NAME MATCHES "^test_event_log")
        target_include_directories(${TEST_NAME} PRIVATE
            ${PROJECT_SOURCE_DIR}/src/core
        )
    endif()

    if(TEST_NAME MATCHES "^test_grpc_temporal_backend")
        target_sources(${TEST_NAME} PRIVATE
            ${PROJECT_SOURCE_DIR}/pdk/temporal_agent/src/temporal_client.cpp
            ${PROJECT_SOURCE_DIR}/pdk/temporal_agent/src/grpc_temporal_backend.cpp
        )
        target_include_directories(${TEST_NAME} PRIVATE
            ${PROJECT_SOURCE_DIR}/pdk/temporal_agent/include
            ${PROJECT_SOURCE_DIR}/pdk/temporal_agent/src
        )
    endif()

    if(TEST_NAME MATCHES "^test_pdk_register_agent")
        target_sources(${TEST_NAME} PRIVATE
            ${PROJECT_SOURCE_DIR}/pdk/temporal_agent/src/pdk_entry.cpp
            ${PROJECT_SOURCE_DIR}/pdk/temporal_agent/src/temporal_client.cpp
        )
        target_include_directories(${TEST_NAME} PRIVATE
            ${PROJECT_SOURCE_DIR}/pdk/temporal_agent/include
            ${PROJECT_SOURCE_DIR}/pdk/temporal_agent/src
        )
    endif()

    if(TEST_NAME MATCHES "^test_provider_(refresh|register_dynamic|switch|dynamic_lifecycle|dynamic_integration|factory_concurrent)")
        set(PROVIDER_AGENT_TEST_SOURCES
            ${PROJECT_SOURCE_DIR}/pdk/provider_agent/src/credential_store.cpp
            ${PROJECT_SOURCE_DIR}/pdk/provider_agent/src/pdk_entry.cpp
            ${PROJECT_SOURCE_DIR}/pdk/provider_agent/src/provider_resolve.cpp
            ${PROJECT_SOURCE_DIR}/pdk/provider_agent/src/provider_refresh.cpp
        )
        if(EXISTS ${PROJECT_SOURCE_DIR}/pdk/provider_agent/src/provider_register_dynamic.cpp)
            list(APPEND PROVIDER_AGENT_TEST_SOURCES ${PROJECT_SOURCE_DIR}/pdk/provider_agent/src/provider_register_dynamic.cpp)
        endif()
        if(EXISTS ${PROJECT_SOURCE_DIR}/pdk/provider_agent/src/provider_switch.cpp)
            list(APPEND PROVIDER_AGENT_TEST_SOURCES ${PROJECT_SOURCE_DIR}/pdk/provider_agent/src/provider_switch.cpp)
        endif()
        target_sources(${TEST_NAME} PRIVATE ${PROVIDER_AGENT_TEST_SOURCES})
        target_include_directories(${TEST_NAME} PRIVATE
            ${PROJECT_SOURCE_DIR}/pdk/provider_agent/include
            ${PROJECT_SOURCE_DIR}/pdk/provider_agent/src
        )
        target_link_libraries(${TEST_NAME} PRIVATE dl)
    endif()

    # 注册为 CTest 测试
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})

    # test_cloud_llm_live 需真实 API key，标记为 live 标签，CI 默认不跑
    if(TEST_NAME STREQUAL "test_cloud_llm_live")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS "live")
        # 当 AGENTICDSL_ENABLE_LIVE_TESTS=ON 时，启用真实 API 调用测试
        # 默认 OFF → 编译 disabled 占位 case (no-op)，保证文件仍被识别为有效 Catch2 测试源
        if(AGENTICDSL_ENABLE_LIVE_TESTS)
            target_compile_definitions(${TEST_NAME} PRIVATE AGENTICDSL_ENABLE_LIVE_TESTS)
            message(STATUS "[live-tests] ${TEST_NAME} enabled — will hit real LLM APIs")
        endif()
    endif()

    # Real LLM tests need longer timeout (DeepSeek API call 10-30s, retries multiply).
    # Per-test TIMEOUT overrides `ctest --timeout N` so user can keep default 20s
    # without killing slow network tests. 60s covers 3-retry plan_execute_loop worst case.
    # Affected: 2026-09-09 20s-timeout audit — 8 tests timeout, 2 flakes (Failed not Timeout).
    set(_REALLLM_TIMEOUT_TESTS
        test_cognitive_worker
        test_session_writer_eventlog_integration
    )
    list(FIND _REALLLM_TIMEOUT_TESTS "${TEST_NAME}" _idx)
    if(_idx GREATER -1)
        set_tests_properties(${TEST_NAME} PROPERTIES TIMEOUT 60)
    endif()

    # test_plan_execute_realllm: 3 TEST_CASE × (plan_phase + execute_phase + verify_phase)
    # × retry up to 3 times = worst case 12 deepseek API calls.
    # DeepSeek API 单次 10-30s, 含 reasoning 模式 → 需 180s+
    if(TEST_NAME STREQUAL "test_plan_execute_realllm")
        set_tests_properties(${TEST_NAME} PROPERTIES TIMEOUT 180)
    endif()

    # Real LLM stress test: 8 workers × 20 deepseek tasks = 160 API calls
    # SerializingDecorator 串行化 → 需 300s+ (单次 API 1-2s × 160 = 160-320s + 重试开销)
    if(TEST_NAME STREQUAL "test_cloud_adapter_multithread")
        set_tests_properties(${TEST_NAME} PROPERTIES TIMEOUT 300)
    endif()

    # test_async_bridge 仅链接 Taskflow (C2 Day 1-2 移除 async_simple, ADR-0030 V2 决策)
    if(TEST_NAME STREQUAL "test_async_bridge")
        target_link_libraries(${TEST_NAME} PRIVATE Taskflow)
    endif()

    # test_skill_interpreter 需要 CATCH_AMALGAMATED_CUSTOM_MAIN
    # 自定义 main() 支持 --skill-child 早期分支
    if(TEST_NAME STREQUAL "test_skill_interpreter")
        target_compile_definitions(${TEST_NAME} PRIVATE CATCH_AMALGAMATED_CUSTOM_MAIN)
    endif()

    # budget_agent hook 集成测试需要 .so 路径 (dlopen)
    if(TEST_NAME STREQUAL "test_budget_agent_hooks")
        target_compile_definitions(${TEST_NAME} PRIVATE
            TEST_BUDGET_AGENT_SO_PATH="${CMAKE_BINARY_DIR}/pdk/budget_agent/libBudgetAgent.so")
        target_link_libraries(${TEST_NAME} PRIVATE dl)
    endif()

    # session-manager-jsonl-v2 (2026-08-05): session_manager.cpp 已在 agenticdsl_core 生产库,
    # 4 个 test target 通过 link agenticdsl_core 自动获得, 无需单独 target_sources.
endfunction()

# ================================
# 方案一：为每个 test_xxx.cpp 创建独立测试（推荐）
# ================================

file(GLOB SINGLE_TEST_SOURCES "test_*.cpp")
# 列出所有独立的测试源文件（每个文件一个测试）
#set(SINGLE_TEST_SOURCES
#    test_basic.cpp
#    test_engine.cpp
#    test_parser.cpp
#    test_scheduler.cpp
#)

# 为每个源文件创建一个独立的测试
foreach(TEST_SRC ${SINGLE_TEST_SOURCES})
    # 提取测试名，如 test_reg.cpp -> test_reg
    get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
    # real-llm-core-coverage Phase 0: sibling (chat-real-llm-coverage) 已在
    # examples/pdk_chat_demo/tests 注册同名 target → core 树副本改名避免 CMP0002 冲突
    if(TEST_NAME STREQUAL "test_real_llm_env_helper")
        set(TEST_NAME "test_real_llm_env_helper_core")
    endif()
    add_catch_test(${TEST_NAME} ${TEST_SRC})
endforeach()

# P4 perf 基准测试子目录
add_subdirectory(perf)

# ADR-0074 C1 — few-shot 数据测试使用相对路径 lib/prompts/fewshot, 需以仓库根为工作目录
if(TEST test_few_shot_examples)
    set_tests_properties(test_few_shot_examples PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()

# ADR-0074 C2 — golden suite 测试使用相对路径 lib/prompts/golden, 需以仓库根为工作目录
if(TEST test_golden_suite)
    set_tests_properties(test_golden_suite PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()

# ADR-0074 C3 — V1/V2/V3 prompt 测试经 V2/V3 相对路径读取 lib/prompts/fewshot,
# 需以仓库根为工作目录
if(TEST test_prompt_v1_v2_v3)
    set_tests_properties(test_prompt_v1_v2_v3 PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()

# ADR-0074 C3 — measure_prompt_baseline 测试: 注入 CLI 实际路径 + 相对 golden dir
# 需以仓库根为工作目录 (CLI 默认 --golden-dir lib/prompts/golden)
if(TEST test_measure_prompt_baseline)
    target_compile_definitions(test_measure_prompt_baseline PRIVATE
        TEST_MEASURE_CLI_PATH="$<TARGET_FILE:measure_prompt_baseline>")
    set_tests_properties(test_measure_prompt_baseline PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()

# phase-7a-baseline-retest-v2 — baseline retest 脚本测试 spawn 相对路径
# scripts/evidence-gate-v1.sh, 需以仓库根为工作目录
if(TEST test_baseline_retest_scripts)
    set_tests_properties(test_baseline_retest_scripts PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()

# T21 — test_prompt_evidence_gate 需以仓库根为工作目录
# (lib/prompt/few_shots + lib/prompt/golden 相对路径 + python 工具调用)
if(TEST test_prompt_evidence_gate)
    set_tests_properties(test_prompt_evidence_gate PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()

# ─────────────────────────────────────────────────────────────
# C6+C7 (from-roadmap-phase-6c-execution-dsl): dual_syntax_lint 独立可执行工具
# C7 lint 工具: 检测 legacy 语法 → warning + 行级豁免 + 新文件 heuristic
# 供 test_dsl_extensions 通过 TEST_DUAL_SYNTAX_LINT_PATH 调用
# ─────────────────────────────────────────────────────────────
add_executable(dual_syntax_lint
    ${PROJECT_SOURCE_DIR}/src/modules/parser/dual_syntax_lint.cpp
)
target_include_directories(dual_syntax_lint PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include
)

# test_dsl_extensions 注入 lint 二进制路径 (E2E 调用)
if(TEST test_dsl_extensions)
    target_compile_definitions(test_dsl_extensions PRIVATE
        TEST_DUAL_SYNTAX_LINT_PATH="$<TARGET_FILE:dual_syntax_lint>")
endif()

# ─────────────────────────────────────────────────────────────
# Sprint 7 Day 3 quick win (Oracle Top 2 - 推迟自 Day 2 spot-check):
# 静态 grep 锁定 fork 状态契约, 防止 Day 1 fork dedup 回归。
# 详见: tests/test_fork_static_contracts.sh
# 关联 Oracle: ses_111741590ffeF6HMxhgYq7YKrr
# 关联 spec: openspec/changes/sprint-7-tech-debt-followup/specs/sprint-7-tech-debt-followup/spec.md
# ─────────────────────────────────────────────────────────────
add_test(NAME test_fork_static_contracts
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_fork_static_contracts.sh
         WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

