#include "ghostclaw/tools/builtin/shell.hpp"

#include <array>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace ghostclaw::tools {

namespace {

constexpr std::size_t kMaxOutputBytes = 1024 * 1024;

common::Result<std::string> required_arg(const ToolArgs &args, const std::string &name) {
  const auto it = args.find(name);
  if (it == args.end() || it->second.empty()) {
    return common::Result<std::string>::failure("Missing argument: " + name);
  }
  return common::Result<std::string>::success(it->second);
}

} // namespace

ShellTool::ShellTool(std::shared_ptr<security::SecurityPolicy> policy) : policy_(std::move(policy)) {}

std::string_view ShellTool::name() const { return "shell"; }

std::string_view ShellTool::description() const { return "Execute an allowlisted shell command"; }

std::string ShellTool::parameters_schema() const {
  return R"({"type":"object","required":["command"],"properties":{"command":{"type":"string"}}})";
}

common::Result<ToolResult> ShellTool::execute(const ToolArgs &args, const ToolContext &ctx) {
  if (!policy_) {
    return common::Result<ToolResult>::failure("security policy unavailable");
  }

  auto command_result = required_arg(args, "command");
  if (!command_result.ok()) {
    return common::Result<ToolResult>::failure(command_result.error());
  }
  const std::string command = command_result.value();

  if (!policy_->is_command_allowed(command)) {
    return common::Result<ToolResult>::failure("Command not allowed by policy");
  }

  if (!policy_->check_rate_limit()) {
    return common::Result<ToolResult>::failure("Rate limit exceeded");
  }

  int pipefd[2] = {-1, -1};
  if (pipe(pipefd) != 0) {
    return common::Result<ToolResult>::failure("Failed to create pipe");
  }

  const pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return common::Result<ToolResult>::failure("Failed to fork");
  }

  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);

    if (!ctx.workspace_path.empty()) {
      (void)chdir(ctx.workspace_path.c_str());
    }

    execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char *>(nullptr));
    _exit(127);
  }

  close(pipefd[1]);
  const int flags = fcntl(pipefd[0], F_GETFL, 0);
  fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

  std::string output;
  output.reserve(4096);
  bool truncated = false;
  bool timeout = false;

  const auto started = std::chrono::steady_clock::now();
  const auto timeout_limit = std::chrono::milliseconds(timeout_ms());

  int status = 0;
  bool running = true;
  while (running) {
    const auto now = std::chrono::steady_clock::now();
    if (now - started > timeout_limit) {
      kill(pid, SIGKILL);
      timeout = true;
      running = false;
      break;
    }

    struct pollfd pfd {
      .fd = pipefd[0], .events = POLLIN, .revents = 0,
    };
    (void)poll(&pfd, 1, 50);

    std::array<char, 4096> buffer{};
    const ssize_t bytes = read(pipefd[0], buffer.data(), buffer.size());
    if (bytes > 0) {
      const std::size_t remaining =
          kMaxOutputBytes > output.size() ? kMaxOutputBytes - output.size() : 0;
      const std::size_t to_copy = std::min<std::size_t>(remaining, static_cast<std::size_t>(bytes));
      output.append(buffer.data(), to_copy);
      if (to_copy < static_cast<std::size_t>(bytes) || output.size() >= kMaxOutputBytes) {
        truncated = true;
      }
    }

    const pid_t done = waitpid(pid, &status, WNOHANG);
    if (done == pid) {
      running = false;
    }
  }

  // Drain remaining output.
  std::array<char, 4096> buffer{};
  while (true) {
    const ssize_t bytes = read(pipefd[0], buffer.data(), buffer.size());
    if (bytes <= 0) {
      break;
    }
    const std::size_t remaining = kMaxOutputBytes > output.size() ? kMaxOutputBytes - output.size() : 0;
    const std::size_t to_copy = std::min<std::size_t>(remaining, static_cast<std::size_t>(bytes));
    output.append(buffer.data(), to_copy);
    if (to_copy < static_cast<std::size_t>(bytes) || output.size() >= kMaxOutputBytes) {
      truncated = true;
    }
  }

  close(pipefd[0]);
  waitpid(pid, &status, 0);

  policy_->record_action();

  ToolResult result;
  result.output = output;
  result.truncated = truncated;
  if (truncated) {
    result.output += "\n[output truncated]";
  }

  if (timeout) {
    result.success = false;
    result.output += "\n[command timed out]";
  } else {
    result.success = WIFEXITED(status) && (WEXITSTATUS(status) == 0);
    result.metadata["exit_code"] =
        WIFEXITED(status) ? std::to_string(WEXITSTATUS(status)) : "signal";
  }

  return common::Result<ToolResult>::success(std::move(result));
}

bool ShellTool::is_safe() const { return false; }

std::string_view ShellTool::group() const { return "runtime"; }

} // namespace ghostclaw::tools
