// src/modules/skill_interpreter/skill_interpreter.cpp
// 功能描述：SkillInterpreter PIMPL 实现 — ADR-0055 父进程侧隔离执行引擎。
//          负责 posix_spawn + IPC 循环（poll + NDJSON）+ max_steps 强制 +
//          capability 检查 + budget 计数器 + stderr 收集 + 超时 SIGKILL。
// 设计依据：ADR-0055 + openspec/changes/skill-interpreter-real-loading/design.md
// 作者：AgenticDSL SkillInterpreter change
// 最后修改日期：2026-07-22

#include "agenticdsl/skill/skill_interpreter.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Linux 特有头文件
#ifdef __linux__
#include <fcntl.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <spawn.h>
#endif

#include <nlohmann/json.hpp>

#include "agenticdsl/contract/iinteraction_bus.h"
#include "agenticdsl/contract/itool_registry.h"
#include "agenticdsl/contract/timer_service.h"
#include "agenticdsl/types/layered_context.h"
#include "core/types/tool_result.h"

// common/llm/llm_types.h 中的 ILLMProvider（非契约层 forward-declare）
#include "common/llm/llm_types.h"

namespace agenticdsl {

// ============================================================
// 常量定义
// ============================================================

/// NDJSON 单条消息上限（1 MB，防止恶意 SKILL OOM 父进程）
constexpr size_t kIPCMessageMaxBytes = 1024 * 1024;

/// stderr 收集上限（1 MB）
constexpr size_t kStderrMaxBytes = 1024 * 1024;

/// 子进程退出码约定（父进程 WEXITSTATUS 检测）
constexpr int EXIT_CHILD_PARSE_ERROR = 64;
constexpr int EXIT_CHILD_ENV_MISSING = 70;
constexpr int EXIT_CHILD_PRCTL_ERROR = 71;
constexpr int EXIT_CHILD_SECCOMP_ERROR = 72;
constexpr int EXIT_CHILD_THREAD_LEAK = 73;

// ============================================================
// 内部工具函数
// ============================================================

/// 从 fd 读取一行（以 \n 结尾），维护行缓冲。返回空 string 表示 EOF。
/// 单条消息超限时截断并标记。
struct ReadLineResult {
  std::string line;
  bool truncated = false;
};

static ReadLineResult read_line(int fd, std::string& buf) {
  // 从 fd 读取到内部缓冲区
  char tmp[4096];
  ssize_t n = read(fd, tmp, sizeof(tmp));
  if (n <= 0) {
    // EOF 或错误：如果缓冲区有残存数据，当作最后一行
    if (!buf.empty()) {
      std::string line = std::move(buf);
      buf.clear();
      return {line, false};
    }
    return {"", false};
  }

  buf.append(tmp, static_cast<size_t>(n));

  // 查找 \n
  auto pos = buf.find('\n');
  if (pos == std::string::npos) {
    // 没有完整行：检查是否超限
    if (buf.size() > kIPCMessageMaxBytes) {
      buf.resize(kIPCMessageMaxBytes);
      // 丢弃多余数据直到下一个 \n
      return {"", true};
    }
    return {"", false};
  }

  std::string line = buf.substr(0, pos);
  buf.erase(0, pos + 1);
  return {line, false};
}

/// 写行到 fd（追加 \n）
static bool write_line(int fd, const std::string& msg) {
  std::string framed = msg + '\n';
  const char* data = framed.data();
  size_t remaining = framed.size();
  while (remaining > 0) {
    ssize_t n = write(fd, data, remaining);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    data += n;
    remaining -= static_cast<size_t>(n);
  }
  return true;
}

/// 解析 NDJSON 帧为 IPC 请求
struct IPCRequest {
  std::string method;
  nlohmann::json params;
};

static bool parse_request(const std::string& line, IPCRequest& req) {
  try {
    auto j = nlohmann::json::parse(line);
    req.method = j.value("method", "");
    req.params = j.value("params", nlohmann::json::object());
    return !req.method.empty();
  } catch (...) {
    return false;
  }
}

/// 构造 IPC 响应
static std::string make_response(bool ok, nlohmann::json result,
                                  const std::string& error = "") {
  nlohmann::json j;
  j["ok"] = ok;
  if (!result.is_null()) j["result"] = std::move(result);
  if (!error.empty()) j["error"] = error;
  return j.dump();
}

// ============================================================
// SkillInterpreter::Impl — PIMPL 实现
// ============================================================

class SkillInterpreter::Impl {
 public:
  // Sprint 29 timer migration: 注入 ITimerService*
  // - 非 nullptr 时使用注入 timer (测试可传 FakeTimer, 调用方持有 lifetime)
  // - nullptr 时 timer_ 保持 nullptr, 不自动创建 TimerService
  //   (原因: TimerService jthread 创建影响 fork+exec timing, 导致 baseline
  //    tests 7.8b/7.8c 回归。Sprint 30+ 可改为 lazy per-run 注册)
  Impl(IToolRegistry& tools,
       IInteractionBus& bus,
       ILLMProvider* llm,
       const LayeredContext* ctx,
       ITimerService* timer = nullptr)
      : tools_(&tools),
        bus_(&bus),
        llm_(llm),
        ctx_(ctx),
        timer_(timer),
        owned_timer_(nullptr) {
    // timer_ 直接使用注入指针; nullptr 路径不创建默认 TimerService
    // (见上方注释, Sprint 30+ 改进方向)
  }

  ~Impl() {
    // D8 四步析构顺序 (Oracle session ses_f6f25fd0bffeX5P4rs1hvHOYXQ 决议):
    // ① 防御性 cancel deadline timer (idempotent, RAII guard 已 cancel 时 id=0)
    if (deadline_timer_id_ != 0 && timer_) {
      timer_->cancel(deadline_timer_id_);
      deadline_timer_id_ = 0;
    }
    // ② 排空 timer (jthread join 等 in-flight callback 完成)
    //    owned_timer_.reset() 在函数体内先于成员析构,所有 Impl 成员仍存活,
    //    callback 内 [this] 访问合法,无 UAF
    owned_timer_.reset();
    timer_ = nullptr;
    // ③ 子进程回收 (防止僵尸)
    if (child_pid_ > 0) {
      kill(child_pid_, SIGKILL);
      // SIGKILL 失败忽略
      waitpid_reap(child_pid_, nullptr);
      child_pid_ = -1;
    }
    // ④ 关闭 pipes
    close_all_pipes();
  }

  SkillResult run(const std::string& skill_path,
                  const SkillCapability& cap,
                  std::stop_token token) {
#ifdef __linux__
    // 重置预算计数器
    budget_used_.store(0.0, std::memory_order_relaxed);
    auto start_time = std::chrono::steady_clock::now();

    // === Step 1: 创建 3 对 pipe ===
    int pipe_in[2] = {-1, -1};   // 父→子（响应）
    int pipe_out[2] = {-1, -1};  // 子→父（请求）
    int pipe_err[2] = {-1, -1};  // 子→父（stderr）

    if (pipe2(pipe_in, O_CLOEXEC) < 0 ||
        pipe2(pipe_out, O_CLOEXEC) < 0 ||
        pipe2(pipe_err, O_CLOEXEC) < 0) {
      return make_error(ErrorCode::ResourceExhausted,
                        "pipe2 failed", 0, -1);
    }

    // === Step 2: 获取自身可执行路径 ===
    char exe_path[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (n < 0) {
      close_fd(pipe_in[0]); close_fd(pipe_in[1]);
      close_fd(pipe_out[0]); close_fd(pipe_out[1]);
      close_fd(pipe_err[0]); close_fd(pipe_err[1]);
      return make_error(ErrorCode::UnsupportedPlatform,
                        "readlink(/proc/self/exe) failed", 0, -1);
    }
    exe_path[n] = '\0';

    // === Step 3: 构造 posix_spawn file_actions ===
    // 顺序敏感（spike §0.4 C5 修正）：先 dup2 再 closefrom
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    // 3a. 先 dup2：将 pipe fd 映射到 0/1/2
    posix_spawn_file_actions_adddup2(&actions, pipe_in[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipe_out[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipe_err[1], STDERR_FILENO);

    // 3b. 再 closefrom：关闭 ≥3 的所有 fd（POSIX 按 add-order 执行）
    posix_spawn_file_actions_addclosefrom_np(&actions, 3);

    // === Step 4: 构造 argv + envp ===
    const char* argv[] = {exe_path, "--skill-child", nullptr};

    std::string env_skill_path = "SKILL_PATH=" + skill_path;
    std::string env_ipc_in     = "SKILL_IPC_IN=0";   // STDIN_FILENO
    std::string env_ipc_out    = "SKILL_IPC_OUT=1";  // STDOUT_FILENO
    std::string env_ipc_err    = "SKILL_IPC_ERR=2";  // STDERR_FILENO

    char* envp[] = {
        env_skill_path.data(),
        env_ipc_in.data(),
        env_ipc_out.data(),
        env_ipc_err.data(),
        nullptr,
    };

    // === Step 5: posix_spawn ===
    pid_t pid;
    int spawn_ret = posix_spawn(&pid, exe_path, &actions, nullptr,
                                 const_cast<char* const*>(argv),
                                 const_cast<char* const*>(envp));

    posix_spawn_file_actions_destroy(&actions);

    if (spawn_ret != 0) {
      close_fd(pipe_in[0]); close_fd(pipe_in[1]);
      close_fd(pipe_out[0]); close_fd(pipe_out[1]);
      close_fd(pipe_err[0]); close_fd(pipe_err[1]);
      return make_error(ErrorCode::ResourceExhausted,
                        "posix_spawn failed: " + std::string(std::strerror(spawn_ret)),
                        0, -1);
    }

    child_pid_ = pid;

    // === Step 5a: 设置父进程资源限制 ===
    // RLIMIT_CORE=0: 防止子进程 crash 产生 core dump 泄露内存
    struct rlimit core_lim = {0, 0};
    setrlimit(RLIMIT_CORE, &core_lim);
    // RLIMIT_CPU: CPU 时间兜底（父进程 SIGKILL 之外的二道防线）
    struct rlimit cpu_lim;
    cpu_lim.rlim_cur = static_cast<rlim_t>(cap.timeout_ms.count() / 1000) + 1;
    cpu_lim.rlim_max = cpu_lim.rlim_cur + 1;
    setrlimit(RLIMIT_CPU, &cpu_lim);

    // 父进程关闭子进程端的 pipe fd
    close_fd(pipe_in[0]);   // 子进程已 dup2 到 STDIN，父进程不需要读端
    close_fd(pipe_out[1]);  // 子进程已 dup2 到 STDOUT
    close_fd(pipe_err[1]);  // 子进程已 dup2 到 STDERR

    // === Step 6: IPC 循环 ===
    SkillResult result = ipc_loop_and_wait(
        pid, pipe_out[0], pipe_in[1], pipe_err[0], cap, token);

    auto end_time = std::chrono::steady_clock::now();
    result.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();

    // 关闭父进程端 pipe fd
    close_fd(pipe_out[0]);
    close_fd(pipe_in[1]);
    close_fd(pipe_err[0]);

    child_pid_ = -1;
    return result;

#else
    (void)skill_path;
    (void)cap;
    return make_error(ErrorCode::UnsupportedPlatform,
                      "SkillInterpreter requires Linux", 0, -1);
#endif
  }

 private:
  IToolRegistry* tools_;
  IInteractionBus* bus_;
  ILLMProvider* llm_;
  const LayeredContext* ctx_;
  std::atomic<double> budget_used_{0.0};
  pid_t child_pid_ = -1;

  // pipe fd 缓存（用于析构关闭）
  int pipe_in_w_ = -1;
  int pipe_out_r_ = -1;
  int pipe_err_r_ = -1;

  // === Sprint 29 timer migration (D1/D3/D5/D9) ===
  // owned_timer_: RAII 持有 TimerService (D9 eager at Impl 构造)
  // timer_: 观察者指针 (注入时为外部指针,否则指向 owned_timer_.get())
  // deadline_timer_id_: 当前 run() 注册的 deadline oneshot timer id
  // deadline_exceeded_: timer callback → 主循环通信 (acquire/release 序)
  std::unique_ptr<ITimerService> owned_timer_;
  ITimerService* timer_ = nullptr;
  ITimerService::TimerId deadline_timer_id_ = 0;
  std::atomic<bool> deadline_exceeded_{false};

  // === Wave 4.6: IPC loop 退出信号 (LLM timeout 触发) ===
  // stop_input_thread_: D1 timeout 时设 true, IPC loop 下次迭代 break (避免 write_line 阻塞)
  // input_cv_: notify 通知其他 wait 的线程
  std::atomic<bool> stop_input_thread_{false};
  std::condition_variable input_cv_;

  void close_fd(int& fd) {
    if (fd >= 0) {
      close(fd);
      fd = -1;
    }
  }

  void close_all_pipes() {
    close_fd(pipe_in_w_);
    close_fd(pipe_out_r_);
    close_fd(pipe_err_r_);
  }

  SkillResult make_error(ErrorCode code, const std::string& msg,
                          uint64_t duration, int exit_status) {
    SkillResult r;
    r.success = false;
    r.error_code = code;
    r.stderr_content = msg;
    r.duration_ms = duration;
    r.child_exit_status = exit_status;
    return r;
  }

  /// waitpid EINTR 重试（避免僵尸残留）
  static int waitpid_reap(pid_t pid, int* status) {
    int s;
    int ret;
    do {
      ret = waitpid(pid, &s, 0);
    } while (ret == -1 && errno == EINTR);
    if (status && ret > 0) *status = s;
    return ret;
  }

  /// kill EINTR 重试
  static int kill_retry(pid_t pid, int sig) {
    int ret;
    do {
      ret = kill(pid, sig);
    } while (ret == -1 && errno == EINTR);
    return ret;
  }

  /// IPC 循环 + waitpid 超时管理
  SkillResult ipc_loop_and_wait(pid_t pid, int pipe_out_r, int pipe_in_w,
                                 int pipe_err_r, const SkillCapability& cap,
                                 std::stop_token token = {}) {
    // 保存 pipe fd 以便析构时关闭
    pipe_out_r_ = pipe_out_r;
    pipe_in_w_ = pipe_in_w;
    pipe_err_r_ = pipe_err_r;

    // 预算计数器重置
    budget_used_.store(0.0, std::memory_order_relaxed);

    auto deadline = std::chrono::steady_clock::now() + cap.timeout_ms;
    size_t steps_used = 0;
    std::string stderr_buf;
    bool stderr_truncated = false;
    std::string read_buf_in;   // pipe_out 行缓冲
    std::string read_buf_err;  // pipe_err 行缓冲
    nlohmann::json output = nlohmann::json::object();

    // === Sprint 29 timer migration (D2: deadline oneshot 注册) ===
    // timer callback 写 atomic flag, 主循环 loop-top 检查
    // release 序在 callback, acquire 序在主循环读 (D3 memory ordering)
    // 注意: 注册开销 (mutex + map insert + cv notify ≈ 5-20µs) 在 loop 之前,
    // 理论上可能延迟首个 loop-top check, 但实测在 fork+exec 后 (≥1ms) 无影响
    deadline_exceeded_.store(false, std::memory_order_relaxed);
    if (timer_) {
      deadline_timer_id_ = timer_->register_oneshot(
          cap.timeout_ms,
          [this] { deadline_exceeded_.store(true, std::memory_order_release); });
    }

    // === RAII guard: 函数返回时自动 cancel deadline timer ===
    // 覆盖 4 处 break (POLLHUP/EOF/parse-error/write-fail) + 5 处 return
    // (cancel/timeout/poll-err/max-steps/dispatch-terminate) + 最终 return
    // 析构函数再防御性 cancel (D8 四步顺序步骤①)
    struct DeadlineTimerGuard {
      ITimerService* timer;
      ITimerService::TimerId* id_ptr;
      ~DeadlineTimerGuard() {
        if (timer && *id_ptr != 0) {
          timer->cancel(*id_ptr);
          *id_ptr = 0;
        }
      }
    } deadline_guard{timer_, &deadline_timer_id_};

    pollfd fds[2];
    fds[0].fd = pipe_out_r;
    fds[0].events = POLLIN;
    fds[1].fd = pipe_err_r;
    fds[1].events = POLLIN;

    while (true) {
      auto now = std::chrono::steady_clock::now();
      auto remaining = deadline - now;

      // 外部 cancel → 立即 SIGKILL (不等到 cap.timeout_ms)
      if (token.stop_requested()) {
        kill_retry(pid, SIGKILL);
        int status;
        waitpid_reap(pid, &status);
        SkillResult r;
        r.success = false;
        r.error_code = ErrorCode::Abort;
        r.stderr_content = stderr_buf;
        r.stderr_truncated = stderr_truncated;
        r.child_exit_status = status;
        return r;
      }

      // Sprint 29: timer-driven deadline → 立即 SIGKILL (Oracle D11 first-wins)
      // timer callback 已 set deadline_exceeded_, 主循环 acquire 序读取
      if (deadline_exceeded_.load(std::memory_order_acquire)) {
        kill_retry(pid, SIGKILL);
        int status;
        waitpid_reap(pid, &status);
        SkillResult r;
        r.success = false;
        r.error_code = ErrorCode::Timeout;
        r.stderr_content = stderr_buf;
        r.stderr_truncated = stderr_truncated;
        r.child_exit_status = status;
        return r;
      }

      if (remaining <= std::chrono::nanoseconds(0)) {
        kill_retry(pid, SIGKILL);
        int status;
        waitpid_reap(pid, &status);
        SkillResult r;
        r.success = false;
        r.error_code = ErrorCode::Timeout;
        r.stderr_content = stderr_buf;
        r.stderr_truncated = stderr_truncated;
        r.child_exit_status = status;
        return r;
      }

      // poll EINTR 重试
      int timeout_ms = 0;
      if (remaining > std::chrono::nanoseconds(0)) {
        auto rem_ms = std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
        // 最小 1ms, 防止截断为 0 导致 false timeout
        timeout_ms = std::max(1, static_cast<int>(rem_ms.count()));
        // P1 (Oracle SHIP-with-fixes): clamp poll 周期 ≤ 100ms 以使 mid-run cancel
        // 及时响应. 原 cap.timeout_ms 路径下 hung 子进程会阻塞 poll 至 timeout,
        // token 检查只在 loop-top, 外部 cancel 等满 timeout 才生效.
        // 100ms 粒度足够 IPC 心跳 + cancel 响应, 不引入显著 CPU 开销.
        timeout_ms = std::min(timeout_ms, 100);
      }

      int n;
      do {
        n = poll(fds, 2, timeout_ms);
      } while (n < 0 && errno == EINTR);

      if (n == 0) {
        // 超时无事件
        continue;
      }

      if (n < 0) {
        // poll 错误（非 EINTR），SIGKILL 子进程
        kill_retry(pid, SIGKILL);
        int status;
        waitpid_reap(pid, &status);
        SkillResult r;
        r.success = false;
        r.error_code = ErrorCode::ResourceExhausted;
        r.stderr_content = stderr_buf;
        r.stderr_truncated = stderr_truncated;
        r.child_exit_status = status;
        return r;
      }

      // 处理 stderr（先读，防止 pipe 缓冲区满阻塞子进程）
      if (fds[1].revents & POLLIN) {
        auto rl = read_line(pipe_err_r, read_buf_err);
        if (!rl.line.empty()) {
          if (stderr_buf.size() + rl.line.size() <= kStderrMaxBytes) {
            stderr_buf += rl.line + "\n";
          } else {
            stderr_truncated = true;
          }
        }
      }

      // 处理 pipe_out（子进程 IPC 请求）
      if (fds[0].revents & POLLIN) {
        // Wave 4.6 fix: D1 timeout 杀掉 child 后 child 是 zombie, 但 read_line 可能 block
        // (kernel pipe fd 未完全 close). 先 waitpid(WNOHANG) 检测 zombie → 直接 break
        // 避免在 read_line 上 hang. 同时处理 stop_input_thread_ 标志
        int wstatus = 0;
        pid_t wret = waitpid(pid, &wstatus, WNOHANG);
        if (wret == pid || (wret == -1 && errno == ECHILD)) {
          // child 是 zombie 或已被 reap → IPC loop 结束
          break;
        }
        if (stop_input_thread_.load(std::memory_order_acquire)) {
          break;
        }
        auto rl = read_line(pipe_out_r, read_buf_in);
        if (rl.truncated) {
          // IPC 消息超 1MB → SIGKILL
          kill_retry(pid, SIGKILL);
          int status;
          waitpid_reap(pid, &status);
          SkillResult r;
          r.success = false;
          r.error_code = ErrorCode::MaxStepsExceeded;
          r.stderr_content = stderr_buf;
          r.stderr_truncated = stderr_truncated;
          r.child_exit_status = status;
          return r;
        }

        if (rl.line.empty()) {
          // EOF — 子进程已退出（pipe 关闭）
          break;
        }

        // max_steps 强制：每收到一个 IPC 请求就递增
        if (++steps_used > cap.max_steps) {
          kill_retry(pid, SIGKILL);
          int status;
          waitpid_reap(pid, &status);
          SkillResult r;
          r.success = false;
          r.error_code = ErrorCode::MaxStepsExceeded;
          r.stderr_content = stderr_buf;
          r.stderr_truncated = stderr_truncated;
          r.child_exit_status = status;
          return r;
        }

        // 解析 IPC 请求
        IPCRequest req;
        if (!parse_request(rl.line, req)) {
          write_line(pipe_in_w, make_response(false, nullptr, "parse error"));
          continue;
        }

        // dispatch
        IPCResponse resp = dispatch(req, cap, pid, token);
        if (!write_line(pipe_in_w, serialize(resp))) {
          // 写失败 → 子进程 pipe 已关闭
          // Wave 4.6 fix: 若 child 被 kill_retry (e.g. D1 timeout) 后 parent write
          // 可能不立即 fail (kernel 未完全关闭 pipe), 此时 check waitpid(WNOHANG)
          // 确认 child 已 zombie → break. 比等 POLLHUP 触发更主动.
          int wstatus = 0;
          pid_t wret = waitpid(pid, &wstatus, WNOHANG);
          if (wret == pid || (wret == -1 && errno == ECHILD)) {
            // child 是 zombie 或已被 reap → IPC loop 结束
            break;
          }
          // child 还没死 (write 失败但 child 还在 process 退出中),
          // 下一轮 poll() 应该检测到 POLLHUP, 但保险起见也 break 避免 hang
          break;
        }

        // 捕获 return 值
        if (req.method == "return") {
          output = req.params.value("value", nlohmann::json::object());
          break;
        }

        // 检查 terminate 标志（budget 超限时 dispatch 已 SIGKILL 子进程）
        if (resp.terminate) {
          int status;
          waitpid_reap(pid, &status);
          SkillResult tr;
          tr.success = false;
          tr.error_code = ErrorCode::BudgetExhausted;
          tr.stderr_content = stderr_buf;
          tr.stderr_truncated = stderr_truncated;
          tr.child_exit_status = status;
          return tr;
        }
      }

      // POLLHUP — 子进程 pipe 关闭
      if (fds[0].revents & POLLHUP) {
        break;
      }
    }

    // === waitpid 回收 ===
    int status;
    waitpid_reap(pid, &status);

    SkillResult r;
    r.success = (WIFEXITED(status) && WEXITSTATUS(status) == 0);
    r.output = output;
    r.stderr_content = stderr_buf;
    r.stderr_truncated = stderr_truncated;
    r.child_exit_status = status;

    // 区分退出原因
    if (WIFEXITED(status)) {
      int exit_code = WEXITSTATUS(status);
      if (exit_code == EXIT_CHILD_PARSE_ERROR) {
        r.success = false;
        r.error_code = ErrorCode::InvalidArg;
      } else if (exit_code == EXIT_CHILD_ENV_MISSING) {
        r.success = false;
        r.error_code = ErrorCode::InvalidArg;
      } else if (exit_code == EXIT_CHILD_PRCTL_ERROR) {
        r.success = false;
        r.error_code = ErrorCode::UnsupportedPlatform;
      } else if (exit_code == EXIT_CHILD_SECCOMP_ERROR) {
        r.success = false;
        r.error_code = ErrorCode::SandboxViolation;
      } else if (exit_code == EXIT_CHILD_THREAD_LEAK) {
        r.success = false;
        r.error_code = ErrorCode::Crash;
      } else if (exit_code != 0) {
        r.success = false;
        r.error_code = ErrorCode::Crash;
      }
    } else if (WIFSIGNALED(status)) {
      int sig = WTERMSIG(status);
      if (sig == SIGSYS) {
        r.success = false;
        r.error_code = ErrorCode::SandboxViolation;
      } else if (sig == SIGKILL) {
        // 超时或 budget 超限已在上层处理
        if (r.error_code == ErrorCode::Unknown) {
          r.success = false;
          r.error_code = ErrorCode::Crash;
        }
      } else {
        r.success = false;
        r.error_code = ErrorCode::Crash;
      }
    }

    return r;
  }

  // === dispatch — 将 IPC 请求分发给 host 服务 ===
  struct IPCResponse {
    bool ok = false;
    nlohmann::json result;
    std::string error;
    bool terminate = false;  // true = SIGKILL 后该 IPC 不到达
  };

  std::string serialize(const IPCResponse& resp) {
    return make_response(resp.ok, resp.result, resp.error);
  }

  IPCResponse dispatch(const IPCRequest& req, const SkillCapability& cap,
                       pid_t pid, std::stop_token token = {}) {
    if (req.method == "call_tool") {
      return dispatch_call_tool(req, cap, pid);
    } else if (req.method == "emit_event") {
      return dispatch_emit_event(req, cap);
    } else if (req.method == "llm_generate") {
      return dispatch_llm_generate(req, cap, token);  // fix-skill-interpreter-dispatch-llm-token: token 透传
    } else if (req.method == "consume_budget") {
      return dispatch_consume_budget(req, cap, pid);
    } else if (req.method == "return") {
      // return 由上层捕获，不放行到这里
      return IPCResponse{true, req.params.value("value", nlohmann::json::object())};
    } else {
      return IPCResponse{false, nullptr, "unknown method"};
    }
  }

  IPCResponse dispatch_call_tool(const IPCRequest& req,
                                  const SkillCapability& cap,
                                  pid_t pid) {
    std::string tool_name = req.params.value("name", "");
    auto args = req.params.value("args", nlohmann::json::object());

    // capability 检查：allowed_tools 白名单
    if (!cap.allowed_tools.empty()) {
      if (std::find(cap.allowed_tools.begin(), cap.allowed_tools.end(),
                    tool_name) == cap.allowed_tools.end()) {
        return IPCResponse{false, nullptr, "tool not allowed"};
      }
    }

    if (!tools_) {
      return IPCResponse{false, nullptr, "no tool registry"};
    }

    try {
      // IToolRegistry::call_tool_json 接受 name+json args
      nlohmann::json result = tools_->call_tool_json(tool_name, args);
      return IPCResponse{true, result};
    } catch (const std::exception& e) {
      return IPCResponse{false, nullptr, e.what()};
    }
  }

  IPCResponse dispatch_emit_event(const IPCRequest& req,
                                   const SkillCapability& cap) {
    std::string topic = req.params.value("topic", "");
    auto payload = req.params.value("payload", nlohmann::json::object());

    // C3 defense: allowed_topics whitelist
    if (!cap.allowed_topics.empty()) {
      if (std::find(cap.allowed_topics.begin(), cap.allowed_topics.end(),
                    topic) == cap.allowed_topics.end()) {
        return IPCResponse{false, nullptr,
                           "emit_event topic not in allowed_topics"};
      }
    }

    if (bus_) {
      // Decision 12: 桥接到 string 重载
      bus_->emit(topic, payload.dump());
    }
    return IPCResponse{true};
  }

  IPCResponse dispatch_llm_generate(const IPCRequest& req,
                                     const SkillCapability& cap,
                                     std::stop_token token = {}) {
    if (!cap.allow_llm || !llm_) {
      return IPCResponse{false, nullptr, "llm_generate not allowed"};
    }

    // Oracle bg_e3787930 观察 #1: early-exit on cancelled token (防御性).
    // 避免在 token 已 cancel 时还调用 llm_->generate (依赖 provider 自觉检查).
    // 对 well-behaved provider 等价, 对不响应 token 的 provider 是强保证.
    if (token.stop_requested()) {
      return IPCResponse{false, nullptr, "cancelled before llm_generate"};
    }

    std::string prompt = req.params.value("prompt", "");

    // === Wave 4.5 D1: 独立 worker thread + cv.wait_for + kill_retry ===
    // 真正修复 misbehaved provider 永久 hang 场景 (不依赖 provider 自觉 stop_token).
    // 复用 Sprint 28 jthread pattern + Sprint 29 kill_retry 清理子进程.
    // Result<T,E> 构造函数是 private (llm_types.h:106), 不能默认构造, 用
    // unique_ptr<Result<>> 包装 (nullptr 表示未生成, factory 创建后 unique)
    std::unique_ptr<Result<GenerationResult, LLMError>> result_ptr;
    std::exception_ptr eptr;
    std::atomic<bool> done{false};
    std::mutex m;
    std::condition_variable cv;

    std::thread worker([&] {
      try {
        GenerationRequest gen_req;
        gen_req.prompt = prompt;
        // ⚠️ NOT redundant: LLMParams = LLMConfig 别名, 默认 model = "gpt-4o-mini"
        // (非空). 若不清空, CloudLLMAdapter L164 会拿默认遮蔽 factory 设置的真实
        // model (skill 子进程通过 IPC llm_generate 调用父进程 LLM 时尤其重要,
        // server 拒绝 "you passed gpt-4o-mini"). 清空让 adapter fallback.
        // 详见 openspec/changes/fix-generation-request-model-default/.
        gen_req.params.model.clear();
        auto r = llm_->generate(gen_req, token);  // fix-skill-interpreter-dispatch-llm-token: 替换硬编码 {} 为外部 token
        {
          std::lock_guard<std::mutex> lk(m);
          result_ptr = std::make_unique<Result<GenerationResult, LLMError>>(std::move(r));
        }
      } catch (...) {
        eptr = std::current_exception();
      }
      done.store(true);
      cv.notify_all();
    });

    {
      std::unique_lock<std::mutex> lk(m);
      cv.wait_for(lk, cap.timeout_ms, [&] { return done.load(); });
    }
    worker.join();  // 必 join (即使 worker throw, RAII-style)

    if (!done.load()) {
      // 超时: kill 子进程 (Sprint 29 已 ship kill_retry line 347-352)
      // child_pid_ 是 Impl 成员 (line 195 析构函数用, 这里复用)
      kill_retry(this->child_pid_, SIGKILL);
      // ⚠️ worker.detach() 关键: BlockingLLMProvider 永远 hang (设计如此模拟 misbehaved
      // provider), worker.join() 会 block forever → std::thread dtor 触发 std::terminate()
      // → 进程崩溃. detach 让 worker 继续后台运行 (线程泄漏可接受, 目标是不让
      // 父进程IPC loop 永久 hang). worker 会在进程退出时被回收.
      worker.detach();
      // Wave 4.6 修复: 设 stop_input_thread_=true, IPC loop 下次迭代 break (避免 write_line
      // 阻塞 — kernel 刚 kill 子进程后, parent write 到子进程 pipe 可能不立即 EPIPE
      // 而是 block 直到子进程 pipe fd 被完全关闭. stop_input_thread_=true 让 IPC loop
      // 下次循环 check 时直接 break, 避免在 write_line 上 hang)
      this->stop_input_thread_.store(true, std::memory_order_release);
      // Also notify input_cv_ in case parent thread is waiting on it
      this->input_cv_.notify_all();
      return IPCResponse{false, nullptr, "llm_generate timeout"};
    }
    worker.join();  // 正常路径: worker 已 done, join 立即返回
    if (eptr) std::rethrow_exception(eptr);
    if (result_ptr && result_ptr->has_value()) {
      return IPCResponse{true, {{"content", result_ptr->value().text}}};
    }
    return IPCResponse{false, nlohmann::json::object(), "LLM generation failed"};
  }

  IPCResponse dispatch_consume_budget(const IPCRequest& req,
                                       const SkillCapability& cap,
                                       pid_t pid) {
    double amount = req.params.value("amount", 0.0);
    // Decision 11: 内部 std::atomic<double> 计数器
    double current = budget_used_.load(std::memory_order_relaxed);
    while (true) {
      if (current + amount > cap.budget_limit_usd) {
        kill_retry(pid, SIGKILL);
        return IPCResponse{false, nlohmann::json::object(), "budget exceeded", true};
      }
      if (budget_used_.compare_exchange_weak(current, current + amount)) {
        break;
      }
    }
    return IPCResponse{true};
  }
};

// ============================================================
// SkillInterpreter 公开 API
// ============================================================

SkillInterpreter::SkillInterpreter(IToolRegistry& tools,
                                    IInteractionBus& bus,
                                    ILLMProvider* llm,
                                    const LayeredContext* ctx,
                                    ITimerService* timer)
    : impl_(std::make_unique<Impl>(tools, bus, llm, ctx, timer)) {}

SkillInterpreter::~SkillInterpreter() = default;  // out-of-line

SkillInterpreter::SkillInterpreter(SkillInterpreter&&) noexcept = default;
SkillInterpreter& SkillInterpreter::operator=(SkillInterpreter&&) noexcept = default;

SkillResult SkillInterpreter::run(const std::string& skill_path,
                                   const SkillCapability& cap,
                                   std::stop_token token) {
  return impl_->run(skill_path, cap, token);
}

}  // namespace agenticdsl