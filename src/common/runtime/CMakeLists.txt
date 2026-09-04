# src/common/runtime/CMakeLists.txt
# ADR-0072 D1 阶段 B: IStreamHandle 参考实现运行时模块
# 2 个 IStreamHandle 参考实现: BufferedStreamHandle + CallbackStreamHandle
# 作者: AgenticDSL Phase 6c / 2026-09-04
# 设计依据: openspec/changes/adr-0072-d1-stream-runtime-semantics/

add_library(agenticdsl_runtime STATIC
    stream_handle.cpp
)

target_link_libraries(agenticdsl_runtime PUBLIC
    agenticdsl_includes
)
