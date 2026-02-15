#include "ghostclaw/sandbox/docker.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ghostclaw::sandbox {

namespace {

void set_non_blocking(const int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) {
    (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
}

void read_into_buffer(const int fd, std::string &buffer) {
  std::array<char, 4096> chunk{};
  while (true) {
    const ssize_t bytes = read(fd, chunk.data(), chunk.size());
    if (bytes > 0) {
      buffer.append(chunk.data(), static_cast<std::size_t>(bytes));
      continue;
    }
    if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return;
    }
    return;
  }
}

std::string join_args(const std::vector<std::string> &args) {
  std::string out;
  for (const auto &arg : args) {
    if (!out.empty()) {
      out.push_back(' ');
    }
    out += arg;
  }
  return out;
}

} // namespace

common::Result<DockerProcessResult>
DockerCliRunner::run(const std::vector<std::string> &args, const DockerCommandOptions &options) {
  if (args.empty()) {
    return common::Result<DockerProcessResult>::failure("docker command is empty");
  }

  int stdout_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};
  if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
    if (stdout_pipe[0] >= 0) {
      close(stdout_pipe[0]);
    }
    if (stdout_pipe[1] >= 0) {
      close(stdout_pipe[1]);
    }
    if (stderr_pipe[0] >= 0) {
      close(stderr_pipe[0]);
    }
    if (stderr_pipe[1] >= 0) {
      close(stderr_pipe[1]);
    }
    return common::Result<DockerProcessResult>::failure("failed to create pipes for docker");
  }

  const pid_t pid = fork();
  if (pid < 0) {
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);
    return common::Result<DockerProcessResult>::failure("failed to fork docker process");
  }

  if (pid == 0) {
    (void)dup2(stdout_pipe[1], STDOUT_FILENO);
    (void)dup2(stderr_pipe[1], STDERR_FILENO);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);

    std::vector<char *> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char *>("docker"));
    for (const auto &arg : args) {
      argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);

    execvp("docker", argv.data());
    _exit(127);
  }

  close(stdout_pipe[1]);
  close(stderr_pipe[1]);
  set_non_blocking(stdout_pipe[0]);
  set_non_blocking(stderr_pipe[0]);

  std::string stdout_text;
  std::string stderr_text;
  int status = 0;
  bool timed_out = false;
  const auto started = std::chrono::steady_clock::now();

  while (true) {
    read_into_buffer(stdout_pipe[0], stdout_text);
    read_into_buffer(stderr_pipe[0], stderr_text);

    const pid_t waited = waitpid(pid, &status, WNOHANG);
    if (waited == pid) {
      break;
    }

    const auto elapsed = std::chrono::steady_clock::now() - started;
    if (elapsed > options.timeout) {
      timed_out = true;
      (void)kill(pid, SIGKILL);
      (void)waitpid(pid, &status, 0);
      break;
    }

    struct pollfd poll_fds[2] = {
        {.fd = stdout_pipe[0], .events = POLLIN, .revents = 0},
        {.fd = stderr_pipe[0], .events = POLLIN, .revents = 0},
    };
    (void)poll(poll_fds, 2, 50);
  }

  read_into_buffer(stdout_pipe[0], stdout_text);
  read_into_buffer(stderr_pipe[0], stderr_text);
  close(stdout_pipe[0]);
  close(stderr_pipe[0]);

  DockerProcessResult result;
  result.stdout_text = std::move(stdout_text);
  result.stderr_text = std::move(stderr_text);
  result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

  if (timed_out) {
    result.exit_code = -1;
    if (!options.allow_failure) {
      return common::Result<DockerProcessResult>::failure("docker command timed out: " +
                                                           join_args(args));
    }
  }

  if (result.exit_code != 0 && !options.allow_failure) {
    const std::string message = result.stderr_text.empty()
                                    ? "docker command failed: " + join_args(args)
                                    : result.stderr_text;
    return common::Result<DockerProcessResult>::failure(message);
  }

  return common::Result<DockerProcessResult>::success(std::move(result));
}

} // namespace ghostclaw::sandbox
