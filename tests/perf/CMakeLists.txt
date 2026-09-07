# tests/perf/CMakeLists.txt
# 性能基准测试 — EventLogWriter::query() P4
# 3 cases: 1k / 10k / 100k events (性能目标: 10k < 100ms)
# 设计依据: openspec/changes/event-log-query-api (P4)

# catch_amalgamated.hpp + main_test_runner.cpp (沿用 tests/ 主 CMakeLists 模板)
add_executable(test_event_log_query_perf
    test_event_log_query_perf.cpp
    ${CMAKE_SOURCE_DIR}/tests/catch_amalgamated.cpp
    ${CMAKE_SOURCE_DIR}/tests/main_test_runner.cpp
)

target_include_directories(test_event_log_query_perf PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external/nlohmann_json/single_include
)

target_link_libraries(test_event_log_query_perf PRIVATE
    agenticdsl
    agenticdsl_modules_parser
)

target_compile_definitions(test_event_log_query_perf PRIVATE
    CATCH_CONFIG_ENABLE_ALL_STRINGMAKERS=1
    $<$<CONFIG:Release>:HYDRAFORGE_PERF_STRICT>
    $<$<CONFIG:RelWithDebInfo>:HYDRAFORGE_PERF_STRICT>
)

add_test(
    NAME test_event_log_query_perf
    COMMAND test_event_log_query_perf
)
