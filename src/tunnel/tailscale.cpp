#include "ghostclaw/tunnel/tailscale.hpp"

#include "ghostclaw/common/fs.hpp"

#include <regex>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ghostclaw::tunnel {

namespace {

#ifndef _WIN32
common::Result<std::string> run_command_capture(const std::string &command,
                                                 const std::vector<std::string> &args) {
  int pipefd[2] = {-1, -1};
  if (pipe(pipefd) != 0) {
    return common::Result<std::string>::failure("failed to create pipe");
  }

  const pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return common::Result<std::string>::failure("failed to fork process");
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
  char buffer[512] = {0};
  while (true) {
    const ssize_t n = read(pipefd[0], buffer, sizeof(buffer));
    if (n <= 0) {
      break;
    }
    output.append(buffer, static_cast<std::size_t>(n));
  }
  close(pipefd[0]);

  int status = 0;
  if (waitpid(pid, &status, 0) <= 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    return common::Result<std::string>::failure("command failed: " + command);
  }

  return common::Result<std::string>::success(std::move(output));
}
#endif

} // namespace

TailscaleTunnel::TailscaleTunnel(std::string command_path, std::string hostname)
    : command_path_(std::move(command_path)), configured_hostname_(std::move(hostname)) {}

common::Result<std::string> TailscaleTunnel::start(const std::string &local_host,
                                                    const std::uint16_t local_port) {
  (void)local_host;
  if (running_ && !public_url_.empty()) {
    return common::Result<std::string>::success(public_url_);
  }

#ifdef _WIN32
  (void)local_port;
  return common::Result<std::string>::failure("tailscale tunnel is not implemented on Windows");
#else
  auto started = run_command_capture(command_path_, {"serve", "--bg", std::to_string(local_port)});
  if (!started.ok()) {
    return common::Result<std::string>::failure(started.error());
  }

  std::string hostname = configured_hostname_;
  if (hostname.empty()) {
    auto resolved = get_tailscale_hostname();
    if (!resolved.ok()) {
      return common::Result<std::string>::failure(resolved.error());
    }
    hostname = resolved.value();
  }

  if (!hostname.empty() && hostname.back() == '.') {
    hostname.pop_back();
  }

  port_ = local_port;
  public_url_ = "https://" + hostname;
  running_ = true;
  return common::Result<std::string>::success(public_url_);
#endif
}

common::Status TailscaleTunnel::stop() {
#ifdef _WIN32
  running_ = false;
  port_ = 0;
  public_url_.clear();
  return common::Status::success();
#else
  if (port_ != 0) {
    (void)run_command_capture(command_path_, {"serve", "--remove", std::to_string(port_)});
  }
  running_ = false;
  port_ = 0;
  public_url_.clear();
  return common::Status::success();
#endif
}

bool TailscaleTunnel::health_check() { return running_ && !public_url_.empty(); }

std::optional<std::string> TailscaleTunnel::public_url() const {
  if (!running_ || public_url_.empty()) {
    return std::nullopt;
  }
  return public_url_;
}

common::Result<std::string> TailscaleTunnel::get_tailscale_hostname() const {
#ifdef _WIN32
  return common::Result<std::string>::failure("tailscale hostname lookup not implemented on Windows");
#else
  auto output = run_command_capture(command_path_, {"status", "--json"});
  if (!output.ok()) {
    return common::Result<std::string>::failure(output.error());
  }

  const std::regex dns_pattern(R"re("DNSName"\s*:\s*"([^"]+)")re");
  std::smatch match;
  if (std::regex_search(output.value(), match, dns_pattern) && match.size() > 1) {
    return common::Result<std::string>::success(match[1].str());
  }
  return common::Result<std::string>::failure("could not determine tailscale DNS name");
#endif
}

} // namespace ghostclaw::tunnel
