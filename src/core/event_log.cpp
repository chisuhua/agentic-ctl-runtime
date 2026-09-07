// src/core/event_log.cpp
// 功能描述：EventLogWriter 实现（ADR-0080 v1.1 D5 + D12 + D2）
// 设计依据：ADR-0080 v1.1 amendment
// 最后修改日期：2026-08-12

#include "core/event_log.h"

#include "agenticdsl/contract/bus_event.h"
#include "agenticdsl/contract/event_builder.h"
#include "agenticdsl/contract/iinteraction_bus.h"
#include "agenticdsl/types/capture_mode.h"

#include <iostream>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace agenticdsl {

namespace {

std::int64_t now_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string next_event_id() {
  static std::atomic<std::uint64_t> counter{0};
  auto ts = now_unix_ms();
  return "evt-" + std::to_string(ts) + "-" + std::to_string(counter.fetch_add(1));
}

// Training 三重保护校验
void validate_training_mode(const EventLogConfig& cfg, const std::string& output_path) {
  if (cfg.capture_mode != CaptureMode::Training) return;

  // 保护 #2: 路径必须包含 "train" 或 "distill" 子串
  if (output_path.find("train") == std::string::npos &&
      output_path.find("distill") == std::string::npos) {
    throw std::invalid_argument(
        "EventLog Training mode requires output path containing 'train' or 'distill'");
  }

  // 保护 #3: WARNING 记录（v1.1 emit WARNING 事件）
  std::cerr << "[WARNING] EventLogConfig in Training mode — PII capture risk. "
            << "Ensure agent_id is set and output path: " << output_path << std::endl;
}

// Online → Training 降级检测
bool detect_online_to_training_downgrade(const EventLogConfig& new_cfg,
                                          CaptureMode previous_mode) {
  return previous_mode == CaptureMode::Online &&
         new_cfg.capture_mode == CaptureMode::Training;
}

}  // anonymous namespace

EventLogWriter::EventLogWriter(EventLogConfig config,
                                 std::shared_ptr<IInteractionBus> bus)
    : config_(std::move(config)), bus_(std::move(bus)) {
  std::error_code ec;
  std::filesystem::create_directories(config_.event_log_dir, ec);
  log_path_ = config_.event_log_dir / (config_.event_log_agent_id + ".v1.jsonl");
  file_.open(log_path_, std::ios::out | std::ios::app);
  if (!file_.is_open()) {
    // 文件无法打开不影响 bus 回调（事件仍入队，flush 时丢弃）
  } else {
    // 记录当前文件大小
    file_.seekp(0, std::ios::end);
    current_file_size_ =
 static_cast<std::uint64_t>(file_.tellp());
    file_.seekp(0, std::ios::end);
  }

  // Training 模式启动校验
  validate_training_mode(config_, log_path_.string());

  // Online → Training 降级检测 + 审计事件发射
  static CaptureMode s_previous_mode = CaptureMode::Off;  // V1: 全局共享，V2: per-instance
  if (detect_online_to_training_downgrade(config_, s_previous_mode)) {
    if (bus_) {
      // 构造 BusEvent + emit event_log.capture_mode_downgrade
      nlohmann::json payload = {
          {"previous_mode", to_string(CaptureMode::Online)},
          {"new_mode", to_string(CaptureMode::Training)},
          {"reason", "Runtime capture mode downgrade detected"}
      };
      bus_->emit(EventBuilder("event_log.capture_mode_downgrade")
                     .args(payload)
                     .build());
    }
  }
  s_previous_mode = config_.capture_mode;

  flush_thread_ = std::thread(&EventLogWriter::flush_loop, this);
  if (bus_) {
    subscribe_to_bus();
  }
}

EventLogWriter::~EventLogWriter() { stop(); }

void EventLogWriter::stop() {
  if (!running_.exchange(false)) return;
  buffer_cv_.notify_all();
  if (flush_thread_.joinable()) {
    flush_thread_.join();
  }
  flush_sync();
  if (file_.is_open()) {
    file_.flush();
    file_.close();
  }
}

void EventLogWriter::flush_sync() {
  std::vector<BusEvent> snapshot;
  {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    snapshot.reserve(buffer_.size());
    while (!buffer_.empty()) {
      snapshot.push_back(std::move(buffer_.front()));
      buffer_.pop();
    }
  }
  if (!file_.is_open() || snapshot.empty()) return;
  for (const auto& e : snapshot) {
    auto line = serialize(e);
    file_ << line << "\n";
    current_file_size_ += line.size() + 1;
    rotate_if_needed();
  }
  file_.flush();
}

void EventLogWriter::subscribe_to_bus() {
  if (!bus_) return;
  bus_->subscribe("*", [this](const BusEvent& event) {
    on_bus_event(event);
  });
}

void EventLogWriter::on_bus_event(const BusEvent& event) {
  {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    buffer_.push(event);
  }
  buffer_cv_.notify_one();
}

void EventLogWriter::flush_loop() {
  while (running_.load()) {
    std::vector<BusEvent> snapshot;
    {
      std::unique_lock<std::mutex> lock(buffer_mutex_);
      buffer_cv_.wait_for(lock, config_.flush_interval,
                           [this] { return !buffer_.empty() || !running_.load(); });
      snapshot.reserve(buffer_.size());
      while (!buffer_.empty()) {
        snapshot.push_back(std::move(buffer_.front()));
        buffer_.pop();
      }
    }
    if (!file_.is_open() || snapshot.empty()) continue;
    for (const auto& e : snapshot) {
      auto line = serialize(e);
      file_ << line << "\n";
      current_file_size_ += line.size() + 1;
      rotate_if_needed();
    }
    file_.flush();
  }
}

std::string EventLogWriter::serialize(const BusEvent& event) const {
  nlohmann::json j;
  j["v"] = 1;
  j["event_id"] = next_event_id();
  j["ts_wall"] = now_unix_ms();
  j["causal_time"] = event.causal_time;
  j["topic"] = event.topic;
  j["agent_id"] = config_.event_log_agent_id;
  if (event.payload.trace_id.has_value() && !event.payload.trace_id->empty()) {
    j["session_id"] = *event.payload.trace_id;
  }
  j["payload"] = event.payload.data;
  if (event.payload.error_code.has_value()) {
    j["payload"]["_error_code"] =
 static_cast<int>(event.payload.error_code.value());
  }
  if (event.payload.latency_ms > 0) {
    j["payload"]["_latency_ms"] = event.payload.latency_ms;
  }
  return j.dump();
}

void EventLogWriter::rotate_if_needed() {
  if (current_file_size_ < config_.max_file_size) return;
  if (!file_.is_open()) return;
  file_.flush();
  file_.close();
  // 简化 rotation：仅 .rotation.1（保留最近 N 个）
  std::error_code ec;
  for (size_t i = config_.max_rotation_files; i > 0; --i) {
    auto src = (i == 1) ? log_path_
                           : std::filesystem::path(log_path_.string() +
                                                  ".rotation." + std::to_string(i - 1));
    auto dst = std::filesystem::path(log_path_.string() + ".rotation." +
                                      std::to_string(i));
    std::filesystem::rename(src, dst, ec);
  }
  file_.open(log_path_, std::ios::out | std::ios::trunc);
  current_file_size_ = 0;
}

std::vector<BusEvent> EventLogWriter::read(
    const std::string& agent_id,
    const std::filesystem::path& log_dir) {
  std::vector<BusEvent> out;
  auto path = log_dir / (agent_id + ".v1.jsonl");
  std::ifstream in(path);
  if (!in.is_open()) return out;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    try {
      auto j = nlohmann::json::parse(line);
      if (j.value("v", 0) != 1) continue;
      BusEvent e;
      e.topic = j.value("topic", std::string{});
      e.causal_time = j.value("causal_time", uint64_t{0});
      auto payload_it = j.find("payload");
      if (payload_it != j.end() && payload_it->is_object()) {
        e.payload.data = *payload_it;
        auto ec_it = payload_it->find("_error_code");
        if (ec_it != payload_it->end()) {
          e.payload.error_code = static_cast<ErrorCode>(ec_it->get<int>());
        }
      }
      out.push_back(std::move(e));
    } catch (const nlohmann::json::exception&) {
      continue;
    }
  }
  return out;
}

std::vector<BusEvent> EventLogWriter::read(
    const std::string& agent_id,
    uint64_t start_causal_time,
    uint64_t end_causal_time) const {
  auto all = read(agent_id, config_.event_log_dir);
  std::vector<BusEvent> filtered;
  filtered.reserve(all.size());
  for (auto& e : all) {
    if (e.causal_time >= start_causal_time && e.causal_time <= end_causal_time) {
      filtered.push_back(std::move(e));
    }
  }
  return filtered;
}

namespace {

bool topic_matches_glob(const std::string& topic, const std::string& glob) {
  if (glob.empty() || glob == "*") return true;
  auto pos = glob.find('*');
  if (pos == std::string::npos) return topic == glob;
  if (pos == 0) return true;
  return topic.substr(0, pos) == glob.substr(0, pos);
}

// 提前筛选: 在 JSON parse 之前做 substring 检查, 跳过明显不匹配的行
// JSONL 格式: ...,"topic":"<name>",... ; 通过 find("\"topic\":\"<prefix>") 排除
// 仅在 glob 简单前缀场景下有效; glob 为 "*" 或空时返回 true (无条件 parse)
bool line_topic_quick_match(const std::string& line, const std::string& glob) {
  if (glob.empty() || glob == "*") return true;
  auto pos = glob.find('*');
  if (pos == 0) return true;  // "*" 或 "*.suffix" 匹配所有 topic
  // 前缀匹配: 检查 line 是否包含 "\"topic\":\"<prefix>"
  std::string needle = "\"topic\":\"";
  needle += glob.substr(0, pos);
  return line.find(needle) != std::string::npos;
}

}  // namespace

std::vector<BusEvent> EventLogWriter::query(
    const std::string& agent_id,
    const QueryFilter& filter,
    size_t max_count) const {
  std::vector<BusEvent> out;
  out.reserve(std::min(max_count, size_t{1024}));
  auto path = config_.event_log_dir / (agent_id + ".v1.jsonl");
  std::ifstream in(path);
  if (!in.is_open()) return out;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    if (!line_topic_quick_match(line, filter.topic_glob)) continue;
    try {
      auto j = nlohmann::json::parse(line);
      if (j.value("v", 0) != 1) continue;
      auto topic = j.value("topic", std::string{});
      if (!topic_matches_glob(topic, filter.topic_glob)) continue;
      auto ct = j.value("causal_time", uint64_t{0});
      if (filter.has_time_window &&
          (ct < filter.start_causal_time || ct > filter.end_causal_time)) {
        continue;
      }
      BusEvent e;
      e.topic = std::move(topic);
      e.causal_time = ct;
      auto payload_it = j.find("payload");
      if (payload_it != j.end() && payload_it->is_object()) {
        e.payload.data = *payload_it;
        auto ec_it = payload_it->find("_error_code");
        if (ec_it != payload_it->end()) {
          e.payload.error_code = static_cast<ErrorCode>(ec_it->get<int>());
        }
      }
      out.push_back(std::move(e));
      if (out.size() >= max_count) break;
    } catch (const nlohmann::json::exception&) {
      continue;
    }
  }
  return out;
}

}  // namespace agenticdsl