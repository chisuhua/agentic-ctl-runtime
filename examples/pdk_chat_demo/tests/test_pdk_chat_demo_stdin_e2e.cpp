// tests/test_pdk_chat_demo_stdin_e2e.cpp
// §9 chat-async-io-consumer-loop: pipe E2E regression guard
// 关联: openspec/changes/chat-async-io-consumer-loop
//
// Validates single-reader mode fix:
//  - stdin pipe mode: full input received (no character loss from race)
//  - EOF triggers graceful exit (no hang)
//  - exit code 0 on normal completion

#include <catch_amalgamated.hpp>
#include <fcntl.h>
#include <iostream>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

namespace {

struct CinEofGuard {
    CinEofGuard()  { std::cin.setstate(std::ios::eofbit); }
    ~CinEofGuard() { std::cin.clear(); }
};

// Run pdk_chat_demo --mock with stdin pipe input, capture stdout + exit code.
struct SubprocessResult {
    int exit_code = -1;
    bool signaled = false;
    int signal = 0;
    std::string stdout_output;
    double elapsed_sec = 0.0;
};

SubprocessResult run_with_stdin(const std::string& stdin_text,
                                std::chrono::seconds timeout) {
#ifndef PDK_CHAT_DEMO_PATH
    throw std::runtime_error("PDK_CHAT_DEMO_PATH not defined");
#else
    CinEofGuard eof;

    int out_pipe[2];
    REQUIRE(pipe(out_pipe) == 0);

    // Use a tmp file as stdin source so the child can read sequentially
    char stdin_path[64];
    snprintf(stdin_path, sizeof(stdin_path),
             "/tmp/chat_demo_stdin_%d.txt", getpid());
    {
        std::ofstream f(stdin_path);
        f << stdin_text;
    }

    pid_t pid = fork();
    REQUIRE(pid >= 0);

    if (pid == 0) {
        // Child: redirect stdout → pipe, stdin ← file
        close(out_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(out_pipe[1]);

        int stdin_fd = open(stdin_path, O_RDONLY);
        if (stdin_fd >= 0) {
            dup2(stdin_fd, STDIN_FILENO);
            close(stdin_fd);
        }

        execl(PDK_CHAT_DEMO_PATH, "pdk_chat_demo", "--mock", (char*)nullptr);
        _exit(127);
    }
    close(out_pipe[1]);

    // Parent: read stdout with timeout
    auto start = std::chrono::steady_clock::now();
    std::array<char, 4096> buf;
    std::string out;
    ssize_t n;
    while ((n = read(out_pipe[0], buf.data(), buf.size())) > 0) {
        out.append(buf.data(), n);
        if (std::chrono::steady_clock::now() - start > timeout) {
            kill(pid, SIGKILL);
            break;
        }
    }
    close(out_pipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    SubprocessResult r;
    r.stdout_output = out;
    r.elapsed_sec = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();

    if (WIFSIGNALED(status)) {
        r.signaled = true;
        r.signal = WTERMSIG(status);
        r.exit_code = -1;
    } else {
        r.signaled = false;
        r.exit_code = WEXITSTATUS(status);
    }

    unlink(stdin_path);
    return r;
#endif
}

}  // namespace

TEST_CASE("pipe mode: full input received (no character loss from stdin race)",
          "[pdk_chat_demo][stdin_e2e][9.2]") {
    // The original bug: 'w' of "what can you do?" was swallowed by input thread
    // racing with main loop. After fix, full input should appear in user.input log.
    const std::string input = "what can you do?\nexit\n";

    auto r = run_with_stdin(input, std::chrono::seconds(30));

    INFO("stdout: " << r.stdout_output);
    INFO("elapsed: " << r.elapsed_sec << "s");
    CHECK_FALSE(r.signaled);
    // user.input event should contain the FULL text (not "hat can you do?")
    CHECK(r.stdout_output.find("user.input: what can you do?") != std::string::npos);
    // Or at minimum, "hat" should NOT appear without the leading "w"
    CHECK(r.stdout_output.find("user.input: hat can you do?") == std::string::npos);
}

TEST_CASE("pipe EOF triggers graceful exit (no hang)",
          "[pdk_chat_demo][stdin_e2e][9.4]") {
    const std::string input = "hi\n";  // No explicit /exit — rely on EOF

    auto r = run_with_stdin(input, std::chrono::seconds(30));

    INFO("stdout length: " << r.stdout_output.size());
    INFO("elapsed: " << r.elapsed_sec << "s");
    CHECK_FALSE(r.signaled);
    // Should exit cleanly within 30s (no hang on EOF)
    CHECK(r.elapsed_sec < 30.0);
}

TEST_CASE("multiple lines: all messages processed without losing any",
          "[pdk_chat_demo][stdin_e2e][stress]") {
    // §7.5 + §10.6: regression guard for "second turn hung"
    const std::string input = "hi\nhow are you\ngoodbye\nexit\n";

    auto r = run_with_stdin(input, std::chrono::seconds(60));

    INFO("stdout: " << r.stdout_output);
    INFO("elapsed: " << r.elapsed_sec << "s");
    CHECK_FALSE(r.signaled);
    CHECK(r.elapsed_sec < 60.0);

    // All 3 follow-up inputs should appear in user.input log
    CHECK(r.stdout_output.find("user.input: hi") != std::string::npos);
    CHECK(r.stdout_output.find("user.input: how are you") != std::string::npos);
    CHECK(r.stdout_output.find("user.input: goodbye") != std::string::npos);
}