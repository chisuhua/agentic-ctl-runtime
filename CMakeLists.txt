cmake_minimum_required(VERSION 3.20)
project(AgenticDSL LANGUAGES CXX)

option(AGENTICDSL_BUILD_TESTS "Build unit tests" OFF)
option(AGENTICDSL_BUILD_EXAMPLES "Build example programs (agent_basic / agent_simple / agent_loop / slice_01 / phase1_*)" OFF)

# 启用需要真实 API key 的 live 测试（默认 OFF，避免 CI/开发环境无密钥时误跑）
# 启用方式: cmake -DAGENTICDSL_ENABLE_LIVE_TESTS=ON
# 安全约束: 严禁提交真实 API key；真实请求会消耗 token 与额度
option(AGENTICDSL_ENABLE_LIVE_TESTS "Enable live tests that hit real LLM APIs (require API keys)" OFF)

# A 阶段：ThreadSanitizer 选项（验证 InMemoryBus 多线程安全）
option(AGENTICDSL_ENABLE_TSAN "Enable ThreadSanitizer (InMemoryBus verification)" OFF)
if(AGENTICDSL_ENABLE_TSAN)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -g")
  set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=thread")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# cpp-httplib (header-only)
set(HTTPLIB_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/external/cpp-httplib")

# nlohmann_json (header-only)
find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp)
if(NOT NLOHMANN_JSON_INCLUDE_DIR)
    set(NLOHMANN_JSON_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/external/nlohmann_json/include")
endif()

# inja (header-only)
find_path(INJA_INCLUDE_DIR inja/inja.hpp)
if(NOT INJA_INCLUDE_DIR)
    set(INJA_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/external/inja/include")
endif()

set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(external/yaml-cpp yaml-cpp-build)
set(YAML_CPP_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/external/yaml-cpp/include")

# Taskflow (header-only, v3.9.0)
# 关闭其内部测试与示例，避免 gtest 等额外依赖与编译时间
set(TF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TF_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(external/taskflow)

# cxxopts vendored target (hydraforge_cxxopts) — header-only, offline, no network
add_subdirectory(external)

# async_simple 已 deprecated (C2 Day 1-2 移除, ADR-0030 V2 决策: std::jthread 替代)
# external/ 不再加载 (async_simple 目录保留 git 历史)
# 原代码: add_subdirectory(external)

# INTERFACE library for common includes and dependencies
add_library(agenticdsl_includes INTERFACE)

target_include_directories(agenticdsl_includes INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/modules>
    $<INSTALL_INTERFACE:include>
    ${NLOHMANN_JSON_INCLUDE_DIR}
    ${INJA_INCLUDE_DIR}
    ${YAML_CPP_INCLUDE_DIR}
    ${HTTPLIB_INCLUDE_DIR}
)

target_link_libraries(agenticdsl_includes INTERFACE
    yaml-cpp::yaml-cpp
)

add_library(agenticdsl_common STATIC
    src/common/llm/cloud_adapter.cpp
    src/common/llm/http_adapter.cpp
    src/common/llm/llama_adapter.cpp
    src/common/llm/llama_adapter_provider.cpp
    src/common/llm/llama_tool.cpp
    src/common/llm/llm_provider_factory.cpp      # P1.T1: LLMProviderFactory 路由
    src/common/llm/mock_provider.cpp
    src/common/llm/mock_provider_factory.cpp    # P1.T1: MockProviderFactory
    src/common/llm/factory.cpp                  # Sprint 6 P2-7
    src/common/llm/sse_stream.cpp
    src/common/llm/stream_to_bus.cpp            # C2 Day 1-2: IGenerationStream → IInteractionBus bridge
    src/common/llm/illmprovider_decorator.cpp   # Phase 5: ILLMProviderDecorator 基类 (REQ-IPD-001)
    src/common/llm/cost_tracking_decorator.cpp  # Phase 5: CostTrackingDecorator 计费 (REQ-IPD-002)
    src/common/llm/compliance_decorator.cpp     # Phase 5: ComplianceDecorator 合规日志 (REQ-IPD-003)
    src/common/llm/rate_limit_decorator.cpp     # Phase 5: RateLimitDecorator 配额限制 (REQ-IPD-004)
    src/common/llm/tracing_decorator.cpp        # Phase 6a: TracingDecorator 事件追踪 (ADR-0068 §决策 3)
    src/common/llm/serializing_decorator.cpp    # Wave 1 #2: SerializingDecorator 修复 CloudLLMAdapter 多线程 SIGSEGV
    src/common/llm/orchestration_illm_provider.cpp  # Phase 5: Dual Consumer Model (REQ-ICC-001)
    src/common/prompts/v1.cpp                  # ADR-0074 D-3: V1 schema constraint builder
    src/common/prompts/v2.cpp                  # ADR-0074 D-3: V2 few-shot 注入 builder
    src/common/prompts/v3.cpp                  # ADR-0074 D-3+D-5: V3 two-stage 注入 builder
    src/common/env/local_backend.cpp          # ADR-0075 D2: LocalBackend fork+execve (C11)
    src/common/env/docker_backend.cpp         # ADR-0075 D3: DockerBackend cpp-httplib REST (C12)
    src/common/env/backend_factory.cpp        # ADR-0075 D1: create_backend 工厂
    src/common/hooks/env_validation_hook.cpp  # ADR-0075 D5: EnvValidationHook pre-hook (C13)
    src/common/tools/registry.cpp
    src/common/tools/command_registry.cpp    # adr-0070: CommandRegistry L1 (DECLARE_COMMAND 治理入口)
    src/common/tools/factory.cpp              # P2.C: ToolRegistry factory (decouple engine.cpp)
    src/common/tools/tool_schema_validator.cpp  # ADR-0073 D3: JSON Schema 2020-12 校验包装层
    src/common/observability/otel_exporter.cpp  # P11: OTel span exporter 骨架 (ADR-0063)
    src/common/utils/parser_utils.cpp
    src/common/utils/template_renderer.cpp
    src/common/utils/yaml_json.cpp
    src/common/utils/timer_service.cpp  # Sprint 28 kernel-timer-service: ITimerService 默认实现 (cv + jthread)
    src/core/types/tool_result.cpp
)
# Sprint 28 microkernel migration: agenticdsl_common now linked into
# PDK .so (temporal_agent etc.) - require PIC for shared object linkage
set_target_properties(agenticdsl_common PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_link_libraries(agenticdsl_common PUBLIC
    agenticdsl_includes
    agenticdsl_policy
    OpenSSL::SSL
    OpenSSL::Crypto
)

# ADR-0074 C3 — prompts 子目录头文件可见性 (等价 src/common/CMakeLists.txt 修改,
# agenticdsl_common 定义于根 CMakeLists, 项目无 src/common/CMakeLists.txt)
target_include_directories(agenticdsl_common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/common/prompts>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/common/observability>  # P11: OTel headers
)

add_subdirectory(src/modules/parser)
add_subdirectory(src/modules/context)
add_subdirectory(src/modules/budget)
add_subdirectory(src/modules/scheduler)
add_subdirectory(src/modules/executor)
add_subdirectory(src/modules/trace)
add_subdirectory(src/modules/library)
add_subdirectory(src/modules/system)
add_subdirectory(src/modules/cognitive)
add_subdirectory(src/modules/prompt)   # Sprint 25 T21: Prompt Evidence Gate (ADR-0074)
add_subdirectory(src/modules/plugin)
add_subdirectory(src/modules/pdk)
add_subdirectory(src/modules/skill_interpreter)
add_subdirectory(src/modules/testing)
add_subdirectory(src/modules/ir)  # Sprint 24 T15: Trajectory IR (ADR-0061-06 v1.1)
add_subdirectory(src/modules/distillation)  # Phase 1: FileDistillationWriter V1 (capture-mode-and-distillation-writer-v1)
add_subdirectory(src/common/contract)
add_subdirectory(src/common/policy)
add_subdirectory(src/common/tools)
add_subdirectory(src/common/governance)
add_subdirectory(src/common/governance/cross_cutting)
add_subdirectory(src/common/runtime)  # ADR-0072 D1 阶段 B: IStreamHandle 参考实现

# Phase 1 公共头文件占位 (W1D2.5 启动前置, 见 .omo/plans/phase1-execution.md §W1D2.5.T1)
# 每个子目录定义为 INTERFACE library, 命名 agenticdsl_hdr_<module> 避免与 src/ 实现库冲突
add_subdirectory(include/agenticdsl/cognitive)
add_subdirectory(include/agenticdsl/contract)
add_subdirectory(include/agenticdsl/pdk)
add_subdirectory(include/agenticdsl/plugin)
add_subdirectory(include/agenticdsl/types)

# Phase 1 Sprint 4 PDK 骨架 (K3 决策: monorepo `pdk/` 子目录, ADR-0021 §2.2)
# hydraforge_pdk INTERFACE 库 (header-only), 静态链接到插件 (P3)
# Phase 2 拆分至独立 hydraforge-pdk 仓库时移除此行, 改 find_package(hydraforge_pdk REQUIRED)
# 注: enable_testing() 在 add_subdirectory(pdk) 前调用, 使 pdk 子目录中的
# add_test() 调用可被 ctest 发现 (pkm-temporal-demo-scaffold Task 1)。
enable_testing()
add_subdirectory(pdk)

add_library(agenticdsl_core STATIC
 src/core/engine.cpp
 src/core/session_manager.cpp  # session-manager-jsonl-v2: 接入生产库 (ADR-0033 存储层)
 src/core/event_log.cpp        # ADR-0080 v1.1: AppendOnlyEventLog 写入器
 src/core/session_writer.cpp   # ADR-0079 v1.1 + P5: SessionWriter (D5 + D6 topic filter)
 src/core/types/session.cpp
src/core/types/session_registry.cpp  # C11 Phase 5 Stage 1
src/core/context_compactor.cpp      # context-compactor Task 1: 上下文压缩器骨架
src/core/agent_registry.cpp         # P7: IAgentRegistry InMemory 参考实现 (ADR-0082 §决策 7)
 src/core/agent_hook_registry.cpp    # P3: IAgentHookRegistry InMemory 参考实现 (ADR-0081)
 src/core/parsed_graph_to_trajectory_ir.cpp  # T15: TrajectoryIR 单向 Converter (ADR-0061-06 v1.1)
)

target_link_libraries(agenticdsl_core
 agenticdsl_includes
 agenticdsl_common
 agenticdsl_runtime  # ADR-0072 D1 阶段 B: IStreamHandle 参考实现
 agenticdsl_modules_parser
 agenticdsl_modules_context
 agenticdsl_modules_budget
 agenticdsl_modules_executor
 agenticdsl_modules_scheduler
 agenticdsl_modules_trace
 agenticdsl_modules_library
 agenticdsl_modules_system
 agenticdsl_modules_cognitive
 agenticdsl_modules_prompt
 agenticdsl_modules_plugin
 agenticdsl_modules_pdk
 agenticdsl_modules_skill_interpreter
 agenticdsl_contract
 agenticdsl_policy
 agenticdsl_governance
 agenticdsl_secure_tools
 Threads::Threads
)

# Alias target for tests
add_library(agenticdsl ALIAS agenticdsl_core)

# --- Examples (opt-in via AGENTICDSL_BUILD_EXAMPLES) ---
# Sprint 19: agent_simple + agent_loop 从审计债修复后纳入 opt-in 套件
if(AGENTICDSL_BUILD_EXAMPLES)
    # --- Sample: agent_basic ---
    add_executable(agent_basic
        examples/agent_basic/main.cpp
    )

    target_link_libraries(agent_basic
        agenticdsl_includes
        agenticdsl_core
    )

    # --- Sample: agent_simple (MockLLMProvider 模式) ---
    add_subdirectory(examples/agent_simple)

    # --- Sample: agent_loop (MockLLMProvider 多轮循环) ---
    add_subdirectory(examples/agent_loop)

    # --- Sample: slice_01_tool_call (Track 0.2 / B) ---
    add_subdirectory(examples/slice_01_tool_call)

    # --- Phase 1 Sprint 0 Plugin Stub examples (K1 决策) ---
    add_subdirectory(examples/phase1_model_router_plugin)
    add_subdirectory(examples/phase1_plugin_demo)

    # --- Phase 5 YIELD/STREAM token generator (C12) ---
    add_subdirectory(examples/phase5_yield_token_generator)

    # --- pdk_chat_demo (Phase A 端到端 Agent-as-Plugin 验证) ---
    # 关联: examples/pdk_chat_demo/DESIGN.md (784 行, 15 节)
    add_subdirectory(examples/pdk_chat_demo)

    # --- pkm_temporal_demo (Temporal workflow demo, Task 4-6) ---
    add_subdirectory(examples/pkm_temporal_demo)
endif()

# Create alias for tests

# --- Tools (C++ CLI, ADR-0074 C3) ---
add_subdirectory(tools)

# --- Tests (Optional) ---
if(AGENTICDSL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================
# Public headers INTERFACE library (Stage 4 / Task 18)
#
# Exposes include/agenticdsl/ as a proper INTERFACE target so
# external projects (examples, plugins) can link against
# `agenticdsl::headers` and get the include path automatically.
# ============================================================
add_library(agenticdsl_headers INTERFACE)
add_library(agenticdsl::headers ALIAS agenticdsl_headers)

target_include_directories(agenticdsl_headers INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_compile_features(agenticdsl_headers INTERFACE cxx_std_20)
