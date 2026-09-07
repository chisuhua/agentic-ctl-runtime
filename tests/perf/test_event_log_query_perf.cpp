// tests/perf/test_event_log_query_perf.cpp
// 功能描述：EventLogWriter::query() 性能基准 (P4 event-log-query-api)
//          ≥3 cases: 1k / 10k / 100k events
//          性能目标: 10k events query < 100ms
// 设计依据：openspec/changes/event-log-query-api (P4)
// 作者：HydraForge Sprint 22 P4 ship
// 最后修改日期：2026-08-20

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#include "catch_amalgamated.hpp"

#include "agenticdsl/contract/bus_event.h"
#include "agenticdsl/contract/event_builder.h"
#include "core/event_log.h"
#include "core/types/event_log_config.h"
#include "test_helpers/mock_bus.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

namespace fs = std::filesystem;
using agenticdsl::BusEvent;
using agenticdsl::EventLogConfig;
using agenticdsl::EventLogWriter;

namespace {

fs::path make_temp_dir() {
  static std::atomic<uint64_t> counter{0};
  const auto n = counter.fetch_add(1);
  std::ostringstream oss;
  oss << "event_log_perf_" << ::getpid() << "_" << n;
  auto dir = fs::temp_directory_path() / oss.str();
  fs::create_directories(dir);
  return dir;
}

BusEvent make_event(const std::string& topic, uint64_t causal_time) {
  BusEvent e;
  e.topic = topic;
  e.causal_time = causal_time;
  e.payload.ok = true;
  return e;
}

void seed_events(EventLogWriter& writer,
                 std::shared_ptr<agenticdsl::test::MockBus>& bus,
                 size_t count) {
  for (size_t i = 0; i < count; ++i) {
    const std::string topic = (i % 3 == 0) ? "llm.request"
                              : (i % 3 == 1) ? "tool.call"
                                               : "llm.response";
    bus->emit(make_event(topic, i * 10));
  }
  writer.stop();
}

}  // namespace

// HYDRAFORGE_PERF_STRICT 由 CMake 在 Release/RelWithDebInfo build 时定义;
// 未定义 (Debug build) 时放宽阈值, 避免 nlohmann::json::parse + system load 导致 flaky 失败
#ifndef HYDRAFORGE_PERF_STRICT
#define HYDRAFORGE_PERF_STRICT 0
#endif

TEST_CASE("EventLogWriter::query perf @ 1k events", "[perf][event_log][1k]") {
  auto dir = make_temp_dir();
  auto bus = std::make_shared<agenticdsl::test::MockBus>();
  EventLogConfig cfg;
  cfg.event_log_enabled = true;
  cfg.event_log_agent_id = "perf_1k";
  cfg.event_log_dir = dir;
  cfg.flush_interval = std::chrono::milliseconds(60000);
  EventLogWriter writer(cfg, bus);

  seed_events(writer, bus, 1000);

  EventLogWriter::QueryFilter filter;
  filter.topic_glob = "llm.*";
  filter.start_causal_time = 0;
  filter.end_causal_time = UINT64_MAX;

  auto start = std::chrono::steady_clock::now();
  auto result = writer.query("perf_1k", filter, 10000);
  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start).count();

  REQUIRE(result.size() >= 600);
  // Debug build 受 nlohmann::json::parse + system load 影响放宽阈值
  REQUIRE(elapsed_ms < (HYDRAFORGE_PERF_STRICT ? 50 : 250));
  fs::remove_all(dir);
}

TEST_CASE("EventLogWriter::query perf @ 10k events", "[perf][event_log][10k]") {
  auto dir = make_temp_dir();
  auto bus = std::make_shared<agenticdsl::test::MockBus>();
  EventLogConfig cfg;
  cfg.event_log_enabled = true;
  cfg.event_log_agent_id = "perf_10k";
  cfg.event_log_dir = dir;
  cfg.flush_interval = std::chrono::milliseconds(60000);
  EventLogWriter writer(cfg, bus);

  seed_events(writer, bus, 10000);

  EventLogWriter::QueryFilter filter;
  filter.topic_glob = "llm.*";
  filter.start_causal_time = 0;
  filter.end_causal_time = UINT64_MAX;

  auto start = std::chrono::steady_clock::now();
  auto result = writer.query("perf_10k", filter, 100000);
  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start).count();

  REQUIRE(result.size() >= 6000);
  REQUIRE(elapsed_ms < (HYDRAFORGE_PERF_STRICT ? 100 : 800));
  fs::remove_all(dir);
}

TEST_CASE("EventLogWriter::query perf @ 100k events", "[perf][event_log][100k]") {
  auto dir = make_temp_dir();
  auto bus = std::make_shared<agenticdsl::test::MockBus>();
  EventLogConfig cfg;
  cfg.event_log_enabled = true;
  cfg.event_log_agent_id = "perf_100k";
  cfg.event_log_dir = dir;
  cfg.flush_interval = std::chrono::milliseconds(60000);
  EventLogWriter writer(cfg, bus);

  seed_events(writer, bus, 100000);

  EventLogWriter::QueryFilter filter;
  filter.topic_glob = "llm.*";
  filter.start_causal_time = 0;
  filter.end_causal_time = UINT64_MAX;

  auto start = std::chrono::steady_clock::now();
  auto result = writer.query("perf_100k", filter, 1000000);
  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start).count();

  REQUIRE(result.size() >= 60000);
  REQUIRE(elapsed_ms < (HYDRAFORGE_PERF_STRICT ? 2000 : 8000));
  fs::remove_all(dir);
}
