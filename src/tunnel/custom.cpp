#include "ghostclaw/tunnel/custom.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>

#ifndef _WIN32
#include <csignal>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ghostclaw::tunnel {

namespace {

#ifndef _WIN32
common::Result<TunnelProcess> spawn_custom_with_url(const std::string &command,
                                                     const std::vector<std::string> &args,
                                                     std::chrono::seconds timeout) {
  int pipefd[2] = {-1, -1};
  if (pipe(pipefd) != 0) {
    return common::Result<TunnelProcess>::failure("failed to create custom tunnel pipe");
  }

  const pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return common::Result<TunnelProcess>::failure("failed to fork custom tunnel");
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
      char buf[256] = {0};
      const ssize_t n = read(pipefd[0], buf, sizeof(buf));
      if (n > 0) {
        output.append(buf, static_cast<std::size_t>(n));
        const auto newline = output.find('\n');
        if (newline != std::string::npos) {
          std::string url = output.substr(0, newline);
          while (!url.empty() && std::isspace(static_cast<unsigned char>(url.back())) != 0) {
            url.pop_back();
          }
          while (!url.empty() && std::isspace(static_cast<unsigned char>(url.front())) != 0) {
            url.erase(url.begin());
          }
          if (!url.empty()) {
            close(pipefd[0]);
            TunnelProcess process;
            process.pid = pid;
            process.public_url = std::move(url);
            return common::Result<TunnelProcess>::success(std::move(process));
          }
        }
      }
    }

    int status = 0;
    if (waitpid(pid, &status, WNOHANG) == pid) {
      break;
    }
  }

  close(pipefd[0]);
  kill(pid, SIGTERM);
  int status = 0;
  (void)waitpid(pid, &status, 0);
  return common::Result<TunnelProcess>::failure("custom tunnel did not output a public URL");
}
#endif

void replace_all(std::string &value, const std::string &needle, const std::string &replacement) {
  std::size_t pos = 0;
  while ((pos = value.find(needle, pos)) != std::string::npos) {
    value.replace(pos, needle.size(), replacement);
    pos += replacement.size();
  }
}

} // namespace

CustomTunnel::CustomTunnel(std::string command, std::vector<std::string> args)
    : command_(std::move(command)), args_(std::move(args)) {}

common::Result<std::string> CustomTunnel::start(const std::string &local_host,
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
  return common::Result<std::string>::failure("custom tunnel is not implemented on Windows");
#else
  const std::string command = substitute_placeholders(command_, local_host, local_port);
  std::vector<std::string> replaced_args;
  replaced_args.reserve(args_.size());
  for (const auto &arg : args_) {
    replaced_args.push_back(substitute_placeholders(arg, local_host, local_port));
  }

  auto spawned = spawn_custom_with_url(command, replaced_args, std::chrono::seconds(15));
  if (!spawned.ok()) {
    return common::Result<std::string>::failure(spawned.error());
  }

  process_.set(spawned.value());
  return common::Result<std::string>::success(spawned.value().public_url);
#endif
}

common::Status CustomTunnel::stop() {
  process_.terminate();
  return common::Status::success();
}

bool CustomTunnel::health_check() { return process_.is_running() && process_.get_url().has_value(); }

std::optional<std::string> CustomTunnel::public_url() const { return process_.get_url(); }

std::string CustomTunnel::substitute_placeholders(const std::string &input, const std::string &host,
                                                  const std::uint16_t port) {
  std::string out = input;
  replace_all(out, "{host}", host);
  replace_all(out, "{port}", std::to_string(port));
  return out;
}

} // namespace ghostclaw::tunnel
