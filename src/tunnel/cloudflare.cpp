#include "ghostclaw/tunnel/cloudflare.hpp"

#include "ghostclaw/common/fs.hpp"

#include <chrono>
#include <regex>
#include <sstream>
#include <vector>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ghostclaw::tunnel {

namespace {

#ifndef _WIN32
common::Result<TunnelProcess> spawn_with_output_regex(const std::string &command,
                                                       const std::vector<std::string> &args,
                                                       const std::regex &pattern,
                                                       std::chrono::seconds timeout) {
  int pipefd[2] = {-1, -1};
  if (pipe(pipefd) != 0) {
    return common::Result<TunnelProcess>::failure("failed to create pipe");
  }

  const pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return common::Result<TunnelProcess>::failure("failed to fork tunnel process");
  }

  if (pid == 0) {
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[0]);
    close(pipefd[1]);

    std::vector<char *> cargs;
    cargs.reserve(args.size() + 2);
    cargs.push_back(const_cast<char *>(command.c_str()));
    for (const auto &arg : args) {
      cargs.push_back(const_cast<char *>(arg.c_str()));
    }
    cargs.push_back(nullptr);

    execvp(command.c_str(), cargs.data());
    _exit(127);
  }

  close(pipefd[1]);

  std::string output;
  output.reserve(2048);
  std::smatch match;
  const auto deadline = std::chrono::steady_clock::now() + timeout;

  while (std::chrono::steady_clock::now() < deadline) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(pipefd[0], &readfds);
    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 200 * 1000;

    const int sel = select(pipefd[0] + 1, &readfds, nullptr, nullptr, &tv);
    if (sel > 0 && FD_ISSET(pipefd[0], &readfds)) {
      char buf[512] = {0};
      const ssize_t n = read(pipefd[0], buf, sizeof(buf));
      if (n > 0) {
        output.append(buf, static_cast<std::size_t>(n));
        if (std::regex_search(output, match, pattern)) {
          close(pipefd[0]);
          TunnelProcess process;
          process.pid = pid;
          process.public_url = match[0].str();
          return common::Result<TunnelProcess>::success(std::move(process));
        }
      }
    }

    int status = 0;
    const pid_t done = waitpid(pid, &status, WNOHANG);
    if (done == pid) {
      break;
    }
  }

  close(pipefd[0]);
  kill(pid, SIGTERM);
  int status = 0;
  (void)waitpid(pid, &status, 0);
  return common::Result<TunnelProcess>::failure("failed to extract cloudflare public URL");
}
#endif

} // namespace

CloudflareTunnel::CloudflareTunnel(std::string command_path)
    : command_path_(std::move(command_path)) {}

common::Result<std::string> CloudflareTunnel::start(const std::string &local_host,
                                                     const std::uint16_t local_port) {
  if (process_.is_running()) {
    const auto url = process_.get_url();
    if (url.has_value()) {
      return common::Result<std::string>::success(*url);
    }
  }

#ifdef _WIN32
  (void)local_host;
  (void)local_port;
  return common::Result<std::string>::failure("cloudflare tunnel is not implemented on Windows");
#else
  const std::string local_url = "http://" + local_host + ":" + std::to_string(local_port);
  const std::regex url_pattern(R"(https://[a-z0-9-]+\.trycloudflare\.com)");
  auto spawned = spawn_with_output_regex(command_path_, {"tunnel", "--url", local_url},
                                         url_pattern, std::chrono::seconds(30));
  if (!spawned.ok()) {
    return common::Result<std::string>::failure(spawned.error());
  }

  process_.set(spawned.value());
  return common::Result<std::string>::success(spawned.value().public_url);
#endif
}

common::Status CloudflareTunnel::stop() {
  process_.terminate();
  return common::Status::success();
}

bool CloudflareTunnel::health_check() { return process_.is_running() && process_.get_url().has_value(); }

std::optional<std::string> CloudflareTunnel::public_url() const { return process_.get_url(); }

std::optional<std::string>
CloudflareTunnel::extract_url_from_output(const std::string &output) const {
  const std::regex url_pattern(R"(https://[a-z0-9-]+\.trycloudflare\.com)");
  std::smatch match;
  if (std::regex_search(output, match, url_pattern)) {
    return match[0].str();
  }
  return std::nullopt;
}

} // namespace ghostclaw::tunnel
